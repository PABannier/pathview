#pragma once

#include <string>
#include <optional>
#include "json.hpp"

namespace pathview {
namespace ipc {

using json = nlohmann::json;

/**
 * JSON-RPC 2.0 error structure
 */
struct IPCError {
    int code;
    std::string message;
    std::optional<json> data;

    json ToJson() const;
    static IPCError FromJson(const json& j);
};

/**
 * JSON-RPC 2.0 request message
 */
struct IPCRequest {
    std::string jsonrpc = "2.0";
    int id;
    std::string method;
    json params;

    json ToJson() const;
    static IPCRequest FromJson(const json& j);
};

/**
 * JSON-RPC 2.0 response message
 */
struct IPCResponse {
    std::string jsonrpc = "2.0";
    int id;
    std::optional<json> result;
    std::optional<IPCError> error;

    json ToJson() const;
    static IPCResponse FromJson(const json& j);
};

/**
 * JSON-RPC 2.0 error codes
 */
namespace ErrorCodes {
    constexpr int ParseError = -32700;
    constexpr int InvalidRequest = -32600;
    constexpr int MethodNotFound = -32601;
    constexpr int InvalidParams = -32602;
    constexpr int InternalError = -32603;

    // Custom application errors
    constexpr int NoSlideLoaded = -32000;
    constexpr int NoPolygonsLoaded = -32001;
    constexpr int FileNotFound = -32002;
    constexpr int InvalidOperation = -32003;
}

} // namespace ipc
} // namespace pathview
