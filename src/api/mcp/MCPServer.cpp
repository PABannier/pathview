#include "MCPServer.h"
#include "MCPTools.h"
#include "../ipc/IPCClient.h"
#include "../http/SnapshotManager.h"
#include "../http/HTTPServer.h"
#include "mcp_tool.h"
#include <iostream>

namespace pathview {
namespace mcp {

MCPServer::MCPServer(ipc::IPCClient* ipcClient,
                     http::SnapshotManager* snapshotManager,
                     http::HTTPServer* httpServer,
                     int mcpPort)
    : ipcClient_(ipcClient)
    , snapshotManager_(snapshotManager)
    , httpServer_(httpServer)
{
    // Initialize MCP server with HTTP+SSE transport
    ::mcp::server::configuration config;
    config.host = "127.0.0.1";
    config.port = mcpPort;
    config.sse_endpoint = "/sse";

    server_ = std::make_unique<::mcp::server>(config);

    // Set server info
    server_->set_server_info("PathView MCP Server", "0.1.0");

    // Set capabilities
    ::mcp::json capabilities = {
        {"resources", ::mcp::json::object()},
        {"tools", ::mcp::json::object()}
    };
    server_->set_capabilities(capabilities);

    // Initialize tool handlers with our infrastructure
    tools::Initialize(ipcClient_, snapshotManager_, httpServer_);
}

MCPServer::~MCPServer() {
    Stop();
}

void MCPServer::RegisterTools() {
    // Slide control tools
    ::mcp::tool load_slide = ::mcp::tool_builder("load_slide")
        .with_description("Load a whole-slide image file")
        .with_string_param("path", "Absolute path to slide file (.svs, .tiff, etc.)")
        .build();
    server_->register_tool(load_slide, tools::HandleLoadSlide);

    ::mcp::tool get_slide_info = ::mcp::tool_builder("get_slide_info")
        .with_description("Get information about the currently loaded slide")
        .build();
    server_->register_tool(get_slide_info, tools::HandleGetSlideInfo);

    // Viewport control tools
    ::mcp::tool pan = ::mcp::tool_builder("pan")
        .with_description("Pan the viewport by delta in slide coordinates")
        .with_number_param("dx", "X delta in pixels")
        .with_number_param("dy", "Y delta in pixels")
        .build();
    server_->register_tool(pan, tools::HandlePan);

    ::mcp::tool center_on = ::mcp::tool_builder("center_on")
        .with_description("Center viewport on a specific point in slide coordinates")
        .with_number_param("x", "X coordinate in slide space")
        .with_number_param("y", "Y coordinate in slide space")
        .build();
    server_->register_tool(center_on, tools::HandleCenterOn);

    ::mcp::tool zoom = ::mcp::tool_builder("zoom")
        .with_description("Zoom in or out (delta: 1.1 = 10% in, 0.9 = 10% out)")
        .with_number_param("delta", "Zoom factor (> 1.0 zooms in, < 1.0 zooms out)")
        .build();
    server_->register_tool(zoom, tools::HandleZoom);

    ::mcp::tool zoom_at_point = ::mcp::tool_builder("zoom_at_point")
        .with_description("Zoom at a specific screen point")
        .with_number_param("screen_x", "Screen X coordinate")
        .with_number_param("screen_y", "Screen Y coordinate")
        .with_number_param("delta", "Zoom factor")
        .build();
    server_->register_tool(zoom_at_point, tools::HandleZoomAtPoint);

    ::mcp::tool reset_view = ::mcp::tool_builder("reset_view")
        .with_description("Reset viewport to fit entire slide in window")
        .build();
    server_->register_tool(reset_view, tools::HandleResetView);

    // Snapshot tool
    ::mcp::tool capture_snapshot = ::mcp::tool_builder("capture_snapshot")
        .with_description("Capture current viewport as PNG image")
        .with_number_param("width", "Image width (optional)", false)
        .with_number_param("height", "Image height (optional)", false)
        .build();
    server_->register_tool(capture_snapshot, tools::HandleCaptureSnapshot);

    // Polygon tools
    ::mcp::tool load_polygons = ::mcp::tool_builder("load_polygons")
        .with_description("Load polygon overlay from protobuf file")
        .with_string_param("path", "Absolute path to .pb or .protobuf file")
        .build();
    server_->register_tool(load_polygons, tools::HandleLoadPolygons);

    ::mcp::tool query_polygons = ::mcp::tool_builder("query_polygons")
        .with_description("Query polygons in a rectangular region")
        .with_number_param("x", "Region X coordinate (slide space)")
        .with_number_param("y", "Region Y coordinate (slide space)")
        .with_number_param("w", "Region width")
        .with_number_param("h", "Region height")
        .build();
    server_->register_tool(query_polygons, tools::HandleQueryPolygons);

    ::mcp::tool set_polygon_visibility = ::mcp::tool_builder("set_polygon_visibility")
        .with_description("Show or hide polygon overlay")
        .with_boolean_param("visible", "True to show, false to hide")
        .build();
    server_->register_tool(set_polygon_visibility, tools::HandleSetPolygonVisibility);

    // Session management tools
    ::mcp::tool agent_hello = ::mcp::tool_builder("agent_hello")
        .with_description("Register agent identity and get session info")
        .with_string_param("agent_name", "Name/identifier of the AI agent")
        .with_string_param("agent_version", "Version of the AI agent (optional)", false)
        .build();
    server_->register_tool(agent_hello, tools::HandleAgentHello);

    // Navigation lock tools
    ::mcp::tool nav_lock = ::mcp::tool_builder("nav_lock")
        .with_description("Acquire navigation lock to prevent user input")
        .with_string_param("owner_uuid", "UUID of lock owner (agent)")
        .with_number_param("ttl_seconds", "Lock time-to-live in seconds (default 300)", false)
        .build();
    server_->register_tool(nav_lock, tools::HandleNavLock);

    ::mcp::tool nav_unlock = ::mcp::tool_builder("nav_unlock")
        .with_description("Release navigation lock")
        .with_string_param("owner_uuid", "UUID of lock owner (agent)")
        .build();
    server_->register_tool(nav_unlock, tools::HandleNavUnlock);

    ::mcp::tool nav_lock_status = ::mcp::tool_builder("nav_lock_status")
        .with_description("Check navigation lock status")
        .build();
    server_->register_tool(nav_lock_status, tools::HandleNavLockStatus);

    std::cout << "Registered " << 15 << " MCP tools" << std::endl;
}

void MCPServer::Run() {
    std::cout << "Starting MCP server on http://127.0.0.1:9000" << std::endl;
    std::cout << "SSE endpoint: http://127.0.0.1:9000/sse" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    server_->start(true);  // Blocking
}

void MCPServer::Stop() {
    if (server_) {
        server_->stop();
    }
}

} // namespace mcp
} // namespace pathview
