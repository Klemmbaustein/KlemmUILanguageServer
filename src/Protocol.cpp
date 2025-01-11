#include "Protocol.h"
#include "Workspace.h"
#include "Util/StrUtil.h"
#include <iostream>
#include <Markup/MarkupParse.h>
#include <Markup/ParseError.h>
#include <unordered_set>
#include <Markup/MarkupVerify.h>
using namespace kui::MarkupStructure;

namespace protocol
{
	bool AllowMarkdownInHover = false;
	bool ReceivedShutdownRequest = false;
	bool HasVsCppLocalVariable = true;

	struct VariableUsage
	{
		enum UsageType
		{
			Global,
			Const,
			Var,
		};

		UsageType Type = Global;
		kui::stringParse::StringToken Token;
		std::string File;

		union
		{
			kui::MarkupStructure::Global* FromGlobal;
			kui::MarkupStructure::Constant* FromConstant;
			kui::MarkupStructure::MarkupElement* VariableElement;
		};
	};

	std::map<std::string, std::vector<VariableUsage>> VariableUsages;
	static kui::MarkupStructure::ParseResult LastParseResult;
	std::vector<DiagnosticError> LastDiagnostics;
}

namespace protocol::tokens
{
	using namespace kui;

	static json GetTokenLegends()
	{
		return {
			{ "tokenTypes", { "type", "property", HasVsCppLocalVariable ? "cppLocalVariable" : "variable" } },
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

	static void ScanElementForTokens(kui::MarkupStructure::UIElement& Element, std::vector<protocol::tokens::Token>& FileTokens)
	{
		using namespace protocol;

		FileTokens.push_back(tokens::Token{
			.Token = Element.TypeName,
			.Type = tokens::TYPE });

		if (!Element.ElementName.Empty())
		{
			FileTokens.push_back(tokens::Token{
				.Token = Element.ElementName,
				.Type = tokens::PROPERTY });
		}

		for (kui::MarkupStructure::Property& i : Element.ElementProperties)
		{
			FileTokens.push_back(tokens::Token{
				.Token = i.Name,
				.Type = tokens::VARIABLE });
		}

		for (kui::MarkupStructure::UIElement& Child : Element.Children)
		{
			ScanElementForTokens(Child, FileTokens);
		}
	}

	static json GetDocumentTokens(std::string FileName)
	{
		using namespace workspace;
		std::vector<tokens::Token> FileTokens;
		for (auto& i : LastParseResult.Globals)
		{
			if (workspace::CompareFiles(ConvertFilePath(i.File), ConvertFilePath(FileName)))
				FileTokens.push_back(tokens::Token{
				.Token = i.Name,
				.Type = VARIABLE,
				.Modifier = 0 });
		}

		for (auto& i : LastParseResult.Constants)
		{
			if (workspace::CompareFiles(ConvertFilePath(i.File), ConvertFilePath(FileName)))
				FileTokens.push_back(tokens::Token{
				.Token = i.Name,
				.Type = VARIABLE,
				.Modifier = 0 });
		}

		for (auto& i : LastParseResult.Elements)
		{
			if (workspace::CompareFiles(ConvertFilePath(i.File), ConvertFilePath(FileName)))
				ScanElementForTokens(i.Root, FileTokens);
		}

		for (auto& i : VariableUsages)
		{
			for (auto& Usage : i.second)
			{
				if (workspace::CompareFiles(ConvertFilePath(Usage.File), ConvertFilePath(FileName)))
					FileTokens.push_back(tokens::Token{
					.Token = Usage.Token,
					.Type = Usage.Type == VariableUsage::Var ? PROPERTY : VARIABLE,
					.Modifier = Usage.Type == VariableUsage::Const ? MOD_READONLY : 0 });
			}
		}

		return ConvertTokensToJson(FileTokens);
	}
}

void protocol::Init()
{
}

static std::string GetGlobalHoverMessage(kui::MarkupStructure::Global* From)
{
	using namespace protocol;
	using namespace workspace;

	if (AllowMarkdownInHover)
	{
		if (From->Value.empty())
			return "`global " + From->Name.Text + "´\n\nDefined in `" + GetDisplayName(From->File) + "`";
		return "`global " + From->Name.Text + " = " + From->Value + "´\n\nDefined in `" + GetDisplayName(From->File) + "`";
	}
	if (From->Value.empty())
		return "global " + From->Name.Text + "\nDefined in " + GetDisplayName(From->File);
	return "global " + From->Name.Text + " = " + From->Value + "\nDefined in " + GetDisplayName(From->File);
}

static std::string GetConstHoverMessage(kui::MarkupStructure::Constant* From)
{
	using namespace protocol;
	using namespace workspace;

	if (AllowMarkdownInHover)
	{
		return "`const " + From->Name.Text + " = " + From->Value + "´\n\nDefined in `" + GetDisplayName(From->File) + "`";
	}
	return "const " + From->Name.Text + " = " + From->Value + "\nDefined in " + GetDisplayName(From->File);
}

static std::string GetVariableHoverMessage(std::string Name, kui::MarkupStructure::MarkupElement* From)
{
	using namespace protocol;
	if (AllowMarkdownInHover)
	{
		return "`var " + From->FromToken.Text + "." + Name + "´";
	}
	return "var " + From->FromToken.Text + "." + Name;
}

static std::string GetElementHoverMessage(kui::MarkupStructure::UIElement From, std::string File)
{
	using namespace kui::MarkupStructure;
	using namespace protocol;
	using namespace workspace;

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

static void ScanForVariableUsages(kui::MarkupStructure::UIElement& Target, kui::MarkupStructure::MarkupElement& Root)
{
	using namespace protocol;
	using namespace kui;

	auto AddVariableUsage = [](const std::string& Name, const VariableUsage& Usage) {

		if (VariableUsages.contains(Name))
			VariableUsages[Name].push_back(Usage);
		else
			VariableUsages.insert({ Name, { Usage } });
		};

	for (auto& i : Target.ElementProperties)
	{
		MarkupStructure::Global* g = LastParseResult.GetGlobal(i.Value);
		if (g)
		{
			AddVariableUsage(g->Name.Text, VariableUsage{
				.Type = VariableUsage::Global,
				.Token = i.Value,
				.File = Root.File,
				.FromGlobal = g
				});
			continue;
		}
		MarkupStructure::Constant* c = LastParseResult.GetConstant(i.Value);
		if (c)
		{
			AddVariableUsage(c->Name.Text, VariableUsage{
				.Type = VariableUsage::Const,
				.Token = i.Value,
				.File = Root.File,
				.FromConstant = c
				});
			continue;
		}

		for (auto& var : Root.Root.Variables)
		{
			if (var.first != i.Value.Text)
				continue;

			AddVariableUsage(var.first, VariableUsage{
				.Type = VariableUsage::Var,
				.Token = i.Value,
				.File = Root.File,
				.VariableElement = &Root
				});
			break;
		}
	}

	for (MarkupStructure::UIElement& Child : Target.Children)
	{
		ScanForVariableUsages(Child, Root);
	}
}

void protocol::PublishDiagnostics(std::vector<protocol::DiagnosticError> Error, Message* RespondTo)
{
	std::string TargetFile;

	if (RespondTo)
	{
		TargetFile = RespondTo->MessageJson["textDocument"];
	}

	for (auto& File : workspace::Files)
	{
		if (!TargetFile.empty() && File.first != TargetFile)
		{
			continue;
		}

		json DiagnosticsJson = json::array();

		for (auto& i : Error)
		{
			if (i.File != File.first)
				continue;

			DiagnosticsJson.push_back(json::object({
				{ "message", i.Message },
				{ "severity", i.Severity },
				{ "code", i.Type == DiagnosticError::Verify ? "kuiVerify" : "kuiParse" },
				{ "range", { { "start", {
					{ "line", i.Line },
				{ "character", i.Begin },
				} },
				{ "end", {
					{ "line", i.Line },
				{ "character", i.End },
				} } } } }));
		}

		if (RespondTo)
		{
			ResponseMessage NewMessage = ResponseMessage(*RespondTo, {
				{ "kind", "full" },
				{ "items", DiagnosticsJson },
				{ "diagnostics", DiagnosticsJson }
				});
			NewMessage.Send();
		}
		else
		{
			Message NewMessage = Message("textDocument/publishDiagnostics", {
				{ "uri", File.first },
				{ "diagnostics", DiagnosticsJson }
				}, true);
			NewMessage.Send();
		}
	}
}

void protocol::ScanFile(const std::string& Content, std::string Uri)
{
	using namespace workspace;

	std::vector<kui::MarkupParse::FileEntry> Entries;

	Files[Uri].Content = Content;

	for (auto& i : Files)
	{
		Entries.push_back(kui::MarkupParse::FileEntry{
			.Content = i.second.Content,
			.Name = i.first,
			});
	}

	LastDiagnostics.clear();

	bool Verifying = false;
	kui::parseError::ErrorCallback = [&Verifying](std::string ErrorText, std::string File, size_t ErrorLine, size_t Begin, size_t End) {
		LastDiagnostics.push_back(DiagnosticError
			{
				.Message = ErrorText,
				.File = File,
				.Type = Verifying ? DiagnosticError::Verify : DiagnosticError::Parse,
				.Line = ErrorLine,
				.Begin = Begin,
				.End = End,
			});
		};
	LastParseResult = kui::MarkupParse::ParseFiles(Entries);
	Verifying = true;
	kui::markupVerify::Verify(LastParseResult);

	VariableUsages.clear();
	for (auto& i : LastParseResult.Elements)
	{
		ScanForVariableUsages(i.Root, i);
	}

	PublishDiagnostics(LastDiagnostics);

	for (auto& i : LastParseResult.FileLines)
	{
		Files[i.first].SemanticTokens = tokens::GetDocumentTokens(i.first);
	}
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

static std::optional<std::pair<UIElement*, MarkupElement*>> GetElementAt(std::string File, size_t Line, size_t Character)
{
	for (auto& i : protocol::LastParseResult.Elements)
	{
		if (i.File != File)
		{
			continue;
		}
		std::vector RootArray = { i.Root };
		auto* Token = GetClosestElement(RootArray, Line, Character);
		if (Token)
			return std::pair{ Token, &i };
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

static std::string GetTooltipFromElement(MarkupElement& Root, UIElement& FromElement, size_t Char, size_t Line, std::string File)
{
	if (FromElement.TypeName.BeginChar <= Char
		&& FromElement.TypeName.EndChar > Char
		&& Line == FromElement.TypeName.Line)
	{
		return GetElementHoverMessage(FromElement, File);
	}
	if (!FromElement.ElementName.Empty()
		&& FromElement.ElementName.BeginChar <= Char
		&& FromElement.ElementName.EndChar > Char
		&& Line == FromElement.ElementName.Line)
	{
		return "child " + FromElement.TypeName.Text + " " + Root.FromToken.Text + "." + FromElement.ElementName.Text;
	}

	for (auto& Var : FromElement.Variables)
	{
		if (Var.second.Token.BeginChar <= Char && Var.second.Token.EndChar > Char && Line == Var.second.Token.Line)
		{
			return GetVariableHoverMessage(Var.second.Token, &Root);
		}
	}

	for (auto& Param : FromElement.ElementProperties)
	{
		if (Param.Name.BeginChar <= Char && Param.Name.EndChar > Char && Line == Param.Name.Line)
		{
			auto Info = GetPropertyInfo(Param, FromElement);
			if (Info.first.empty())
				continue;
			if (protocol::AllowMarkdownInHover)
				return "`" + Info.first + "`\n\n" + Info.second;
			return Info.first + "\n" + Info.second;
		}
	}

	for (auto& Child : FromElement.Children)
	{
		std::string Message = GetTooltipFromElement(Root, Child, Char, Line, File);
		if (!Message.empty())
		{
			return Message;
		}
	}

	return "";
}

static std::string GetHoverMessage(std::string File, size_t Char, size_t Line)
{
	using namespace protocol;
	using namespace workspace;

	for (auto& i : LastParseResult.Elements)
	{
		if (!CompareFiles(ConvertFilePath(i.File), ConvertFilePath(File)))
			continue;

		std::string HoverMessage = GetTooltipFromElement(i, i.Root, Char, Line, File);

		if (!HoverMessage.empty())
			return HoverMessage;
	}
	for (auto& Variable : VariableUsages)
	{
		for (VariableUsage& Usage : Variable.second)
		{
			if (!CompareFiles(ConvertFilePath(Usage.File), ConvertFilePath(File)))
				continue;
			if (Usage.Token.BeginChar <= Char && Usage.Token.EndChar > Char && Line == Usage.Token.Line)
			{
				if (Usage.Type == VariableUsage::Global)
					return GetGlobalHoverMessage(Usage.FromGlobal);
				if (Usage.Type == VariableUsage::Const)
					return GetConstHoverMessage(Usage.FromConstant);
				if (Usage.Type == VariableUsage::Var)
					return GetVariableHoverMessage(Variable.first, Usage.VariableElement);
				return Usage.Token.Text;
			}
		}
	}

	for (auto& Global : LastParseResult.Globals)
	{
		if (!CompareFiles(ConvertFilePath(Global.File), ConvertFilePath(File)))
			continue;

		if (Global.Name.BeginChar <= Char && Global.Name.EndChar > Char && Line == Global.Name.Line)
		{
			return GetGlobalHoverMessage(&Global);
		}
	}

	for (auto& Const : LastParseResult.Constants)
	{
		if (!CompareFiles(ConvertFilePath(Const.File), ConvertFilePath(File)))
			continue;

		if (Const.Name.BeginChar <= Char && Const.Name.EndChar > Char && Line == Const.Name.Line)
		{
			return GetConstHoverMessage(&Const);
		}
	}

	return "";
}

static json GetTokenCompletions(std::string File, kui::stringParse::StringToken Token)
{
	using namespace kui::MarkupStructure;

	json CompletionArray = json::array();

	std::optional Elem = GetElementAt(File, Token.Line, Token.BeginChar);
	std::unordered_set<std::string> AutoCompleteValues;

	auto AddKeyword = [&CompletionArray](std::string Name, std::string Detail) {
		CompletionArray.push_back({ { "label", Name },
			{ "detail", Detail },
			{ "kind", 14 } });
		};

	auto AddGlobal = [&CompletionArray](std::string Name, std::string Detail) {
		CompletionArray.push_back({ { "label", Name },
			{ "detail", Detail },
			{ "kind", 6 } });
		};

	auto AddVariable = [&CompletionArray](std::string Name, std::string Detail) {
		CompletionArray.push_back({ { "label", Name },
			{ "detail", Detail },
			{ "kind", 10 } });
		};

	auto AddConst = [&CompletionArray](std::string Name, std::string Detail) {
		CompletionArray.push_back({ { "label", Name },
			{ "detail", Detail },
			{ "kind", 21 } });
		};

	auto AddElement = [&CompletionArray](std::string Name, std::string Detail) {
		CompletionArray.push_back({ { "label", Name },
			{ "detail", Detail },
			{ "kind", 7 } });
		};

	if (Elem.has_value())
	{
		AddKeyword("child", "Child element keyword");
		AddKeyword("var", "Variable keyword");

		PropElementType ElementType = GetTypeFromString(Elem->first->TypeName);

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

			CompletionArray.push_back({
				{ "label", i.Name },
				{ "detail", Info.first },
				{ "documentation", Info.second },
				{ "kind", 6 }
				});
		}

		for (auto& i : Elem->second->Root.Variables)
		{
			AddVariable(i.first, GetVariableHoverMessage(i.first, Elem->second));
		}

		for (auto& i : protocol::LastParseResult.Constants)
		{
			AddConst(i.Name, GetConstHoverMessage(&i));
		}
		for (auto& i : protocol::LastParseResult.Globals)
		{
			AddGlobal(i.Name, GetGlobalHoverMessage(&i));
		}
		for (auto& i : protocol::LastParseResult.Elements)
		{
			AddElement(i.FromToken.Text, GetElementHoverMessage(i.Root, i.File));
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
		{ "endCharacter", From.EndChar + 1 } });

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

void protocol::HandleClientMessage(Message msg)
{
	using namespace workspace;

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
			const json& FormatJson = msg.MessageJson.at(ContentFormat);
			AllowMarkdownInHover = std::find(FormatJson.begin(), FormatJson.end(), "markdown") != FormatJson.end();
		}

		json::json_pointer SemanticTokensTypes = "/capabilities/textDocument/semanticTokens/tokenTypes"_json_pointer;
		if (msg.MessageJson.contains(SemanticTokensTypes))
		{
			const json& TokenTypes = msg.MessageJson.at(SemanticTokensTypes);
			HasVsCppLocalVariable = std::find(TokenTypes.begin(), TokenTypes.end(), "cppLocalVariable") != TokenTypes.end();
		}

		if (msg.MessageJson.contains("rootUri"))
		{
			CurrentWorkspacePath = ConvertFilePath(msg.MessageJson["rootUri"]);
			UpdateFiles();
		}

		// TODO: Read the content of the initialize method instead of just assuming basic capabilities.
		ResponseMessage Response = ResponseMessage(msg, {
			{ "capabilities", {
				{ "hoverProvider", true },
			{ "textDocumentSync", {
				{ "openClose", true },
			{ "change", 1 }
			} },
			{ "diagnosticProvider", {
				{ "interFileDiagnostics", true },
			{ "workspaceDiagnostics", false }
			} },
			{ "foldingRangeProvider", true },
			{ "semanticTokensProvider", {
				{ "full", true },
			{ "legend", tokens::GetTokenLegends() }
			} },
			{ "completionProvider", json::object() }
			// TODO: Properly handle the position encoding.
			//	{ "positionEncoding", "utf-8" }
			} } });
		Response.Send();
	}
	else if (msg.Method == "textDocument/hover")
	{
		std::string Message = GetHoverMessage(
			msg.MessageJson.at("textDocument").at("uri"),
			msg.MessageJson.at("position").at("character"),
			msg.MessageJson.at("position").at("line"));
		ResponseMessage Response = ResponseMessage(msg, {
			{ "contents", Message.empty() ? json(json::value_t::null) : json(Message) }
			});
		Response.Send();
	}
	else if (msg.Method == "textDocument/completion")
	{
		std::string Document = msg.MessageJson.at("textDocument").at("uri");

		size_t Character = msg.MessageJson.at("position").at("character");
		size_t Line = msg.MessageJson.at("position").at("line");

		std::optional Token = kui::stringParse::GetTokenAt(LastParseResult.FileLines[Document],
			Character, Line);

		ResponseMessage Response = ResponseMessage(msg, GetTokenCompletions(Document,
			kui::stringParse::StringToken("", Character, Character + 1, Line)));
		Response.Send();
	}
	else if (msg.Method == "textDocument/foldingRange")
	{
		std::string Document = msg.MessageJson.at("textDocument").at("uri");

		json ResponseArray = json::array();
		for (auto& i : LastParseResult.Elements)
		{
			if (!workspace::CompareFiles(ConvertFilePath(Document), ConvertFilePath(i.File)))
				continue;

			json Array = GetFoldingRanges(i.Root);

			for (json& Range : Array)
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

		if (!Files.contains(File.get<std::string>()))
		{
			ResponseMessage Response = ResponseMessage(msg, json(), ResponseMessage::ResponseError(LSPErrorCode::InvalidParams, "File not found: " + File));
			Response.Send();
			return;
		}
		ResponseMessage Response = ResponseMessage(msg, { { "data", Files[File].SemanticTokens } });
		Response.Send();
	}
	else if (msg.Method == "textDocument/diagnostic")
	{
		PublishDiagnostics(LastDiagnostics);
	}
	else if (msg.Method == "shutdown")
	{
		ResponseMessage Response = ResponseMessage(msg, json());
		Response.Send();
		ReceivedShutdownRequest = true;
	}
	else if (msg.Method.size() && msg.Method[0] != '$')
	{
		ResponseMessage Response = ResponseMessage(msg, json(), ResponseMessage::ResponseError(LSPErrorCode::MethodNotFound, "Unknown method."));
		std::cerr << "unhandled method: " << msg.Method << " - responding with error." << std::endl;
	}
	else
	{
		std::cerr << "unhandled method: " << msg.Method << std::endl;
	}
}

void protocol::HandleClientNotification(Message msg)
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
		using namespace workspace;
		json TextDocument = msg.MessageJson.at("textDocument");
		std::string Uri = TextDocument.at("uri");
		std::string Text = TextDocument.at("text");

		OnUriOpened(Uri);
		OpenedFiles.push_back(ConvertFilePath(Uri));
		UpdateFiles();

		ScanFile(Text, Uri);
	}
	else if (msg.Method == "textDocument/didChange")
	{
		ScanFile(msg.MessageJson.at("contentChanges").at(0).at("text"), msg.MessageJson.at("textDocument").at("uri"));
	}
	else if (msg.Method == "NotificationReceived")
	{
		return;
	}
	else
	{
		std::cerr << "unhandled notify: " << msg.Method << ": " << msg.MessageJson.dump(2) << std::endl;
	}
}
