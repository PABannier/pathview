cmake_minimum_required(VERSION 3.20)
project(pathview CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add custom CMake modules path (for FindOpenSlide, FindSDL2_image, and FindNFD)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Resource directory for fonts and icons
set(RESOURCES_DIR "${CMAKE_SOURCE_DIR}/resources")

# Find threading library (cross-platform)
find_package(Threads REQUIRED)

# Find SDL2 via vcpkg
find_package(SDL2 CONFIG REQUIRED)

# Find SDL2_image using custom module (vcpkg's CONFIG mode can be unreliable)
find_package(SDL2_image REQUIRED)

# Find NFD using custom module (vcpkg's CONFIG mode can be unreliable)
find_package(NFD REQUIRED)

# Find Protobuf
find_package(Protobuf REQUIRED)
find_package(OpenSlide REQUIRED)

# Find simdjson (for JSON polygon loading)
find_package(simdjson CONFIG REQUIRED)

# Find PNG
find_package(PNG REQUIRED)

# Find UUID library (platform-specific)
if(WIN32)
    # Windows: No external UUID library needed - using built-in implementation
    set(UUID_LIBRARY "")
elseif(APPLE)
    # macOS has UUID in System framework
    find_library(UUID_LIBRARY System REQUIRED)
else()
    # Linux uses libuuid
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UUID REQUIRED uuid)
endif()

# Find Abseil (required by modern protobuf for logging)
find_package(absl CONFIG REQUIRED)

# ImGui sources (compiled directly, not via vcpkg)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)

# Check if ImGui exists
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR
        "ImGui not found at ${IMGUI_DIR}. "
        "Please run: mkdir -p external/imgui && cd external/imgui && "
        "curl -L https://github.com/ocornut/imgui/archive/refs/tags/v1.90.0.tar.gz | tar xz --strip-components=1")
endif()

# ImGui core and backend files
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

# Application sources
set(APP_SOURCES
    src/core/main.cpp
    src/core/Application.cpp
    src/core/ActionCard.cpp
    src/core/UIStyle.cpp
    src/core/SlideLoader.cpp
    src/core/Viewport.cpp
    src/core/Animation.cpp
    src/core/TileCache.cpp
    src/core/TileLoadThreadPool.cpp
    src/core/SlideRenderer.cpp
    src/core/TextureManager.cpp
    src/core/Minimap.cpp
    src/core/PolygonOverlay.cpp
    src/core/PolygonLoader.cpp
    src/core/PolygonIndex.cpp
    src/core/PolygonTriangulator.cpp
    src/core/AnnotationManager.cpp
    src/core/NavigationLock.cpp
    src/core/PNGEncoder.cpp
    src/core/ScreenshotBuffer.cpp
    src/loaders/ProtobufPolygonLoader.cpp
    src/loaders/JSONPolygonLoader.cpp
    protobuf/cell_polygons.pb.cc
)

# Create executable
add_executable(pathview ${APP_SOURCES} ${IMGUI_SOURCES})

# Include directories
target_include_directories(pathview PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/loaders
    ${CMAKE_SOURCE_DIR}/protobuf
    ${CMAKE_SOURCE_DIR}/resources/fonts
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${OPENSLIDE_INCLUDE_DIRS}
    ${Protobuf_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
)

# Add compile definitions for resource paths
target_compile_definitions(pathview PRIVATE
    RESOURCES_DIR="${RESOURCES_DIR}"
)

# MSVC-specific compile options and definitions
if(MSVC)
    target_compile_options(pathview PRIVATE
        /W4         # Warning level 4
        /WX-        # Warnings are not errors (for now)
        /utf-8      # UTF-8 source and execution character set
        /bigobj     # Increase object file section limit
        /MP         # Multi-processor compilation
    )
    target_compile_definitions(pathview PRIVATE
        _CRT_SECURE_NO_WARNINGS     # Disable secure function warnings
        NOMINMAX                     # Don't define min/max macros
        WIN32_LEAN_AND_MEAN         # Exclude rarely-used Windows headers
    )
endif()

# Link libraries
target_link_libraries(pathview PRIVATE
    SDL2::SDL2
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    ${OPENSLIDE_LIBRARIES}
    nfd::nfd
    ${Protobuf_LIBRARIES}
    absl::log
    absl::log_internal_check_op
    absl::log_internal_message
    absl::hash
    PNG::PNG
    simdjson::simdjson
    Threads::Threads
)

# UUID library (only needed on macOS and Linux)
if(UUID_LIBRARY)
    target_link_libraries(pathview PRIVATE ${UUID_LIBRARY})
endif()

# Platform-specific settings
if(WIN32)
    # Windows-specific: Link Winsock2 for TCP sockets
    target_link_libraries(pathview PRIVATE ws2_32)
    
    # Add SDL2main for Windows entry point
    target_link_libraries(pathview PRIVATE SDL2::SDL2main)
elseif(APPLE)
    target_link_libraries(pathview PRIVATE
        "-framework CoreFoundation"
        "-framework ApplicationServices"
    )

    # Set RPATH for finding shared libraries
    set_target_properties(pathview PROPERTIES
        BUILD_RPATH "@executable_path"
        INSTALL_RPATH "@executable_path"
    )
elseif(UNIX)
    # Linux-specific settings
    target_link_libraries(pathview PRIVATE dl)

    set_target_properties(pathview PROPERTIES
        BUILD_RPATH "\$ORIGIN"
        INSTALL_RPATH "\$ORIGIN"
    )
endif()

# Compiler warnings for GCC/Clang
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pathview PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3>
    )
endif()

# Add API subdirectory (MCP server + IPC infrastructure)
option(BUILD_MCP_SERVER "Build MCP server binary" ON)

if(BUILD_MCP_SERVER)
    add_subdirectory(src/api)
endif()

# Enable testing
option(BUILD_TESTING "Build test suite" ON)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

# Print configuration summary
message(STATUS "=== PathView Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "SDL2 found: ${SDL2_FOUND}")
message(STATUS "OpenSlide found: ${OPENSLIDE_FOUND}")
message(STATUS "OpenSlide include: ${OPENSLIDE_INCLUDE_DIRS}")
message(STATUS "OpenSlide library: ${OPENSLIDE_LIBRARIES}")
message(STATUS "ImGui directory: ${IMGUI_DIR}")
message(STATUS "Build MCP server: ${BUILD_MCP_SERVER}")
message(STATUS "==============================")
