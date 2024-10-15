# CMakeList.txt : CMake project for KlemmUILanguageServer, include source and define
# project specific logic here.
#
cmake_minimum_required(VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if(POLICY CMP0141)
	cmake_policy(SET CMP0141 NEW)
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("KlemmUILanguageServer")

add_subdirectory("deps/json")
add_subdirectory("deps/KlemmUI/Util/Markup/MarkupLib")

# Add source to this project's executable.
add_executable(KlemmUILanguageServer
	"src/Protocol.h"
	"src/Protocol.cpp"
	"src/main.cpp"
	"src/Message.h"
	"src/Message.cpp"
	"src/Util/StrUtil.h"
	"src/Util/StrUtil.cpp"
	"src/Workspace.h"
	"src/Workspace.cpp")

target_link_libraries(KlemmUILanguageServer PRIVATE nlohmann_json)
target_link_libraries(KlemmUILanguageServer PRIVATE KlemmUIMarkup)

if(CMAKE_VERSION VERSION_GREATER 3.12)
	set_property(TARGET KlemmUILanguageServer PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
