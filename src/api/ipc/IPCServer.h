#pragma once

#include "IPCMessage.h"
#include <functional>
#include <vector>
#include <string>

// Cross-platform socket includes
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    using socket_t = int;
    #define INVALID_SOCKET_VALUE (-1)
#endif

namespace pathview {
namespace ipc {

/**
 * Command handler function type
 * Takes method name and params, returns result JSON
 */
using CommandHandler = std::function<json(const std::string& method, const json& params)>;

/**
 * Disconnect callback function type
 * Called when a client disconnects
 */
using DisconnectCallback = std::function<void(socket_t clientFd)>;

/**
 * TCP socket server for IPC
 * Non-blocking, integrates with GUI event loop
 * Cross-platform: works on Windows (Winsock) and Unix (BSD sockets)
 */
class IPCServer {
public:
    explicit IPCServer(CommandHandler handler);
    ~IPCServer();

    // Delete copy constructor and assignment
    IPCServer(const IPCServer&) = delete;
    IPCServer& operator=(const IPCServer&) = delete;

    /**
     * Start the IPC server
     * Binds to localhost on a specific port
     * Writes port number to a file for discovery
     * @return true on success
     */
    bool Start();

    /**
     * Stop the IPC server
     * Closes all connections and removes port file
     */
    void Stop();

    /**
     * Process pending IPC messages (non-blocking)
     * Should be called from the GUI event loop
     * @param timeoutMs Maximum time to spend processing (milliseconds)
     */
    void ProcessMessages(int timeoutMs);

    /**
     * Check if server is running
     */
    bool IsRunning() const { return serverFd_ != INVALID_SOCKET_VALUE; }

    /**
     * Get the port number the server is listening on
     */
    int GetPort() const { return port_; }

    /**
     * Get the port file path (for client discovery)
     */
    std::string GetPortFilePath() const;

    /**
     * Set callback for client disconnections
     */
    void SetDisconnectCallback(DisconnectCallback callback) {
        disconnectCallback_ = callback;
    }

    /**
     * Get client FD for current request (called during HandleRequest)
     */
    socket_t GetCurrentClientFd() const { return currentClientFd_; }

    // Default port for IPC communication
    static constexpr int DEFAULT_PORT = 9999;

private:
    void AcceptConnections();
    void HandleClient(socket_t clientFd);
    void RemoveClient(socket_t clientFd);
    IPCResponse HandleRequest(const IPCRequest& request);
    
    // Platform-specific helpers
    static bool SetNonBlocking(socket_t fd);
    static void CloseSocket(socket_t fd);
    static std::string GetLastErrorString();
    void WritePortFile();
    void RemovePortFile();

    CommandHandler handler_;
    socket_t serverFd_;
    int port_;
    std::vector<socket_t> clients_;
    DisconnectCallback disconnectCallback_;
    socket_t currentClientFd_;  // Set during HandleRequest

    static constexpr int MAX_CLIENTS = 5;
    static constexpr int BUFFER_SIZE = 65536;  // 64KB buffer for messages

#ifdef _WIN32
    static bool wsaInitialized_;
    static bool InitializeWinsock();
    static void CleanupWinsock();
#endif
};

} // namespace ipc
} // namespace pathview
