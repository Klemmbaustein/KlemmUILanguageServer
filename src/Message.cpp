#include "Message.h"
#include <iostream>
#include <string>
#include "Util/StrUtil.h"
#include <fcntl.h>
#include <io.h>

static int IdCounter = 0;

Message::Message(std::string Method, json MessageJson, bool Notification)
{
	this->MessageJson = MessageJson;
	this->MessageID = IdCounter++;
	this->Method = Method;
	this->IsRequest = !Notification;
}

Message::Message(json FromJson)
{
	if (FromJson.contains("error"))
	{
		std::cerr << FromJson.dump(2) << std::endl;
	}

	if (FromJson.contains("id"))
	{
		if (FromJson.contains("method"))
			this->Method = FromJson.at("method");
		this->MessageID = FromJson.at("id");
		if (FromJson.contains("params"))
			this->MessageJson = FromJson.at("params");
		this->IsRequest = true;
	}
	else
	{
		this->Method = FromJson.at("method");
		if (FromJson.contains("params"))
			this->MessageJson = FromJson.at("params");
		this->IsRequest = false;
	}
}

Message::Message()
{
}

Message Message::ReadFromStdOut()
{
	int ContentLength = 0;
	while (true)
	{
		std::string ReadHeader;

		char HeaderBuffer[8000];
		std::cin.getline(HeaderBuffer, sizeof(HeaderBuffer));
		ReadHeader = HeaderBuffer;
		if (ReadHeader.empty())
		{
			break;
		}

		std::pair Header = ParseHeader(ReadHeader);

		if (StrUtil::CaseInsensitiveCompare(Header.first, "Content-Length"))
		{
			ContentLength = std::stoi(Header.second);
		}
	}

	if (ContentLength == 0)
		exit(0);

	char* ContentBuffer = new char[ContentLength + 1]();
	std::cin.read(ContentBuffer, ContentLength);
	ContentBuffer[ContentLength] = 0;

	Message Out;

	try
	{
		json ContentJson = json::parse(ContentBuffer);
		if (!ContentJson.contains("jsonrpc") || ContentJson.at("jsonrpc") != "2.0")
		{
			return Message();
		}

		Out = ContentJson;
	}
	catch (json::parse_error)
	{
		return Message();
	}

	delete[] ContentBuffer;

	return Out;
}

void Message::Send()
{
	std::string MessageContent = GetMessageJson().dump();
	//std::cerr << GetMessageJson().dump(2) << std::endl;
	std::string MessageString = StrUtil::Format("Content-Length: %i\r\n\r\n", int(MessageContent.size())) + MessageContent;
	int _ = _setmode(_fileno(stdout), O_BINARY);
	std::cout.write(MessageString.c_str(), MessageString.size());
	std::cout << std::flush;
}

json Message::GetMessageJson()
{
	if (IsRequest)
		return {
			{ "jsonrpc", "2.0" },
			{ "id", MessageID },
			{ "method", Method },
			{ "params", MessageJson }
		};
	else
		return {
			{ "jsonrpc", "2.0" },
			{ "method", Method },
			{ "params", MessageJson }
		};
}

std::pair<std::string, std::string> Message::ParseHeader(std::string HeaderString)
{
	size_t Colon = HeaderString.find_first_of(':');
	if (Colon == std::string::npos)
		return {};

	std::string Name = HeaderString.substr(0, Colon);
	std::string Content = HeaderString.substr(Colon + 1);

	return { StrUtil::Trim(Name), StrUtil::Trim(Content) };
}

ResponseMessage::ResponseMessage(const Message& From, json Result, std::optional<ResponseError> Error)
{
	this->MessageID = From.MessageID;
	this->MessageJson = Result;
	this->Error = Error;
}

json ResponseMessage::GetMessageJson()
{
	if (Error.has_value())
		return {
			{ "jsonrpc", "2.0" },
			{ "id", MessageID },
			{ "error", Error.value().ToJson() },
		};
	else
		return {
			{ "jsonrpc", "2.0" },
			{ "id", MessageID },
			{ "result", MessageJson },
		};
}

json ResponseMessage::ResponseError::ToJson()
{
	return {
		{ "code", int(1) },
		{ "message", this->Message },
		{ "data", Data }
	};
}
