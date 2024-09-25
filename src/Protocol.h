#pragma once
#include "Message.h"
#include <vector>

namespace Protocol
{
	struct DiagnosticError
	{
		std::string Message;
		size_t Line = 0, Begin = 0, End = 0;
		int32_t Severity = 1;
	};


	std::string ConvertFilePath(std::string FilePathUri);
	void Init();
	void PublishDiagnostics(std::vector<DiagnosticError> Error, std::string File);
	void ScanFile(std::string Content, std::string Uri);
	void HandleClientMessage(Message msg);
	void HandleClientNotification(Message msg);
}