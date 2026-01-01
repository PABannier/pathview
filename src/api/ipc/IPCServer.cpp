#include "IPCServer.h"
#include <iostream>
#include <algorithm>
#include <cstring>
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
bool IPCServer::wsaInitialized_ = false;

bool IPCServer::InitializeWinsock() {
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

void IPCServer::CleanupWinsock() {
    if (wsaInitialized_) {
        WSACleanup();
        wsaInitialized_ = false;
    }
}
#endif

std::string IPCServer::GetLastErrorString() {
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

bool IPCServer::SetNonBlocking(socket_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
#endif
}

void IPCServer::CloseSocket(socket_t fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

std::string IPCServer::GetPortFilePath() const {
#ifdef _WIN32
    const char* temp = std::getenv("TEMP");
    if (!temp) temp = std::getenv("TMP");
    if (!temp) temp = ".";
    return std::string(temp) + "\\pathview-port.txt";
#else
    return "/tmp/pathview-port";
#endif
}

void IPCServer::WritePortFile() {
    std::string path = GetPortFilePath();
    std::ofstream file(path);
    if (file.is_open()) {
        file << port_;
        file.close();
        std::cout << "Wrote port " << port_ << " to " << path << std::endl;
    } else {
        std::cerr << "Warning: Failed to write port file: " << path << std::endl;
    }
}

void IPCServer::RemovePortFile() {
    std::string path = GetPortFilePath();
    std::filesystem::remove(path);
}

static bool SendAll(socket_t fd, const std::string& data, int timeoutMs) {
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);

    size_t totalSent = 0;
    while (totalSent < data.size()) {
#ifdef _WIN32
        int sent = send(fd, data.data() + totalSent, 
                       static_cast<int>(data.size() - totalSent), 0);
#else
        ssize_t sent = send(fd, data.data() + totalSent, data.size() - totalSent, 0);
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
                return false;
            }

            int remainingMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()
            );

            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(fd, &writefds);

            timeval timeout;
            timeout.tv_sec = remainingMs / 1000;
            timeout.tv_usec = (remainingMs % 1000) * 1000;

            int activity = select(static_cast<int>(fd) + 1, nullptr, &writefds, nullptr, &timeout);
            if (activity <= 0) {
                return false;
            }
            continue;
        }

        return false;
    }

    return true;
}

IPCServer::IPCServer(CommandHandler handler)
    : handler_(std::move(handler))
    , serverFd_(INVALID_SOCKET_VALUE)
    , port_(DEFAULT_PORT)
    , currentClientFd_(INVALID_SOCKET_VALUE)
{
}

IPCServer::~IPCServer() {
    Stop();
}

bool IPCServer::Start() {
#ifdef _WIN32
    if (!InitializeWinsock()) {
        return false;
    }
#endif

    // Create TCP socket
    serverFd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverFd_ == INVALID_SOCKET_VALUE) {
        std::cerr << "Failed to create socket: " << GetLastErrorString() << std::endl;
        return false;
    }

    // Allow address reuse
    int optval = 1;
#ifdef _WIN32
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<const char*>(&optval), sizeof(optval));
#else
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

    // Set non-blocking
    if (!SetNonBlocking(serverFd_)) {
        std::cerr << "Failed to set non-blocking: " << GetLastErrorString() << std::endl;
        CloseSocket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }

    // Bind to localhost
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 only

    if (bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket: " << GetLastErrorString() << std::endl;
        CloseSocket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }

    // Listen for connections
    if (listen(serverFd_, MAX_CLIENTS) < 0) {
        std::cerr << "Failed to listen on socket: " << GetLastErrorString() << std::endl;
        CloseSocket(serverFd_);
        serverFd_ = INVALID_SOCKET_VALUE;
        return false;
    }

    // Write port file for client discovery
    WritePortFile();

    std::cout << "IPC server listening on 127.0.0.1:" << port_ << std::endl;
    return true;
}

