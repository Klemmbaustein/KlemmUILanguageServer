#include <iostream>
#include "Protocol.h"

int main(int argc, char** argv)
{
	protocol::Init();
	while (true)
	{
		auto msg = Message::ReadFromStdOut();
		protocol::HandleClientMessage(msg);
	}
}