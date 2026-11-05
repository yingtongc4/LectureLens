#pragma once

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

// JSON-RPC 2.0 and Model Context Protocol constants shared by server and client.
namespace ll::mcp {

using json = nlohmann::json;

// Modern revisions carry version and capabilities in every request's _meta;
// legacy revisions negotiate once with `initialize`. Newest first.
inline constexpr std::array<std::string_view, 1> kModernVersions = {"2026-07-28"};
inline constexpr std::array<std::string_view, 3> kLegacyVersions = {"2025-11-25", "2025-06-18", "2025-03-26"};

inline constexpr const char* kMetaProtocolVersion = "io.modelcontextprotocol/protocolVersion";
inline constexpr const char* kMetaClientCapabilities = "io.modelcontextprotocol/clientCapabilities";
inline constexpr const char* kMetaClientInfo = "io.modelcontextprotocol/clientInfo";
inline constexpr const char* kMetaServerInfo = "io.modelcontextprotocol/serverInfo";

namespace error_code {
inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;
inline constexpr int kUnsupportedProtocolVersion = -32022;  // 2026-07-28
}  // namespace error_code

// A JSON-RPC error response received by a client, or raised by a handler.
class RpcError : public std::runtime_error {
public:
    RpcError(int code, const std::string& message, json data = nullptr)
        : std::runtime_error(message), code_(code), data_(std::move(data)) {}
    int code() const { return code_; }
    const json& data() const { return data_; }

private:
    int code_;
    json data_;
};

inline bool is_modern_version(std::string_view v) {
    for (const auto m : kModernVersions) {
        if (m == v) return true;
    }
    return false;
}

inline bool is_legacy_version(std::string_view v) {
    for (const auto l : kLegacyVersions) {
        if (l == v) return true;
    }
    return false;
}

inline json supported_versions() {
    json out = json::array();
    for (const auto v : kModernVersions) out.push_back(v);
    for (const auto v : kLegacyVersions) out.push_back(v);
    return out;
}

struct Implementation {
    std::string name;
    std::string version;
    json to_json() const { return {{"name", name}, {"version", version}}; }
};

}  // namespace ll::mcp