void IPCServer::Stop() {
    if (serverFd_ == INVALID_SOCKET_VALUE) {
        return;
    }

    // Close all client connections
    for (socket_t fd : clients_) {
        CloseSocket(fd);
    }
    clients_.clear();

    // Close server socket
    CloseSocket(serverFd_);
    serverFd_ = INVALID_SOCKET_VALUE;

    // Remove port file
    RemovePortFile();

    std::cout << "IPC server stopped" << std::endl;
}

void IPCServer::ProcessMessages(int timeoutMs) {
    if (serverFd_ == INVALID_SOCKET_VALUE) {
        return;
    }

    // Prepare fd_set for select()
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(serverFd_, &readfds);

    int maxFd = static_cast<int>(serverFd_);
    for (socket_t fd : clients_) {
        FD_SET(fd, &readfds);
        maxFd = std::max(maxFd, static_cast<int>(fd));
    }

    // Set timeout
    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

    int activity = select(maxFd + 1, &readfds, nullptr, nullptr, &timeout);

    if (activity < 0) {
#ifdef _WIN32
        if (WSAGetLastError() != WSAEINTR) {
#else
        if (errno != EINTR) {
#endif
            std::cerr << "Select error: " << GetLastErrorString() << std::endl;
        }
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
    std::vector<socket_t> clientsCopy = clients_;  // Copy to avoid iterator invalidation
    for (socket_t fd : clientsCopy) {
        if (FD_ISSET(fd, &readfds)) {
            HandleClient(fd);
        }
    }
}

void IPCServer::AcceptConnections() {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);

    socket_t clientFd = accept(serverFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (clientFd == INVALID_SOCKET_VALUE) {
#ifdef _WIN32
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
#else
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
#endif
            std::cerr << "Accept failed: " << GetLastErrorString() << std::endl;
        }
        return;
    }

    if (clients_.size() >= MAX_CLIENTS) {
        std::cerr << "Max clients reached, rejecting connection" << std::endl;
        CloseSocket(clientFd);
        return;
    }

    // Set client socket non-blocking
    if (!SetNonBlocking(clientFd)) {
        std::cerr << "Failed to set client non-blocking: " << GetLastErrorString() << std::endl;
        CloseSocket(clientFd);
        return;
    }

    clients_.push_back(clientFd);
    std::cout << "New IPC client connected (fd=" << clientFd << ")" << std::endl;
}

void IPCServer::HandleClient(socket_t clientFd) {
    char buffer[BUFFER_SIZE];
#ifdef _WIN32
    int n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
#else
    ssize_t n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
#endif

    if (n <= 0) {
#ifdef _WIN32
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
            return;
        }
#else
        if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            return;
        }
#endif
        // Client disconnected or error
        std::cout << "IPC client disconnected (fd=" << clientFd << ")" << std::endl;
        RemoveClient(clientFd);
        return;
    }

    buffer[n] = '\0';

    try {
        // Parse JSON-RPC request
        json requestJson = json::parse(buffer);
        IPCRequest request = IPCRequest::FromJson(requestJson);

        // Set current client FD for handler
        currentClientFd_ = clientFd;

        // Handle request
        IPCResponse response = HandleRequest(request);

        // Clear current client FD
        currentClientFd_ = INVALID_SOCKET_VALUE;

        // Send response
        std::string responseStr = response.ToJson().dump();
        responseStr += "\n";  // Add newline delimiter

        if (!SendAll(clientFd, responseStr, 5000)) {
            std::cerr << "Failed to send response: " << GetLastErrorString() << std::endl;
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
        SendAll(clientFd, responseStr, 5000);
    } catch (const std::exception& e) {
        std::cerr << "IPC error: " << e.what() << std::endl;
    }
}

void IPCServer::RemoveClient(socket_t clientFd) {
    CloseSocket(clientFd);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), clientFd), clients_.end());

    if (disconnectCallback_) {
        disconnectCallback_(clientFd);
    }
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
