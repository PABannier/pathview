#include "IPCClient.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <chrono>

namespace pathview {
namespace ipc {

IPCClient::IPCClient(const std::string& socketPath)
    : socketPath_(socketPath)
    , clientFd_(-1)
    , nextId_(1)
{
}

IPCClient::~IPCClient() {
    Disconnect();
}

bool IPCClient::Connect() {
    if (clientFd_ >= 0) {
        return true;  // Already connected
    }

    // Create Unix domain socket
    clientFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (clientFd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    // Prepare address
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    // Connect to server
    if (connect(clientFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to " << socketPath_ << ": " << strerror(errno) << std::endl;
        close(clientFd_);
        clientFd_ = -1;
        return false;
    }

    std::cout << "Connected to IPC server at " << socketPath_ << std::endl;
    return true;
}

void IPCClient::Disconnect() {
    if (clientFd_ >= 0) {
        close(clientFd_);
        clientFd_ = -1;
        std::cout << "Disconnected from IPC server" << std::endl;
    }
}

IPCResponse IPCClient::SendRequest(const IPCRequest& request, int timeoutMs) {
    if (!IsConnected()) {
        throw std::runtime_error("Not connected to IPC server");
    }

    // Serialize request
    std::string requestStr = request.ToJson().dump();
    requestStr += "\n";  // Add newline delimiter

    // Send request (handle short writes)
    SendAll(requestStr, timeoutMs);

    // Wait for response
    std::string responseStr = ReadResponse(timeoutMs);

    // Parse response
    try {
        json responseJson = json::parse(responseStr);
        return IPCResponse::FromJson(responseJson);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Failed to parse response: ") + e.what());
    }
}

void IPCClient::SendAll(const std::string& data, int timeoutMs) {
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);

    size_t totalSent = 0;
    while (totalSent < data.size()) {
        ssize_t sent = send(clientFd_, data.data() + totalSent, data.size() - totalSent, 0);
        if (sent > 0) {
            totalSent += static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            auto now = Clock::now();
            if (now >= deadline) {
                throw std::runtime_error("Timeout sending request");
            }

            int remainingMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()
            );

            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(clientFd_, &writefds);

            timeval timeout;
            timeout.tv_sec = remainingMs / 1000;
            timeout.tv_usec = (remainingMs % 1000) * 1000;

            int activity = select(clientFd_ + 1, nullptr, &writefds, nullptr, &timeout);
            if (activity <= 0) {
                throw std::runtime_error("Timeout sending request");
            }
            continue;
        }

        throw std::runtime_error(std::string("Failed to send request: ") + strerror(errno));
    }
}

std::string IPCClient::ReadResponse(int timeoutMs) {
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);

    std::string response;
    response.reserve(8192);

    while (true) {
        // Return once we see the newline delimiter.
        size_t newlinePos = response.find('\n');
        if (newlinePos != std::string::npos) {
            return response.substr(0, newlinePos);
        }

        auto now = Clock::now();
        if (now >= deadline) {
            throw std::runtime_error("Timeout waiting for response");
        }

        int remainingMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()
        );

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(clientFd_, &readfds);

        timeval timeout;
        timeout.tv_sec = remainingMs / 1000;
        timeout.tv_usec = (remainingMs % 1000) * 1000;

        int activity = select(clientFd_ + 1, &readfds, nullptr, nullptr, &timeout);
        if (activity < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("Select error: ") + strerror(errno));
        }

        if (activity == 0) {
            throw std::runtime_error("Timeout waiting for response");
        }

        char buffer[CHUNK_SIZE];
        ssize_t n = recv(clientFd_, buffer, sizeof(buffer), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("Failed to receive response: ") + strerror(errno));
        }

        if (n == 0) {
            throw std::runtime_error("Connection closed by server");
        }

        response.append(buffer, buffer + n);
    }
}

} // namespace ipc
} // namespace pathview
