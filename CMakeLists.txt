cmake_minimum_required(VERSION 3.21)
project(ifn_teamspeak_plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
FetchContent_Declare(ts3sdk
    URL https://github.com/teamspeak/ts3client-pluginsdk/archive/4aa90a53aa150cbf81e13bc97e68c0431b26499f.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(ts3sdk)
if(NOT ts3sdk_POPULATED)
    FetchContent_Populate(ts3sdk)
endif()

add_library(ts3sdk INTERFACE)
target_include_directories(ts3sdk INTERFACE ${ts3sdk_SOURCE_DIR}/include ${ts3sdk_SOURCE_DIR}/src)

include(cmake/Ts3Plugin.cmake)

set(TS3_PLUGINS group-finder staff-tools staff-board gm-tools sticky-tools)
foreach(plugin IN LISTS TS3_PLUGINS)
    add_subdirectory(plugins/${plugin})
endforeach()

add_custom_target(package)
foreach(plugin IN LISTS TS3_PLUGINS)
    add_dependencies(package package-${plugin})
endforeach()
