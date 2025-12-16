#include "MCPTools.h"
#include "../ipc/IPCClient.h"
#include "../http/SnapshotManager.h"
#include "../http/HTTPServer.h"
#include <stdexcept>

namespace pathview {
namespace mcp {
namespace tools {

// Global pointers (initialized by Initialize())
static ipc::IPCClient* g_ipcClient = nullptr;
static http::SnapshotManager* g_snapshotManager = nullptr;
static http::HTTPServer* g_httpServer = nullptr;

void Initialize(ipc::IPCClient* ipcClient,
                http::SnapshotManager* snapshotManager,
                http::HTTPServer* httpServer) {
    g_ipcClient = ipcClient;
    g_snapshotManager = snapshotManager;
    g_httpServer = httpServer;
}

// Helper: Send IPC request and return response
static ::mcp::json SendIPCRequest(const std::string& method, const ::mcp::json& params) {
    if (!g_ipcClient || !g_ipcClient->IsConnected()) {
        throw ::mcp::mcp_exception(::mcp::error_code::internal_error, "Not connected to GUI");
    }

    ipc::IPCRequest request;
    request.id = 1;  // ID doesn't matter for us
    request.method = method;
    request.params = params;

    try {
        ipc::IPCResponse response = g_ipcClient->SendRequest(request);

        if (response.error.has_value()) {
            throw ::mcp::mcp_exception(
                ::mcp::error_code::internal_error,
                response.error->message
            );
        }

        return response.result.value_or(::mcp::json::object());
    } catch (const std::runtime_error& e) {
        throw ::mcp::mcp_exception(::mcp::error_code::internal_error, e.what());
    }
}

::mcp::json HandleLoadSlide(const ::mcp::json& params, const std::string&) {
    if (!params.contains("path")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params, "Missing 'path' parameter");
    }

    return SendIPCRequest("slide.load", params);
}

::mcp::json HandleGetSlideInfo(const ::mcp::json&, const std::string&) {
    return SendIPCRequest("slide.info", ::mcp::json::object());
}

::mcp::json HandlePan(const ::mcp::json& params, const std::string&) {
    if (!params.contains("dx") || !params.contains("dy")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'dx' or 'dy' parameters");
    }

    return SendIPCRequest("viewport.pan", params);
}

::mcp::json HandleCenterOn(const ::mcp::json& params, const std::string&) {
    if (!params.contains("x") || !params.contains("y")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'x' or 'y' parameters");
    }

    return SendIPCRequest("viewport.center_on", params);
}

::mcp::json HandleZoom(const ::mcp::json& params, const std::string&) {
    if (!params.contains("delta")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'delta' parameter");
    }

    return SendIPCRequest("viewport.zoom", params);
}

::mcp::json HandleZoomAtPoint(const ::mcp::json& params, const std::string&) {
    if (!params.contains("screen_x") || !params.contains("screen_y") || !params.contains("delta")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'screen_x', 'screen_y', or 'delta' parameters");
    }

    return SendIPCRequest("viewport.zoom_at_point", params);
}

::mcp::json HandleResetView(const ::mcp::json&, const std::string&) {
    return SendIPCRequest("viewport.reset", ::mcp::json::object());
}

::mcp::json HandleCaptureSnapshot(const ::mcp::json& params, const std::string&) {
    // Send IPC request to capture screenshot
    ::mcp::json result = SendIPCRequest("snapshot.capture", params);

    // Decode base64 PNG data
    std::string base64 = result["png_data"].get<std::string>();
    int width = result["width"].get<int>();
    int height = result["height"].get<int>();

    // Decode base64 to binary
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::vector<uint8_t> pngData;
    int in_len = base64.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];

    while (in_len-- && (base64[in_] != '=') && (isalnum(base64[in_]) || (base64[in_] == '+') || (base64[in_] == '/'))) {
        char_array_4[i++] = base64[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; i < 3; i++)
                pngData.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (j = 0; j < i; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

        for (j = 0; j < i - 1; j++)
            pngData.push_back(char_array_3[j]);
    }

    // Store in snapshot manager
    std::string snapshotId = g_snapshotManager->AddSnapshot(pngData, width, height);

    // Also add to stream buffer for MJPEG streaming
    g_snapshotManager->AddStreamFrame(snapshotId);

    return ::mcp::json{
        {"id", snapshotId},
        {"url", "http://127.0.0.1:8080/snapshot/" + snapshotId},
        {"width", width},
        {"height", height}
    };
}

::mcp::json HandleLoadPolygons(const ::mcp::json& params, const std::string&) {
    if (!params.contains("path")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'path' parameter");
    }

    return SendIPCRequest("polygons.load", params);
}

::mcp::json HandleQueryPolygons(const ::mcp::json& params, const std::string&) {
    if (!params.contains("x") || !params.contains("y") ||
        !params.contains("w") || !params.contains("h")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'x', 'y', 'w', or 'h' parameters");
    }

    return SendIPCRequest("polygons.query", params);
}

::mcp::json HandleSetPolygonVisibility(const ::mcp::json& params, const std::string&) {
    if (!params.contains("visible")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'visible' parameter");
    }

    return SendIPCRequest("polygons.set_visibility", params);
}

::mcp::json HandleAgentHello(const ::mcp::json& params, const std::string& sessionId) {
    // Extract agent identity
    std::string agentName = params.value("agent_name", "");
    std::string agentVersion = params.value("agent_version", "");

    if (agentName.empty()) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'agent_name' parameter");
    }

    // Get session info from IPC (includes lock status)
    ::mcp::json sessionParams = {
        {"agent_name", agentName},
        {"agent_version", agentVersion},
        {"session_id", sessionId}
    };

    return SendIPCRequest("session.hello", sessionParams);
}

::mcp::json HandleNavLock(const ::mcp::json& params, const std::string&) {
    if (!params.contains("owner_uuid")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'owner_uuid' parameter");
    }

    return SendIPCRequest("nav.lock", params);
}

::mcp::json HandleNavUnlock(const ::mcp::json& params, const std::string&) {
    if (!params.contains("owner_uuid")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing 'owner_uuid' parameter");
    }

    return SendIPCRequest("nav.unlock", params);
}

::mcp::json HandleNavLockStatus(const ::mcp::json&, const std::string&) {
    return SendIPCRequest("nav.lock_status", ::mcp::json::object());
}

::mcp::json HandleMoveCamera(const ::mcp::json& params, const std::string&) {
    if (!params.contains("center_x") || !params.contains("center_y") || !params.contains("zoom")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing required parameters: center_x, center_y, zoom");
    }
    return SendIPCRequest("viewport.move", params);
}

::mcp::json HandleAwaitMove(const ::mcp::json& params, const std::string&) {
    if (!params.contains("token")) {
        throw ::mcp::mcp_exception(::mcp::error_code::invalid_params,
                                    "Missing required parameter: token");
    }
    return SendIPCRequest("viewport.await_move", params);
}

} // namespace tools
} // namespace mcp
} // namespace pathview
