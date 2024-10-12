#include <iostream>
#include "Protocol.h"
#include "Windows.h"

int main(int argc, char** argv)
{
	try
	{
		Protocol::Init();
		while (true)
		{
			auto msg = Message::ReadFromStdOut();
			Protocol::HandleClientMessage(msg);
		}
	}
	catch (std::exception& e)
	{
		MessageBoxA(NULL, e.what(), "", 0);
	}
	catch (const char* e)
	{
		MessageBoxA(NULL, e, "", 0);
	}
}