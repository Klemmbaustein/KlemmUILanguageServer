# CMakeList.txt : CMake project for KlemmUILanguageServer, include source and define
# project specific logic here.
#
cmake_minimum_required(VERSION 3.15)

# Enable Hot Reload for MSVC compilers if supported.
if(POLICY CMP0141)
	cmake_policy(SET CMP0141 NEW)
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("KlemmUILanguageServer")
set(KLEMMUI_INCLUDE_EXAMPLES OFF)
set(KLEMMUI_DYNAMIC_MARKUP ON)
add_subdirectory("deps/json")
add_subdirectory("deps/KlemmUI")

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
	"src/Workspace.cpp"
	"src/Preview/PreviewWindow.h"
	"src/Preview/PreviewWindow.cpp")

set_property(TARGET KlemmUILanguageServer PROPERTY CXX_STANDARD 20)

target_link_libraries(KlemmUILanguageServer PRIVATE nlohmann_json)
target_link_libraries(KlemmUILanguageServer PRIVATE KlemmUIMarkup)
target_link_libraries(KlemmUILanguageServer PRIVATE KlemmUI)
target_link_libraries(KlemmUILanguageServer PRIVATE KuiDynamicMarkup)

klemmui_markup(KlemmUILanguageServer "ui/")
klemmui_resources(KlemmUILanguageServer "res/")

if(MSVC)
  add_definitions(/MP)
endif()


# TODO: Add tests and install targets if needed.
