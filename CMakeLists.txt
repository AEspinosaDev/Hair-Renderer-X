cmake_minimum_required(VERSION 3.16)
 
project(HairViewer VERSION 1.0.0)
 
# -----------------------------------------------------------------------------
# 1. Standard C++ Setup
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
# compile_commands.json is useful for Clangd/LSP on Linux
if(NOT MSVC)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()
 
# -----------------------------------------------------------------------------
# 2. Dependency Discovery
# -----------------------------------------------------------------------------
 
# Find Vulkan - Standard CMake module
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. 
    Windows: Install LunarG SDK and set VULKAN_SDK env var.
    Linux: Source 'setup-env.sh' from your SDK folder.")
endif()
 
# Find Threads - Required for Linux std::thread support
find_package(Threads REQUIRED)
 
# -----------------------------------------------------------------------------
# 3. Subdirectories & External Engine
# -----------------------------------------------------------------------------
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
 
# The Vulkan-Engine subdirectory likely handles its own Vulkan linking, 
# but we link explicitly in the app target for safety.
add_subdirectory(ext/Vulkan-Engine)
 
# -----------------------------------------------------------------------------
# 4. App Target Setup
# -----------------------------------------------------------------------------
file(GLOB APP_SOURCES "src/*.cpp")
file(GLOB APP_HEADERS "src/*.h")
 
add_executable(HairViewer ${APP_SOURCES} ${APP_HEADERS})
 
# -----------------------------------------------------------------------------
# 5. Cross-Platform Linking
# -----------------------------------------------------------------------------
target_link_libraries(HairViewer PRIVATE 
    VulkanEngine          # The sub-project engine
    Vulkan::Vulkan        # Modern CMake target for Vulkan
    Threads::Threads      # Maps to -pthread on Linux, no-op on Windows
)
 
# Linux-specific: Dynamic Loading library
if(UNIX AND NOT APPLE)
    target_link_libraries(HairViewer PRIVATE dl)
endif()
 
# -----------------------------------------------------------------------------
# 6. Definitions & Compiler Flags
# -----------------------------------------------------------------------------
target_compile_definitions(HairViewer PUBLIC 
    RESOURCES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
)
 
if(MSVC)
    # Windows-specific MSVC flags
    target_compile_options(HairViewer PRIVATE /W4 /MP)
else()
    # Linux/Unix-specific GCC/Clang flags
    target_compile_options(HairViewer PRIVATE -Wall -Wextra -O3)
endif()
