cmake_minimum_required(VERSION 3.15)
project(ollamacode VERSION 2.0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find required packages
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# Optional: readline for better CLI
option(USE_READLINE "Use readline library for better CLI" ON)

if(USE_READLINE)
    # On macOS, Homebrew installs readline as keg-only, so we need to set the path
    if(APPLE)
        set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/readline/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()

    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(READLINE readline)
    endif()
endif()

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${CURL_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
)

# Source files
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/ollama_client.cpp
    src/tool_parser.cpp
    src/tool_executor.cpp
    src/cli.cpp
    src/utils.cpp
    src/session_manager.cpp
    src/command_menu.cpp
    src/mcp_client.cpp
    src/agent.cpp
    src/task_suggester.cpp
)

# Header files
set(HEADERS
    include/config.h
    include/ollama_client.h
    include/tool_parser.h
    include/tool_executor.h
    include/cli.h
    include/utils.h
    include/session_manager.h
    include/command_menu.h
    include/mcp_client.h
    include/json.hpp
    include/agent.h
    include/task_suggester.h
)

# Main executable
add_executable(ollamacode ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(ollamacode
    ${CURL_LIBRARIES}
    ${SQLite3_LIBRARIES}
    Threads::Threads
)

# Link readline if available
if(READLINE_FOUND)
    target_include_directories(ollamacode PRIVATE ${READLINE_INCLUDE_DIRS})
    # On macOS, use the full path to Homebrew's readline to avoid linking against system libedit
    if(APPLE AND EXISTS "/opt/homebrew/opt/readline/lib/libreadline.dylib")
        target_link_libraries(ollamacode "/opt/homebrew/opt/readline/lib/libreadline.dylib")
    else()
        target_link_libraries(ollamacode ${READLINE_LIBRARIES})
    endif()
    target_compile_definitions(ollamacode PRIVATE HAVE_READLINE)
endif()

# Installation
install(TARGETS ollamacode DESTINATION bin)

# Create config directory
install(DIRECTORY DESTINATION $ENV{HOME}/.config/ollamacode)

# Tests (optional)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print build info
message(STATUS "ollamaCode ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CURL found: ${CURL_FOUND}")
message(STATUS "SQLite3 found: ${SQLite3_FOUND}")
message(STATUS "Readline found: ${READLINE_FOUND}")
