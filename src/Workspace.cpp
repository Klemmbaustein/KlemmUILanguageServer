#include "Workspace.h"
#include <filesystem>
#include <sstream>
#include <fstream>
#include <iostream>
namespace filesystem = std::filesystem;

std::string workspace::CurrentWorkspacePath;
std::map<std::string, workspace::FileData> workspace::Files;
std::vector<std::string> workspace::OpenedFiles;

std::vector<std::string> workspace::GetAllUIFiles()
{
	if (!filesystem::exists(CurrentWorkspacePath))
		return {};

	std::vector<std::string> Found;

	try
	{
		for (auto& i : filesystem::recursive_directory_iterator(CurrentWorkspacePath,
			std::filesystem::directory_options::skip_permission_denied))
		{
			if (i.is_regular_file() && i.path().extension() == ".kui")
			{
				Found.push_back(i.path().string());
			}
		}
	}
	catch (std::filesystem::filesystem_error e)
	{
		std::cerr << e.what() << std::endl;
	}
	return Found;
}

void workspace::UpdateFiles()
{
	auto NewFiles = GetAllUIFiles();

	for (auto& i : NewFiles)
	{
		bool Found = false;
		for (auto& ExistingFile : Files)
		{
			if (CompareFiles(ExistingFile.second.Name, i))
			{
				Found = true;
				break;
			}
		}
		if (Found)
			continue;

		std::ifstream Stream = std::ifstream(i);
		std::stringstream ContentStream;
		ContentStream << Stream.rdbuf();
		Stream.close();

		Files.insert({
			i, FileData{
				.Content = ContentStream.str(),
				.Name = i,
			}
			});
	}

	for (auto& i : Files)
	{
		i.second.Opened = false;
		for (auto& Opened : OpenedFiles)
		{
			if (CompareFiles(i.second.Name, Opened))
			{
				i.second.Opened = true;
			}
		}
	}
}

bool workspace::CompareFiles(std::string a, std::string b)
{
	return filesystem::equivalent(a, b);
}

static void ReplaceAll(std::string& str, const std::string& from, const std::string& to)
{
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos)
	{
		str.replace(start_pos, from.size(), to);
		start_pos += to.size();
	}
}

std::string workspace::ConvertFilePath(std::string FilePathUri)
{
	const char* FileUri = "file:///";
	size_t UriSize = strlen(FileUri);

	if (FilePathUri.substr(0, UriSize) != FileUri)
		return FilePathUri;

	ReplaceAll(FilePathUri, "%3A", ":");
	ReplaceAll(FilePathUri, "%3a", ":");
	return FilePathUri.substr(UriSize);
}

void workspace::OnUriOpened(std::string Uri)
{
	std::string Path = ConvertFilePath(Uri);

	for (auto& i : Files)
	{
		if (CompareFiles(i.second.Name, Path))
		{
			Files.insert({
				Uri, i.second
				});
			Files.erase(i.first);
			return;
		}
	}
}

std::string workspace::GetDisplayName(std::string PathOrUri)
{
	return PathOrUri.substr(PathOrUri.find_last_of("/\\") + 1);
}
