#include "IPCMessage.h"

namespace pathview {
namespace ipc {

// IPCError methods
json IPCError::ToJson() const {
    json j = {
        {"code", code},
        {"message", message}
    };

    if (data.has_value()) {
        j["data"] = data.value();
    }

    return j;
}

IPCError IPCError::FromJson(const json& j) {
    IPCError error;
    error.code = j.at("code").get<int>();
    error.message = j.at("message").get<std::string>();

    if (j.contains("data")) {
        error.data = j.at("data");
    }

    return error;
}

// IPCRequest methods
json IPCRequest::ToJson() const {
    return json{
        {"jsonrpc", jsonrpc},
        {"id", id},
        {"method", method},
        {"params", params}
    };
}

IPCRequest IPCRequest::FromJson(const json& j) {
    IPCRequest request;
    request.jsonrpc = j.value("jsonrpc", "2.0");
    request.id = j.at("id").get<int>();
    request.method = j.at("method").get<std::string>();
    request.params = j.value("params", json::object());

    return request;
}

// IPCResponse methods
json IPCResponse::ToJson() const {
    json j = {
        {"jsonrpc", jsonrpc},
        {"id", id}
    };

    if (result.has_value()) {
        j["result"] = result.value();
    }

    if (error.has_value()) {
        j["error"] = error.value().ToJson();
    }

    return j;
}

IPCResponse IPCResponse::FromJson(const json& j) {
    IPCResponse response;
    response.jsonrpc = j.value("jsonrpc", "2.0");
    response.id = j.at("id").get<int>();

    if (j.contains("result")) {
        response.result = j.at("result");
    }

    if (j.contains("error")) {
        response.error = IPCError::FromJson(j.at("error"));
    }

    return response;
}

} // namespace ipc
} // namespace pathview
