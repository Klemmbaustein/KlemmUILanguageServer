#include "Protocol.h"
#include <iostream>
#include <Markup/MarkupParse.h>
#include <Markup/ParseError.h>
#include <unordered_set>
#include <Markup/MarkupVerify.h>

void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos)
	{
		str.replace(start_pos, from.size(), to);
		start_pos += to.size();
	}
}
std::string Protocol::ConvertFilePath(std::string FilePathUri)
{
	replaceAll(FilePathUri, "%3A", ":");
	replaceAll(FilePathUri, "%3a", ":");
	return FilePathUri.substr(8);
}

void Protocol::Init()
{
}

void Protocol::PublishDiagnostics(std::vector<Protocol::DiagnosticError> Error, std::string File)
{
	json DiagnosticsJson = json::array();

	for (auto& i : Error)
	{
		// clang-format off
		DiagnosticsJson.push_back(json::object({ 
			{"message", i.Message},
			{"severity", i.Severity},
			{"range", {{"start", {
				{ "line", i.Line },
				{ "character", i.Begin },
			}},
			{ "end", {
				{ "line", i.Line },
				{ "character", i.End },
			}}}}}));
		// clang-format on
	}

	// clang-format off
	Message NewMessage = Message("textDocument/publishDiagnostics", {
		{"uri", File},
		{"diagnostics", DiagnosticsJson}
	}, true);
	// clang-format on
	NewMessage.Send();
}

static kui::MarkupStructure::ParseResult LastParseResult;

void Protocol::ScanFile(std::string Content, std::string Uri)
{
	kui::MarkupParse::FileEntry Entry;
	Entry.Name = Uri;
	Entry.Content = Content;

	std::vector<DiagnosticError> FoundErrors;

	kui::parseError::ErrorCallback = [Uri, &FoundErrors](std::string ErrorText, size_t ErrorLine, size_t Begin, size_t End) {
		FoundErrors.push_back(DiagnosticError{ .Message = ErrorText, .Line = ErrorLine, .Begin = Begin, .End = End });
	};
	LastParseResult = kui::MarkupParse::ParseFiles({ Entry });
	kui::markupVerify::Verify(LastParseResult);

	PublishDiagnostics(FoundErrors, Uri);
}

static std::string GetTooltipFromElement(kui::MarkupStructure::UIElement From, std::string File)
{
	using namespace kui::MarkupStructure;
	using namespace Protocol;

	if (From.Type == UIElement::ElementType::UserDefined)
		return "`element " + From.TypeName.Text + " : UIBox`\n\nUser defined element. Defined in `" + ConvertFilePath(File) + "`";

	PropElementType Type = GetTypeFromString(From.TypeName);

	if (IsSubclassOf(Type, PropElementType::UIBackground) && Type != PropElementType::UIBackground)
	{
		return "`element " + From.TypeName.Text + " : UIBackground`\n\nNative (C++) element.";
	}
	return "`element " + From.TypeName.Text + " : UIBox`\n\nNative (C++) element.";
}

static kui::MarkupStructure::UIElement* GetClosestElement(std::vector<kui::MarkupStructure::UIElement>& From, size_t Line)
{
	for (auto& i : From)
	{
		if (i.StartLine <= Line && i.EndLine >= Line)
		{
			auto* Closest = GetClosestElement(i.Children, Line);
			if (Closest)
				return Closest;
			return &i;
		}
	}
	return nullptr;
}

static std::optional<kui::MarkupStructure::UIElement> GetElementAt(size_t Line)
{
	for (auto& i : LastParseResult.Elements)
	{
		std::vector RootArray = { i.Root };
		auto* Token = GetClosestElement(RootArray, Line);
		if (Token)
			return *Token;
	}
	return {};
}

std::pair<std::string, std::string> GetPropertyInfo(const kui::MarkupStructure::PropertyElement& From)
{
	using namespace kui::MarkupStructure;

	std::string Detail = "(" + UIElement::Variable::Descriptions[From.VarType].Name + ") " + GetStringFromType(From.Type) + "." + From.Name;

	if (!From.Default.empty())
	{
		Detail.append(" (default: " + From.Default + ")");
	}
	Detail += "\n";


	return { Detail, From.Description };
}

std::pair<std::string, std::string> GetPropertyInfo(const kui::MarkupStructure::Property& From,
	const kui::MarkupStructure::UIElement& FromElement)
{
	using namespace kui::MarkupStructure;

	PropElementType ElementType = GetTypeFromString(FromElement.TypeName.Text);

	for (const auto& i : Properties)
	{
		if (!IsSubclassOf(ElementType, i.Type))
		{
			continue;
		}

		if (i.Name == From.Name.Text)
		{
			return GetPropertyInfo(i);
		}
	}

	return {};
}

