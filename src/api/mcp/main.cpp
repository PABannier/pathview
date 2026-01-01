/**
 * PathView MCP Server
 *
 * Model Context Protocol server for remote control of PathView GUI
 *
 * Architecture:
 * - Connects to PathView GUI via TCP socket on localhost (IPC)
 * - Exposes MCP tools via HTTP+SSE transport (port 9000)
 * - Serves viewport snapshots via HTTP (port 8080)
 */

#include "MCPServer.h"
#include "../ipc/IPCClient.h"
#include "../http/HTTPServer.h"
#include "../http/SnapshotManager.h"
#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);
pathview::mcp::MCPServer* g_mcpServer = nullptr;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived shutdown signal, stopping..." << std::endl;
        g_running = false;
        // Actually stop the blocking MCP server
        if (g_mcpServer) {
            g_mcpServer->Stop();
        }
    }
}

void print_usage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n"
              << "\nOptions:\n"
              << "  --ipc-port PORT    IPC port to connect to GUI (default: auto-detect from port file)\n"
              << "  --http-port PORT   HTTP server port (default: 8080)\n"
              << "  --mcp-port PORT    MCP server port (default: 9000)\n"
              << "  --help             Show this help message\n"
              << "\nExample:\n"
              << "  " << progName << " --ipc-port 9999 --http-port 8080\n"
              << std::endl;
}

int main(int argc, char** argv) {
    // Parse command line arguments
    int ipcPort = -1;  // -1 means auto-detect from port file
    int httpPort = 8080;
    int mcpPort = 9000;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--ipc-port" && i + 1 < argc) {
            ipcPort = std::atoi(argv[++i]);
        } else if (arg == "--http-port" && i + 1 < argc) {
            httpPort = std::atoi(argv[++i]);
        } else if (arg == "--mcp-port" && i + 1 < argc) {
            mcpPort = std::atoi(argv[++i]);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Auto-detect port if not specified
    if (ipcPort < 0) {
        ipcPort = pathview::ipc::IPCClient::ReadPortFromFile();
        std::cout << "Auto-detected IPC port: " << ipcPort << std::endl;
    }

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "PathView MCP Server v0.1.0\n" << std::endl;

    // 1. Connect to GUI via IPC
    std::cout << "Connecting to PathView GUI at localhost:" << ipcPort << "..." << std::endl;
    pathview::ipc::IPCClient ipcClient(ipcPort);

    if (!ipcClient.Connect()) {
        std::cerr << "Failed to connect to GUI. Please ensure PathView is running." << std::endl;
        std::cerr << "Start PathView with: ./build/pathview" << std::endl;
        return 1;
    }

    std::cout << "Connected to GUI\n" << std::endl;

    // 2. Create snapshot manager
    pathview::http::SnapshotManager snapshotManager(50);  // Max 50 snapshots

    // 3. Create HTTP server
    pathview::http::HTTPServer httpServer(httpPort, &snapshotManager);

    // 4. Start HTTP server in background thread
    std::cout << "Starting HTTP server on http://127.0.0.1:" << httpPort << "..." << std::endl;
    std::thread httpThread([&httpServer]() {
        httpServer.Start();
    });

    // Give HTTP server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (!httpServer.IsRunning()) {
        std::cerr << "Failed to start HTTP server" << std::endl;
        ipcClient.Disconnect();
        return 1;
    }

    std::cout << "HTTP server running\n" << std::endl;

    // 5. Create and configure MCP server
    std::cout << "Initializing MCP server..." << std::endl;
    pathview::mcp::MCPServer mcpServer(&ipcClient, &snapshotManager, &httpServer, mcpPort);
    g_mcpServer = &mcpServer;
    mcpServer.RegisterTools();

    std::cout << "MCP server initialized\n" << std::endl;

    // 6. Print status
    std::cout << "===========================================================\n"
              << " PathView MCP Server Ready!\n"
              << "===========================================================\n"
              << "\n"
              << "  MCP Server:  http://127.0.0.1:" << mcpPort << "\n"
              << "  SSE Endpoint: http://127.0.0.1:" << mcpPort << "/sse\n"
              << "  HTTP Server: http://127.0.0.1:" << httpPort << "\n"
              << "  GUI IPC:     localhost:" << ipcPort << "\n"
              << "\n"
              << "Available Tools:\n"
              << "  - load_slide, get_slide_info\n"
              << "  - pan, center_on, zoom, zoom_at_point, reset_view\n"
              << "  - capture_snapshot\n"
              << "  - load_polygons, query_polygons, set_polygon_visibility\n"
              << "\n"
              << "Press Ctrl+C to stop\n"
              << "===========================================================\n"
              << std::endl;

    // 7. Run MCP server (blocking)
    try {
        mcpServer.Run();
    } catch (const std::exception& e) {
        std::cerr << "MCP server error: " << e.what() << std::endl;
    }

    // 8. Cleanup
    std::cout << "\nShutting down..." << std::endl;

    g_mcpServer = nullptr;  // Clear before stopping to avoid double-stop
    mcpServer.Stop();
    httpServer.Stop();

    if (httpThread.joinable()) {
        httpThread.join();
    }

    ipcClient.Disconnect();

    std::cout << "PathView MCP Server stopped" << std::endl;
    return 0;
}
