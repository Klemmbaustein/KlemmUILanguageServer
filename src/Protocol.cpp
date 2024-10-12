#include "Protocol.h"
#include "Util/StrUtil.h"
#include <iostream>
#include <Markup/MarkupParse.h>
#include <Markup/ParseError.h>
#include <unordered_set>
#include <Markup/MarkupVerify.h>

namespace Protocol
{
	bool AllowMarkdownInHover = false;
	bool ReceivedShutdownRequest = false;

	json SemanticTokensArray = json::array();
}

namespace Protocol::Tokens
{
	using namespace kui;

	static json GetTokenLegends()
	{
		return {
			{ "tokenTypes", { "type", "property", "cppLocalVariable" } },
			{ "tokenModifiers", { "readonly" } }
		};
	}

	struct Token
	{
		stringParse::StringToken Token;
		int Type = 0;
		int Modifier = 0;
	};

	constexpr int TYPE = 0;
	constexpr int PROPERTY = 1;
	constexpr int VARIABLE = 2;

	constexpr int MOD_READONLY = 1;

	static json ConvertTokensToJson(std::vector<Token> Tokens)
	{
		json Out = json::array();

		std::sort(Tokens.begin(), Tokens.end(), [](const Token& a, const Token& b) {
			if (a.Token.Line == b.Token.Line)
				return a.Token.BeginChar < b.Token.EndChar;
			return a.Token.Line < b.Token.Line;
		});

		size_t CurrentLine = 0;
		size_t Character = 0;

		for (const Token& i : Tokens)
		{
			if (i.Token.Line != CurrentLine)
				Character = 0;

			Out.push_back(i.Token.Line - CurrentLine);
			Out.push_back(i.Token.BeginChar - Character);
			Out.push_back(i.Token.EndChar - i.Token.BeginChar);
			Out.push_back(i.Type);
			Out.push_back(i.Modifier);
			CurrentLine = i.Token.Line;
			Character = i.Token.BeginChar;
		}

		return Out;
	}
}

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
	return FilePathUri.substr(strlen("file:///"));
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

static void ScanElementForTokens(kui::MarkupStructure::UIElement& Element, std::vector<Protocol::Tokens::Token>& FileTokens)
{
	using namespace Protocol;

	FileTokens.push_back(Tokens::Token{
		.Token = Element.TypeName,
		.Type = Tokens::TYPE });

	if (!Element.ElementName.Empty())
	{
		FileTokens.push_back(Tokens::Token{
			.Token = Element.ElementName,
			.Type = Tokens::PROPERTY });
	}

	for (kui::MarkupStructure::Property& i : Element.ElementProperties)
	{
		FileTokens.push_back(Tokens::Token{
			.Token = i.Name,
			.Type = Tokens::VARIABLE });
	}

	for (kui::MarkupStructure::UIElement& Child : Element.Children)
	{
		ScanElementForTokens(Child, FileTokens);
	}
}

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

	std::vector<Tokens::Token> FileTokens;
	for (auto& i : LastParseResult.Globals)
	{
		FileTokens.push_back(Tokens::Token{
			.Token = i.Name,
			.Type = Tokens::VARIABLE,
			.Modifier = 0 });
	}

	for (auto& i : LastParseResult.Constants)
	{
		FileTokens.push_back(Tokens::Token{
			.Token = i.Name,
			.Type = Tokens::VARIABLE,
			.Modifier = Tokens::MOD_READONLY });
	}

	for (auto& i : LastParseResult.Elements)
	{
		ScanElementForTokens(i.Root, FileTokens);
	}

	SemanticTokensArray = Tokens::ConvertTokensToJson(FileTokens);
}

static std::string GetTooltipFromElement(kui::MarkupStructure::UIElement From, std::string File)
{
	using namespace kui::MarkupStructure;
	using namespace Protocol;

	std::string FilePath = ConvertFilePath(File);

	PropElementType Type = GetTypeFromString(From.TypeName);
	std::string Name = From.TypeName.Text;

	std::string DerivedFrom = IsSubclassOf(Type, PropElementType::UIBackground) && Type != PropElementType::UIBackground ? "UIBackground" : "UIBox";

	if (AllowMarkdownInHover)
	{
		if (From.Type == UIElement::ElementType::UserDefined)
			return StrUtil::Format("`element %s : %s`\n\nUser defined element.", Name.c_str(), DerivedFrom.c_str());
		return StrUtil::Format("`element %s : %s`\n\nNative (C++) element.", Name.c_str(), DerivedFrom.c_str());
	}

	if (From.Type == UIElement::ElementType::UserDefined)
		return StrUtil::Format("element %s : %s\nUser defined element.", Name.c_str(), DerivedFrom.c_str());
	return StrUtil::Format("element %s : %s\nNative (C++) element.", Name.c_str(), DerivedFrom.c_str());
}

static kui::MarkupStructure::UIElement* GetClosestElement(std::vector<kui::MarkupStructure::UIElement>& From, size_t Line, size_t Character)
{
	for (auto& i : From)
	{
		if (i.StartLine > Line || i.EndLine < Line)
			continue;
		if (i.EndLine == Line && i.StartChar <= Character)
			continue;
		if (i.EndLine == Line && i.EndChar >= Character)
			continue;

		auto* Closest = GetClosestElement(i.Children, Line, Character);
		if (Closest)
			return Closest;
		return &i;
	}
	return nullptr;
}

