#include <string>
#include <vector>

namespace workspace
{
	extern std::string CurrentWorkspacePath;

	std::vector<std::string> GetAllUIFiles();

	bool CompareFiles(std::string a, std::string b);
}