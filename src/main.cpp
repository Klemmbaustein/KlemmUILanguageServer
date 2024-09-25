#include <iostream>
#include "Protocol.h"

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
		std::cerr << e.what();
	}
}