static std::optional<kui::MarkupStructure::UIElement> GetElementAt(size_t Line, size_t Character)
{
	for (auto& i : LastParseResult.Elements)
	{
		std::vector RootArray = { i.Root };
		auto* Token = GetClosestElement(RootArray, Line, Character);
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
			if (Protocol::AllowMarkdownInHover)
				return "`" + Info.first + "`\n\n" + Info.second;
			return Info.first + "\n" + Info.second;
		}
	}

	for (auto& Child : FromElement.Children)
	{
		std::string Message = GetElementHoverMessage(Child, Char, Line, File);
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

	std::optional<UIElement> Elem = GetElementAt(Token.Line, Token.BeginChar);
	std::unordered_set<std::string> AutoCompleteValues;

	auto AddKeyword = [&CompletionArray](std::string Name, std::string Detail) {
		CompletionArray.push_back({ { "label", Name },
			{ "detail", Detail },
			{ "kind", 14 } });
	};

	if (Elem.has_value())
	{
		AddKeyword("child", "Child element keyword");
		AddKeyword("var", "Variable keyword");

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
		AddKeyword("element", "Declares element");
		AddKeyword("const", "Compile-time constant");
		AddKeyword("global", "Global variable modifiable at runtime");
	}
	return CompletionArray;
}

static json GetFoldingRanges(kui::MarkupStructure::UIElement& From)
{
	json RangesArray = json::array();
	RangesArray.push_back({ { "startLine", From.TypeName.Line },
		{ "startCharacter", From.TypeName.EndChar },
		{ "endLine", From.EndLine },
		{ "endCharacter", From.EndChar } });

	for (auto& Child : From.Children)
	{
		json ChildRanges = GetFoldingRanges(Child);
		for (json Range : ChildRanges)
		{
			RangesArray.push_back(Range);
		}
	}

	return RangesArray;
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
		json::json_pointer ContentFormat = "/capabilities/textDocument/hover/contentFormat"_json_pointer;
		if (msg.MessageJson.contains(ContentFormat))
		{
			auto FormatJson = msg.MessageJson.at(ContentFormat);
			AllowMarkdownInHover = std::find(FormatJson.begin(), FormatJson.end(), "markdown") != FormatJson.end();
		}

		std::cerr << msg.MessageJson.dump(2) << std::endl;

		// TODO: Read the content of the initialize method instead of just assuming basic capabilities.
		// clang-format off
		ResponseMessage Response = ResponseMessage(msg, { 
			{ "capabilities", {
				{"hoverProvider", true},
				{"textDocumentSync", {
					{"openClose", true},
					{"change", 1}
				}},
				{"diagnosticProvider", {
					{"interFileDiagnostics", true},
					{"workspaceDiagnostics", false}
				}},
				{"foldingRangeProvider", true},
				{"semanticTokensProvider", {
					{"full", true},
					{"legend", Tokens::GetTokenLegends()}
				}},
				{"completionProvider", json::object()}
			// TODO: Properly handle the position encoding.
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
			kui::stringParse::StringToken("", Character, Character + 1, Line)));
		// clang-format on
		Response.Send();
	}
	else if (msg.Method == "textDocument/foldingRange")
	{
		json ResponseArray = json::array();
		for (auto& i : LastParseResult.Elements)
		{
			auto Array = GetFoldingRanges(i.Root);

			for (auto& Range : Array)
			{
				ResponseArray.push_back(Range);
			}
		}
		ResponseMessage Response = ResponseMessage(msg, ResponseArray);
		Response.Send();
	}
	else if (msg.Method == "textDocument/semanticTokens/full")
	{
		json File = msg.MessageJson.at("/textDocument/uri"_json_pointer);

		if (!LastParseResult.FileLines.contains(File.get<std::string>()))
		{
			ResponseMessage Response = ResponseMessage(msg, json(), ResponseMessage::ResponseError(LSPErrorCode::InvalidParams, "File not found: " + File));
			Response.Send();
			return;
		}
		ResponseMessage Response = ResponseMessage(msg, { { "data", SemanticTokensArray } });
		Response.Send();
	}
	else if (msg.Method == "shutdown")
	{
		ResponseMessage Response = ResponseMessage(msg, json());
		Response.Send();
		ReceivedShutdownRequest = true;
	}
	else
	{
		std::cerr << "unhandled method: " << msg.Method << std::endl;
	}
}

void Protocol::HandleClientNotification(Message msg)
{
	if (msg.Method == "exit")
	{
		if (!ReceivedShutdownRequest)
			exit(1);
		exit(0);
	}
	else if (msg.Method == "initialized")
	{
		return;
	}
	else if (msg.Method == "textDocument/didOpen")
	{
		ScanFile(msg.MessageJson.at("textDocument").at("text"), msg.MessageJson.at("textDocument").at("uri"));
	}
	else if (msg.Method == "textDocument/didChange")
	{
		ScanFile(msg.MessageJson.at("contentChanges").at(0).at("text"), msg.MessageJson.at("textDocument").at("uri"));
	}
	else
		std::cerr << "unhandled notify: " << msg.Method << std::endl;
}
