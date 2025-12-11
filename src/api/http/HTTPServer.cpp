#include "HTTPServer.h"
#include "httplib.h"  // From cpp-mcp/common/httplib.h
#include <iostream>

namespace pathview {
namespace http {

HTTPServer::HTTPServer(int port, SnapshotManager* snapshotManager)
    : server_(std::make_unique<httplib::Server>())
    , snapshotManager_(snapshotManager)
    , port_(port)
    , running_(false)
{
    SetupRoutes();
}

HTTPServer::~HTTPServer() {
    Stop();
}

void HTTPServer::SetupRoutes() {
    // Health check endpoint
    server_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    // Snapshot endpoint
    server_->Get(R"(/snapshot/([a-f0-9\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];

        // Get snapshot from manager
        auto snapshot = snapshotManager_->GetSnapshot(id);

        if (!snapshot) {
            res.status = 404;
            res.set_content("Snapshot not found", "text/plain");
            return;
        }

        // Serve PNG image
        res.set_content(
            reinterpret_cast<const char*>(snapshot->pngData.data()),
            snapshot->pngData.size(),
            "image/png"
        );
    });

    // Root endpoint
    server_->Get("/", [this](const httplib::Request&, httplib::Response& res) {
        std::string html = R"(
<!DOCTYPE html>
<html>
<head><title>PathView HTTP Server</title></head>
<body>
    <h1>PathView HTTP Server</h1>
    <p>Server is running on port )" + std::to_string(port_) + R"(</p>
    <p>Endpoints:</p>
    <ul>
        <li>GET /health - Health check</li>
        <li>GET /snapshot/{id} - Get snapshot image</li>
    </ul>
    <p>Cached snapshots: )" + std::to_string(snapshotManager_->GetCacheSize()) + R"(</p>
</body>
</html>
)";
        res.set_content(html, "text/html");
    });
}

void HTTPServer::Start() {
    if (running_) {
        return;
    }

    running_ = true;
    std::cout << "HTTP server starting on http://127.0.0.1:" << port_ << std::endl;

    // Bind to localhost only for security
    if (!server_->listen("127.0.0.1", port_)) {
        std::cerr << "Failed to start HTTP server on port " << port_ << std::endl;
        running_ = false;
    }
}

void HTTPServer::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    server_->stop();
    std::cout << "HTTP server stopped" << std::endl;
}

} // namespace http
} // namespace pathview
