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

target_link_libraries(pathview-mcp PRIVATE
    pathview_ipc
    mcp
    pthread
)

# Platform-specific settings
if(APPLE)
    target_link_libraries(pathview-mcp PRIVATE
        "-framework CoreFoundation"
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(pathview-mcp PRIVATE dl)
endif()

# Install binaries
install(TARGETS pathview pathview-mcp
    RUNTIME DESTINATION bin
)

message(STATUS "MCP server binary will be built: pathview-mcp")
