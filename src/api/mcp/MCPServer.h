#pragma once

#include "mcp_server.h"  // From cpp-mcp
#include <memory>

namespace pathview {

namespace ipc {
    class IPCClient;
}

namespace http {
    class SnapshotManager;
    class HTTPServer;
}

namespace mcp {

/**
 * MCP server wrapper
 * Manages the cpp-mcp server and registers PathView tools
 */
class MCPServer {
public:
    MCPServer(ipc::IPCClient* ipcClient,
              http::SnapshotManager* snapshotManager,
              http::HTTPServer* httpServer);
    ~MCPServer();

    // Delete copy constructor and assignment
    MCPServer(const MCPServer&) = delete;
    MCPServer& operator=(const MCPServer&) = delete;

    /**
     * Register all PathView tools with the MCP server
     */
    void RegisterTools();

    /**
     * Run the MCP server (blocking)
     * Uses STDIO transport for communication with MCP clients
     */
    void Run();

    /**
     * Stop the MCP server
     */
    void Stop();

private:
    std::unique_ptr<::mcp::server> server_;
    ipc::IPCClient* ipcClient_;
    http::SnapshotManager* snapshotManager_;
    http::HTTPServer* httpServer_;
};

} // namespace mcp
} // namespace pathview
