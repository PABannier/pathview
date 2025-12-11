#pragma once

#include "IPCMessage.h"
#include <functional>
#include <vector>
#include <string>

namespace pathview {
namespace ipc {

/**
 * Command handler function type
 * Takes method name and params, returns result JSON
 */
using CommandHandler = std::function<json(const std::string& method, const json& params)>;

/**
 * Unix domain socket server for IPC
 * Non-blocking, integrates with GUI event loop
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
     * Creates Unix domain socket at /tmp/pathview-{pid}.sock
     * Creates symlink /tmp/pathview-latest.sock -> socket
     * @return true on success
     */
    bool Start();

    /**
     * Stop the IPC server
     * Closes all connections and removes socket file
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
    bool IsRunning() const { return serverFd_ >= 0; }

    /**
     * Get the socket path
     */
    std::string GetSocketPath() const { return socketPath_; }

private:
    void AcceptConnections();
    void HandleClient(int clientFd);
    void RemoveClient(int clientFd);
    IPCResponse HandleRequest(const IPCRequest& request);

    CommandHandler handler_;
    int serverFd_;
    std::string socketPath_;
    std::vector<int> clients_;

    static constexpr int MAX_CLIENTS = 5;
    static constexpr int BUFFER_SIZE = 65536;  // 64KB buffer for messages
};

} // namespace ipc
} // namespace pathview
