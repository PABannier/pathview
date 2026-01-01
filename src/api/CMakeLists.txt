# PathView API - MCP Server and IPC Infrastructure

cmake_minimum_required(VERSION 3.20)

# ============================================================================
# IPC Library (shared between GUI and MCP server)
# ============================================================================

add_library(pathview_ipc STATIC
    ipc/IPCMessage.cpp
    ipc/IPCServer.cpp
    ipc/IPCClient.cpp
)

target_include_directories(pathview_ipc PUBLIC
    ${CMAKE_SOURCE_DIR}/src/api
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/external/cpp-mcp/common  # For json.hpp
)

# Platform-specific IPC library dependencies
if(WIN32)
    # Windows: Link Winsock2 for TCP sockets
    target_link_libraries(pathview_ipc PUBLIC ws2_32)
endif()

# MSVC-specific compile options for IPC library
if(MSVC)
    target_compile_options(pathview_ipc PRIVATE
        /W4 /WX- /utf-8 /bigobj /MP
    )
    target_compile_definitions(pathview_ipc PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# Link to main application
target_link_libraries(pathview PRIVATE pathview_ipc)

# ============================================================================
# MCP Server Binary
# ============================================================================

# Check if cpp-mcp is available
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/cpp-mcp/CMakeLists.txt")
    message(WARNING "cpp-mcp not found. MCP server will not be built.")
    message(WARNING "Run: git submodule update --init --recursive")
    return()
endif()

# Add cpp-mcp subdirectory
add_subdirectory(${CMAKE_SOURCE_DIR}/external/cpp-mcp ${CMAKE_BINARY_DIR}/cpp-mcp)

# Create pathview-mcp executable
add_executable(pathview-mcp
    mcp/main.cpp
    mcp/MCPServer.cpp
    mcp/MCPTools.cpp
    http/HTTPServer.cpp
    http/SnapshotManager.cpp
)

target_include_directories(pathview-mcp PRIVATE
    ${CMAKE_SOURCE_DIR}/src/api
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/external/cpp-mcp/include
    ${CMAKE_SOURCE_DIR}/external/cpp-mcp/common
)

# Core link libraries (cross-platform)
target_link_libraries(pathview-mcp PRIVATE
    pathview_ipc
    mcp
    Threads::Threads
)

# MSVC-specific compile options for MCP server
if(MSVC)
    target_compile_options(pathview-mcp PRIVATE
        /W4 /WX- /utf-8 /bigobj /MP
    )
    target_compile_definitions(pathview-mcp PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# Place pathview-mcp at the root of the build folder (same level as pathview)
set_target_properties(pathview-mcp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Platform-specific settings
if(WIN32)
    # Windows: Winsock is linked via pathview_ipc
    # No additional libraries needed
elseif(APPLE)
    target_link_libraries(pathview-mcp PRIVATE
        "-framework CoreFoundation"
    )
elseif(UNIX)
    target_link_libraries(pathview-mcp PRIVATE dl)
endif()

# Install binaries
install(TARGETS pathview pathview-mcp
    RUNTIME DESTINATION bin
)

message(STATUS "MCP server binary will be built: pathview-mcp")
