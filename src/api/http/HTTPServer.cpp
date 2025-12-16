#include "HTTPServer.h"
#include "httplib.h"  // From cpp-mcp/common/httplib.h
#include <iostream>
#include <sstream>
#include <algorithm>

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

    // MJPEG stream endpoint
    server_->Get("/stream", [this](const httplib::Request& req, httplib::Response& res) {
        // Parse FPS parameter (default 5, max 30)
        int fps = 5;
        if (req.has_param("fps")) {
            try {
                fps = std::stoi(req.get_param_value("fps"));
                fps = std::min(30, std::max(1, fps));
            } catch (...) {
                fps = 5;
            }
        }
        int frameDelayMs = 1000 / fps;

        // Set MJPEG headers
        res.set_header("Content-Type", "multipart/x-mixed-replace; boundary=frame");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");

        // Use content provider for streaming
        res.set_content_provider(
            "multipart/x-mixed-replace; boundary=frame",
            [this, frameDelayMs](size_t offset, httplib::DataSink& sink) {
                auto lastFrameTime = std::chrono::steady_clock::now();

                while (running_) {
                    // FPS throttling
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastFrameTime).count();

                    if (elapsed < frameDelayMs) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(frameDelayMs - elapsed));
                    }
                    lastFrameTime = std::chrono::steady_clock::now();

                    // Get latest snapshot from stream buffer
                    std::string snapshotId = snapshotManager_->GetLatestStreamFrame();
                    if (snapshotId.empty()) {
                        continue;
                    }

                    auto snapshot = snapshotManager_->GetSnapshot(snapshotId);
                    if (!snapshot) {
                        continue;
                    }

                    // Build MJPEG frame
                    std::ostringstream frame;
                    frame << "--frame\r\n"
                         << "Content-Type: image/png\r\n"
                         << "Content-Length: " << snapshot->pngData.size() << "\r\n\r\n";

                    // Write frame header
                    if (!sink.write(frame.str().data(), frame.str().size())) {
                        return false;  // Client disconnected
                    }

                    // Write frame data
                    if (!sink.write(reinterpret_cast<const char*>(snapshot->pngData.data()),
                                   snapshot->pngData.size())) {
                        return false;
                    }

                    // Write frame footer
                    if (!sink.write("\r\n", 2)) {
                        return false;
                    }
                }
                return true;
            }
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
        <li>GET /stream?fps=N - MJPEG stream (default 5 FPS, max 30)</li>
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
