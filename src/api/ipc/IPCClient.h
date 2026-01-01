#pragma once

#include "IPCMessage.h"
#include <string>
#include <atomic>

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
 * TCP socket client for IPC
 * Used by MCP server to communicate with GUI application
 * Cross-platform: works on Windows (Winsock) and Unix (BSD sockets)
 */
class IPCClient {
public:
    /**
     * Construct client with port number
     * @param port Port to connect to (default: 9999)
     */
    explicit IPCClient(int port = 9999);
    ~IPCClient();

    // Delete copy constructor and assignment
    IPCClient(const IPCClient&) = delete;
    IPCClient& operator=(const IPCClient&) = delete;

    /**
     * Connect to the IPC server
     * @return true on success
     */
    bool Connect();

    /**
     * Disconnect from the IPC server
     */
    void Disconnect();

    /**
     * Check if connected to server
     */
    bool IsConnected() const { return clientFd_ != INVALID_SOCKET_VALUE; }

    /**
     * Send a request and wait for response
     * Blocking call with timeout
     * @param request The IPC request to send
     * @param timeoutMs Timeout in milliseconds (default 5000)
     * @return IPCResponse from server
     * @throws std::runtime_error on timeout or error
     */
    IPCResponse SendRequest(const IPCRequest& request, int timeoutMs = 5000);

    /**
     * Get the port number
     */
    int GetPort() const { return port_; }

    /**
     * Try to read port from port file
     * @return port number if found, or default port if not
     */
    static int ReadPortFromFile();

private:
    std::string ReadResponse(int timeoutMs);
    void SendAll(const std::string& data, int timeoutMs);
    
    // Platform-specific helpers
    static void CloseSocket(socket_t fd);
    static std::string GetLastErrorString();

    int port_;
    socket_t clientFd_;
    std::atomic<int> nextId_;

    static constexpr int CHUNK_SIZE = 65536;  // Read chunk size

#ifdef _WIN32
    static bool wsaInitialized_;
    static bool InitializeWinsock();
    static void CleanupWinsock();
#endif
};

} // namespace ipc
} // namespace pathview
