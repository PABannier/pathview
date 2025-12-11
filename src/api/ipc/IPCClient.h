#pragma once

#include "IPCMessage.h"
#include <string>
#include <atomic>

namespace pathview {
namespace ipc {

/**
 * Unix domain socket client for IPC
 * Used by MCP server to communicate with GUI application
 */
class IPCClient {
public:
    explicit IPCClient(const std::string& socketPath);
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
    bool IsConnected() const { return clientFd_ >= 0; }

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
     * Get the socket path
     */
    std::string GetSocketPath() const { return socketPath_; }

private:
    std::string ReadResponse(int timeoutMs);

    std::string socketPath_;
    int clientFd_;
    std::atomic<int> nextId_;

    static constexpr int BUFFER_SIZE = 65536;  // 64KB buffer
};

} // namespace ipc
} // namespace pathview
