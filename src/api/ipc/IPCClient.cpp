#include "IPCClient.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/select.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <cerrno>
#endif

namespace pathview {
namespace ipc {

#ifdef _WIN32
bool IPCClient::wsaInitialized_ = false;

bool IPCClient::InitializeWinsock() {
    if (wsaInitialized_) return true;
    
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }
    wsaInitialized_ = true;
    return true;
}

void IPCClient::CleanupWinsock() {
    if (wsaInitialized_) {
        WSACleanup();
        wsaInitialized_ = false;
    }
}
#endif

std::string IPCClient::GetLastErrorString() {
#ifdef _WIN32
    int err = WSAGetLastError();
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                   nullptr, err, 0, (LPSTR)&msg, 0, nullptr);
    std::string result = msg ? msg : "Unknown error";
    LocalFree(msg);
    return result;
#else
    return strerror(errno);
#endif
}

void IPCClient::CloseSocket(socket_t fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

int IPCClient::ReadPortFromFile() {
    std::string path;
#ifdef _WIN32
    const char* temp = std::getenv("TEMP");
    if (!temp) temp = std::getenv("TMP");
    if (!temp) temp = ".";
    path = std::string(temp) + "\\pathview-port.txt";
#else
    path = "/tmp/pathview-port";
#endif

    std::ifstream file(path);
    if (file.is_open()) {
        int port;
        if (file >> port) {
            return port;
        }
    }
    return 9999;  // Default port
}

IPCClient::IPCClient(int port)
    : port_(port)
    , clientFd_(INVALID_SOCKET_VALUE)
    , nextId_(1)
{
}

IPCClient::~IPCClient() {
    Disconnect();
}

bool IPCClient::Connect() {
    if (clientFd_ != INVALID_SOCKET_VALUE) {
        return true;  // Already connected
    }

#ifdef _WIN32
    if (!InitializeWinsock()) {
        return false;
    }
#endif

    // Create TCP socket
    clientFd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientFd_ == INVALID_SOCKET_VALUE) {
        std::cerr << "Failed to create socket: " << GetLastErrorString() << std::endl;
        return false;
    }

    // Prepare address (localhost)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1

    // Connect to server
    if (connect(clientFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to localhost:" << port_ << ": " 
                  << GetLastErrorString() << std::endl;
        CloseSocket(clientFd_);
        clientFd_ = INVALID_SOCKET_VALUE;
        return false;
    }

    std::cout << "Connected to IPC server at localhost:" << port_ << std::endl;
    return true;
}

void IPCClient::Disconnect() {
    if (clientFd_ != INVALID_SOCKET_VALUE) {
        CloseSocket(clientFd_);
        clientFd_ = INVALID_SOCKET_VALUE;
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
#ifdef _WIN32
        int sent = send(clientFd_, data.data() + totalSent, 
                       static_cast<int>(data.size() - totalSent), 0);
#else
        ssize_t sent = send(clientFd_, data.data() + totalSent, data.size() - totalSent, 0);
#endif
        if (sent > 0) {
            totalSent += static_cast<size_t>(sent);
            continue;
        }
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
#else
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
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

            int activity = select(static_cast<int>(clientFd_) + 1, nullptr, &writefds, nullptr, &timeout);
            if (activity <= 0) {
                throw std::runtime_error("Timeout sending request");
            }
            continue;
        }

        throw std::runtime_error(std::string("Failed to send request: ") + GetLastErrorString());
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

        int activity = select(static_cast<int>(clientFd_) + 1, &readfds, nullptr, nullptr, &timeout);
        if (activity < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) {
                continue;
            }
#else
            if (errno == EINTR) {
                continue;
            }
#endif
            throw std::runtime_error(std::string("Select error: ") + GetLastErrorString());
        }

        if (activity == 0) {
            throw std::runtime_error("Timeout waiting for response");
        }

        char buffer[CHUNK_SIZE];
#ifdef _WIN32
        int n = recv(clientFd_, buffer, sizeof(buffer), 0);
#else
        ssize_t n = recv(clientFd_, buffer, sizeof(buffer), 0);
#endif
        if (n < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) {
                continue;
            }
#else
            if (errno == EINTR) {
                continue;
            }
#endif
            throw std::runtime_error(std::string("Failed to receive response: ") + GetLastErrorString());
        }

        if (n == 0) {
            throw std::runtime_error("Connection closed by server");
        }

        response.append(buffer, buffer + n);
    }
}

} // namespace ipc
} // namespace pathview
