#include "Workspace.h"
#include <filesystem>
namespace filesystem = std::filesystem;

std::string workspace::CurrentWorkspacePath;

std::vector<std::string> workspace::GetAllUIFiles()
{
	
	if (!filesystem::exists(CurrentWorkspacePath))
		return {};

	std::vector<std::string> Found;

	for (auto& i : filesystem::directory_iterator(CurrentWorkspacePath))
	{
		if (i.is_regular_file() && i.path().extension() == "kui")
		{
			Found.push_back(i.path().string());
		}
	}
	return Found;
}

bool workspace::CompareFiles(std::string a, std::string b)
{
	return filesystem::equivalent(a, b);
}