static std::string GetElementHoverMessage(kui::MarkupStructure::UIElement& FromElement, size_t Char, size_t Line, std::string File)
{
	if (FromElement.TypeName.BeginChar <= Char && FromElement.TypeName.EndChar > Char && Line == FromElement.TypeName.Line)
	{
		return GetTooltipFromElement(FromElement, File);
	}

	for (auto& Param : FromElement.ElementProperties)
	{
		if (Param.Name.BeginChar <= Char && Param.Name.EndChar > Char && Line == Param.Name.Line)
		{
			auto Info = GetPropertyInfo(Param, FromElement);
			return "`" + Info.first + "`\n\n" + Info.second;
		}
	}

	for (auto& Child : FromElement.Children)
	{
		std::string Message = GetElementHoverMessage(Child, Char, Line,  File);
		if (!Message.empty())
		{
			return Message;
		}
	}

	return "";
}

static std::string GetHoverMessage(std::string File, size_t Char, size_t Line)
{
	for (auto& i : LastParseResult.Elements)
	{
		if (i.File != File)
			continue;

		std::string HoverMessage = GetElementHoverMessage(i.Root, Char, Line, File);

		if (!HoverMessage.empty())
			return HoverMessage;
	}
	return "";
}

static json GetTokenCompletions(std::string File, kui::stringParse::StringToken Token)
{
	using namespace kui::MarkupStructure;

	json CompletionArray = json::array();

	std::optional<UIElement> Elem = GetElementAt(Token.Line);
	std::cerr << Elem.has_value() << ", " << Token.Line << std::endl;
	std::unordered_set<std::string> AutoCompleteValues;

	auto AddKeyword = [&CompletionArray](std::string Name) {
		CompletionArray.push_back({ { "label", Name },
			{ "kind", 14 } });
	};

	if (Elem.has_value())
	{
		AddKeyword("child");
		AddKeyword("var");

		PropElementType ElementType = GetTypeFromString(Elem->TypeName);

		for (const auto& i : Properties)
		{
			if (!IsSubclassOf(ElementType, i.Type))
			{
				continue;
			}
			if (AutoCompleteValues.contains(i.Name))
				continue;
			AutoCompleteValues.insert(i.Name);

			auto Info = GetPropertyInfo(i);

			// clang-format off
			CompletionArray.push_back({
				{ "label", i.Name },
				{ "detail", Info.first },
				{ "documentation", Info.second },
				{ "kind", 6 }
			});
			// clang-format on
		}
	}
	else
	{
		AddKeyword("element");
		AddKeyword("const");
	}
	return CompletionArray;
}

void Protocol::HandleClientMessage(Message msg)
{
	if (!msg.IsRequest)
	{
		HandleClientNotification(msg);
		return;
	}

	if (msg.Method == "initialize")
	{
		// clang-format off
		ResponseMessage Response = ResponseMessage(msg, { 
			{ "capabilities", {
				{"hoverProvider", true},
				{"textDocumentSync", {
					{ "openClose", true },
					{ "change", 1 }
				}},
				{"diagnosticProvider", {
					{ "interFileDiagnostics", true },
					{ "workspaceDiagnostics", false }
				}},
				{"semanticTokensProvider", {
					{"full", true},
					{"legend", {}}
				}},
				{"completionProvider", json::object()}
			//	{ "positionEncoding", "utf-8" }
			}} });
		// clang-format on
		Response.Send();
	}
	else if (msg.Method == "textDocument/hover")
	{
		std::string Message = GetHoverMessage(
			msg.MessageJson.at("textDocument").at("uri"),
			msg.MessageJson.at("position").at("character"),
			msg.MessageJson.at("position").at("line"));
		// clang-format off
		ResponseMessage Response = ResponseMessage(msg, {
				{"contents", Message.empty() ? json(json::value_t::null) : json(Message)}
			});
		// clang-format on
		Response.Send();
	}
	else if (msg.Method == "textDocument/completion")
	{
		std::string Document = msg.MessageJson.at("textDocument").at("uri");

		size_t Character = msg.MessageJson.at("position").at("character");
		size_t Line = msg.MessageJson.at("position").at("line");

		std::optional Token = kui::stringParse::GetTokenAt(LastParseResult.FileLines[Document],
			Character, Line);

		// clang-format off
		ResponseMessage Response = ResponseMessage(msg, GetTokenCompletions(Document,
			Token.value_or(kui::stringParse::StringToken("", Line, Character, Character + 1))));
		// clang-format on
		Response.Send();
	}
	else
	{
		std::cerr << "unhandled method: " << msg.Method << std::endl;
	}
}

void Protocol::HandleClientNotification(Message msg)
{
	if (msg.Method == "textDocument/didOpen")
	{
		ScanFile(msg.MessageJson.at("textDocument").at("text"), msg.MessageJson.at("textDocument").at("uri"));
	}
	if (msg.Method == "textDocument/didChange")
	{
		ScanFile(msg.MessageJson.at("contentChanges").at(0).at("text"), msg.MessageJson.at("textDocument").at("uri"));
	}
}
