#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace workspace
{
	extern std::string CurrentWorkspacePath;

	struct FileData
	{
		bool Opened = false;
		nlohmann::json SemanticTokens = nlohmann::json::array();
		std::string Content;
		std::string Name;
	};

	std::vector<std::string> GetAllUIFiles();
	void UpdateFiles();
	// First: uri, second: file info
	extern std::map<std::string, FileData> Files;
	// Contains paths to all opened files
	extern std::vector<std::string> OpenedFiles;

	std::string ConvertFilePath(std::string FilePathUri);

	void OnUriOpened(std::string Uri);

	std::string GetDisplayName(std::string PathOrUri);

	bool CompareFiles(std::string a, std::string b);
}