#pragma once

#include "mcp_message.h"  // From cpp-mcp
#include <string>

namespace pathview {

// Forward declarations
namespace ipc {
    class IPCClient;
}

namespace http {
    class SnapshotManager;
    class HTTPServer;
}

namespace mcp {

/**
 * MCP tool handlers
 * Each function implements one MCP tool by sending IPC commands to the GUI
 */
namespace tools {

// Initialize tool handlers with IPC client and HTTP infrastructure
void Initialize(ipc::IPCClient* ipcClient,
                http::SnapshotManager* snapshotManager,
                http::HTTPServer* httpServer);

// Slide control tools
::mcp::json HandleLoadSlide(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleGetSlideInfo(const ::mcp::json& params, const std::string& sessionId);

// Viewport control tools
::mcp::json HandlePan(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleCenterOn(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleZoom(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleZoomAtPoint(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleResetView(const ::mcp::json& params, const std::string& sessionId);

// Snapshot tool
::mcp::json HandleCaptureSnapshot(const ::mcp::json& params, const std::string& sessionId);

// Polygon tools
::mcp::json HandleLoadPolygons(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleQueryPolygons(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleSetPolygonVisibility(const ::mcp::json& params, const std::string& sessionId);

// Session management tools
::mcp::json HandleAgentHello(const ::mcp::json& params, const std::string& sessionId);

// Navigation lock tools
::mcp::json HandleNavLock(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleNavUnlock(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleNavLockStatus(const ::mcp::json& params, const std::string& sessionId);

// Tracked camera movement tools
::mcp::json HandleMoveCamera(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleAwaitMove(const ::mcp::json& params, const std::string& sessionId);

// Annotation/ROI tools
::mcp::json HandleCreateAnnotation(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleListAnnotations(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleGetAnnotation(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleDeleteAnnotation(const ::mcp::json& params, const std::string& sessionId);
::mcp::json HandleComputeROIMetrics(const ::mcp::json& params, const std::string& sessionId);

} // namespace tools
} // namespace mcp
} // namespace pathview
