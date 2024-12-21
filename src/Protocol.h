#pragma once
#include "Message.h"
#include <vector>

namespace protocol
{
	struct DiagnosticError
	{
		std::string Message;
		std::string File;
		enum ErrorType
		{
			Parse,
			Verify,
		};
		ErrorType Type;
		size_t Line = 0, Begin = 0, End = 0;
		int32_t Severity = 1;
	};


	void Init();
	void PublishDiagnostics(std::vector<DiagnosticError> Error);
	void ScanFile(const std::string& Content, std::string Uri);
	void HandleClientMessage(Message msg);
	void HandleClientNotification(Message msg);
}