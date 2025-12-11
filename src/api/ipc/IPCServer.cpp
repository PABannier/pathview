#include "IPCServer.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace pathview {
namespace ipc {

IPCServer::IPCServer(CommandHandler handler)
    : handler_(std::move(handler))
    , serverFd_(-1)
{
}

IPCServer::~IPCServer() {
    Stop();
}

bool IPCServer::Start() {
    // Create Unix domain socket
    serverFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    // Set non-blocking
    int flags = fcntl(serverFd_, F_GETFL, 0);
    fcntl(serverFd_, F_SETFL, flags | O_NONBLOCK);

    // Create socket path: /tmp/pathview-{pid}.sock
    socketPath_ = "/tmp/pathview-" + std::to_string(getpid()) + ".sock";

    // Remove stale socket if exists
    unlink(socketPath_.c_str());

    // Bind socket
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
        close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    // Listen for connections
    if (listen(serverFd_, MAX_CLIENTS) < 0) {
        std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
        close(serverFd_);
        serverFd_ = -1;
        unlink(socketPath_.c_str());
        return false;
    }

    // Create symlink /tmp/pathview-latest.sock -> actual socket
    std::string latestPath = "/tmp/pathview-latest.sock";
    unlink(latestPath.c_str());
    if (symlink(socketPath_.c_str(), latestPath.c_str()) != 0) {
        std::cerr << "Warning: Failed to create symlink: " << strerror(errno) << std::endl;
        // Non-fatal, continue
    }

    std::cout << "IPC server listening on " << socketPath_ << std::endl;
    return true;
}

void IPCServer::Stop() {
    if (serverFd_ < 0) {
        return;
    }

    // Close all client connections
    for (int fd : clients_) {
        close(fd);
    }
    clients_.clear();

    // Close server socket
    close(serverFd_);
    serverFd_ = -1;

    // Remove socket file and symlink
    unlink(socketPath_.c_str());
    unlink("/tmp/pathview-latest.sock");

    std::cout << "IPC server stopped" << std::endl;
}

void IPCServer::ProcessMessages(int timeoutMs) {
    if (serverFd_ < 0) {
        return;
    }

    // Prepare fd_set for select()
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(serverFd_, &readfds);

    int maxFd = serverFd_;
    for (int fd : clients_) {
        FD_SET(fd, &readfds);
        maxFd = std::max(maxFd, fd);
    }

    // Set timeout
    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

    int activity = select(maxFd + 1, &readfds, nullptr, nullptr, &timeout);

    if (activity < 0) {
        std::cerr << "Select error: " << strerror(errno) << std::endl;
        return;
    }

    if (activity == 0) {
        // Timeout, no activity
        return;
    }

    // Check for new connections
    if (FD_ISSET(serverFd_, &readfds)) {
        AcceptConnections();
    }

    // Check for client messages
    std::vector<int> clientsCopy = clients_;  // Copy to avoid iterator invalidation
    for (int fd : clientsCopy) {
        if (FD_ISSET(fd, &readfds)) {
            HandleClient(fd);
        }
    }
}

void IPCServer::AcceptConnections() {
    sockaddr_un clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);

    int clientFd = accept(serverFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (clientFd < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
        }
        return;
    }

    if (clients_.size() >= MAX_CLIENTS) {
        std::cerr << "Max clients reached, rejecting connection" << std::endl;
        close(clientFd);
        return;
    }

    // Set client socket non-blocking
    int flags = fcntl(clientFd, F_GETFL, 0);
    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

    clients_.push_back(clientFd);
    std::cout << "New IPC client connected (fd=" << clientFd << ")" << std::endl;
}

void IPCServer::HandleClient(int clientFd) {
    char buffer[BUFFER_SIZE];
    ssize_t n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);

    if (n <= 0) {
        if (n == 0 || (errno != EWOULDBLOCK && errno != EAGAIN)) {
            // Client disconnected or error
            std::cout << "IPC client disconnected (fd=" << clientFd << ")" << std::endl;
            RemoveClient(clientFd);
        }
        return;
    }

    buffer[n] = '\0';

    try {
        // Parse JSON-RPC request
        json requestJson = json::parse(buffer);
        IPCRequest request = IPCRequest::FromJson(requestJson);

        // Handle request
        IPCResponse response = HandleRequest(request);

        // Send response
        std::string responseStr = response.ToJson().dump();
        responseStr += "\n";  // Add newline delimiter

        ssize_t sent = send(clientFd, responseStr.c_str(), responseStr.size(), 0);
        if (sent < 0) {
            std::cerr << "Failed to send response: " << strerror(errno) << std::endl;
            RemoveClient(clientFd);
        }
    } catch (const json::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;

        // Send error response
        IPCResponse errorResponse;
        errorResponse.id = 0;  // Unknown ID
        errorResponse.error = IPCError{
            ErrorCodes::ParseError,
            std::string("Parse error: ") + e.what()
        };

        std::string responseStr = errorResponse.ToJson().dump() + "\n";
        send(clientFd, responseStr.c_str(), responseStr.size(), 0);
    } catch (const std::exception& e) {
        std::cerr << "IPC error: " << e.what() << std::endl;
    }
}

void IPCServer::RemoveClient(int clientFd) {
    close(clientFd);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), clientFd), clients_.end());
}

IPCResponse IPCServer::HandleRequest(const IPCRequest& request) {
    IPCResponse response;
    response.id = request.id;

    try {
        // Call the application's command handler
        json result = handler_(request.method, request.params);
        response.result = result;
    } catch (const std::exception& e) {
        // Convert exception to error response
        response.error = IPCError{
            ErrorCodes::InternalError,
            e.what()
        };
    }

    return response;
}

} // namespace ipc
} // namespace pathview
