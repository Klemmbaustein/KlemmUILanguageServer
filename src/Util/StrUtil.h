#pragma once
#include <string>

namespace StrUtil
{
	std::string Trim(std::string From);

	std::string Format(std::string FormatString, ...);

	bool CaseInsensitiveCompare(std::string a, std::string b);

	std::string Lower(std::string From);
}