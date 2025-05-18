#pragma once
#include <Markup/MarkupStructure.h>

namespace preview
{
	void Init();
	void Destroy();
	void LoadParsed(kui::MarkupStructure::ParseResult* From);
}