#pragma once

#include "SnapshotManager.h"
#include <memory>
#include <string>
#include <atomic>
#include <thread>

// Forward declare httplib::Server
namespace httplib {
    class Server;
}

namespace pathview {
namespace http {

/**
 * HTTP server for serving snapshot images
 * Uses cpp-httplib (header-only library)
 */
class HTTPServer {
public:
    HTTPServer(int port, SnapshotManager* snapshotManager);
    ~HTTPServer();

    // Delete copy constructor and assignment
    HTTPServer(const HTTPServer&) = delete;
    HTTPServer& operator=(const HTTPServer&) = delete;

    /**
     * Start the HTTP server (blocking)
     * Should be called in a separate thread
     */
    void Start();

    /**
     * Stop the HTTP server
     */
    void Stop();

    /**
     * Check if server is running
     */
    bool IsRunning() const { return running_; }

    /**
     * Get the port number
     */
    int GetPort() const { return port_; }

private:
    void SetupRoutes();

    std::unique_ptr<httplib::Server> server_;
    SnapshotManager* snapshotManager_;
    int port_;
    std::atomic<bool> running_;
};

} // namespace http
} // namespace pathview
