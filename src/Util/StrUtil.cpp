#include "StrUtil.h"
#include <cstdarg>
#include <algorithm>

std::string StrUtil::Trim(std::string From)
{
	From.erase(From.find_last_not_of("\t ") + 1);
	From.erase(0, From.find_first_not_of("\t "));
	return From;
}

std::string StrUtil::Format(std::string FormatString, ...)
{
	size_t Size = FormatString.size() + 50, NewSize = Size;
	char* Buffer = nullptr;
	do
	{
		Size = NewSize;
		if (Buffer)
		{
			delete[] Buffer;
		}
		Buffer = new char[Size]();
		va_list va;
		va_start(va, FormatString);
		NewSize = vsnprintf(Buffer, Size, FormatString.c_str(), va);
		va_end(va);

	} while (NewSize > Size);


	std::string StrBuffer = Buffer;
	delete[] Buffer;
	return StrBuffer;
}

bool StrUtil::CaseInsensitiveCompare(std::string a, std::string b)
{
	return Lower(a) == Lower(b);
}

std::string StrUtil::Lower(std::string From)
{
	std::transform(From.begin(), From.end(), From.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return From;
}
