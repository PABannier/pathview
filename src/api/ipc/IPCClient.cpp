#include "IPCClient.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <stdexcept>

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

    // Send request
    ssize_t sent = send(clientFd_, requestStr.c_str(), requestStr.size(), 0);
    if (sent < 0) {
        throw std::runtime_error(std::string("Failed to send request: ") + strerror(errno));
    }

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

std::string IPCClient::ReadResponse(int timeoutMs) {
    // Set up select() for timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(clientFd_, &readfds);

    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

    int activity = select(clientFd_ + 1, &readfds, nullptr, nullptr, &timeout);

    if (activity < 0) {
        throw std::runtime_error(std::string("Select error: ") + strerror(errno));
    }

    if (activity == 0) {
        throw std::runtime_error("Timeout waiting for response");
    }

    // Read response
    char buffer[BUFFER_SIZE];
    ssize_t n = recv(clientFd_, buffer, sizeof(buffer) - 1, 0);

    if (n < 0) {
        throw std::runtime_error(std::string("Failed to receive response: ") + strerror(errno));
    }

    if (n == 0) {
        throw std::runtime_error("Connection closed by server");
    }

    buffer[n] = '\0';

    // Find newline delimiter
    std::string response(buffer);
    size_t newlinePos = response.find('\n');
    if (newlinePos != std::string::npos) {
        response = response.substr(0, newlinePos);
    }

    return response;
}

} // namespace ipc
} // namespace pathview
