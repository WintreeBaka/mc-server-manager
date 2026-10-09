cmake_minimum_required(VERSION 3.19)

project(McServerManager
    VERSION 1.1.1
    DESCRIPTION "Minecraft server manager (Docker based) with Qt frontend and CLI backend"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# UI strings are UTF-8; MSVC needs an explicit flag (MinGW defaults to UTF-8).
if(MSVC)
    add_compile_options(/utf-8 /permissive-)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
foreach(cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin)
endforeach()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qml provides QJSEngine: the plugin host runs plugin frontend scripts in it.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Qml)

qt_standard_project_setup()

add_subdirectory(backend)
add_subdirectory(frontend)

message(STATUS "Qt version      : ${Qt6_VERSION}")
message(STATUS "Build type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "Output binaries : ${CMAKE_BINARY_DIR}/bin")
