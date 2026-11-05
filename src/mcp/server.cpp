#include "mcp/server.hpp"

#include <istream>
#include <ostream>

#include "mcp/schema.hpp"

namespace ll::mcp {
namespace {

json error_response(const json& id, int code, const std::string& message, const json& data = nullptr) {
    json err = {{"code", code}, {"message", message}};
    if (!data.is_null()) err["data"] = data;
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", err}};
}

}  // namespace

Server::Server(Implementation info, std::string instructions, ServerOptions options)
    : info_(std::move(info)), instructions_(std::move(instructions)), options_(options) {}

void Server::add_tool(Tool tool) { tools_.push_back(std::move(tool)); }

std::optional<std::string> Server::handle_line(std::string_view line) {
    const auto msg = json::parse(line, nullptr, false);
    if (msg.is_discarded()) return error_response(nullptr, error_code::kParseError, "parse error").dump();
    const auto reply = handle(msg);
    if (!reply) return std::nullopt;
    return reply->dump();
}

std::optional<json> Server::handle(const json& msg) {
    if (!msg.is_object() || msg.value("jsonrpc", "") != "2.0" || !msg.contains("method") ||
        !msg["method"].is_string()) {
        // A response or garbage; reply only if it carries an id we can echo.
        if (msg.is_object() && msg.contains("id") && !msg.contains("result") && !msg.contains("error")) {
            return error_response(msg["id"], error_code::kInvalidRequest, "invalid request");
        }
        if (!msg.is_object()) return error_response(nullptr, error_code::kInvalidRequest, "invalid request");
        return std::nullopt;
    }
    const std::string method = msg["method"];
    const json params = msg.value("params", json::object());
    if (!msg.contains("id")) return std::nullopt;  // notification: initialized, cancelled, ...
    const json id = msg["id"];

    // Era is chosen per request: a protocol version in _meta means modern.
    // A legacy-only server does not know that key, like a real legacy server.
    const json meta = params.is_object() ? params.value("_meta", json::object()) : json::object();
    const bool modern = options_.modern && meta.is_object() && meta.contains(kMetaProtocolVersion);
    try {
        if (modern) {
            const auto requested = meta[kMetaProtocolVersion].get<std::string>();
            if (!is_modern_version(requested)) {
                json supported = json::array();
                for (const auto& v : supported_versions()) {
                    if (is_modern_version(v.get<std::string>()) ? options_.modern : options_.legacy) supported.push_back(v);
                }
                return error_response(id, error_code::kUnsupportedProtocolVersion, "Unsupported protocol version",
                                      {{"supported", supported}, {"requested", requested}});
            }
        }
        return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", dispatch(method, params, modern)}};
    } catch (const RpcError& e) {
        return error_response(id, e.code(), e.what(), e.data());
    } catch (const json::exception& e) {
        return error_response(id, error_code::kInvalidParams, std::string("invalid params: ") + e.what());
    } catch (const std::exception& e) {
        return error_response(id, error_code::kInternalError, e.what());
    }
}

json Server::dispatch(const std::string& method, const json& params, bool modern) {
    if (method == "server/discover" && options_.modern) {
        json r = discover();
        add_modern_fields(r);
        return r;
    }
    if (method == "initialize" && options_.legacy) return initialize(params);
    if (method == "ping" && options_.legacy) return json::object();
    if (method == "tools/list") return list_tools(modern);
    if (method == "tools/call") {
        json r = call_tool(params);
        if (modern) add_modern_fields(r);
        return r;
    }
    if (method == "initialize") {
        // Modern-only: legacy clients cannot fall forward, so say what works.
        throw RpcError(error_code::kMethodNotFound, "initialize is not supported; this server speaks protocol "
                                                    "versions " + supported_versions().dump());
    }
    throw RpcError(error_code::kMethodNotFound, "method not found: " + method);
}

json Server::discover() const {
    json versions = json::array();
    for (const auto& v : supported_versions()) {
        if (is_modern_version(v.get<std::string>()) ? options_.modern : options_.legacy) versions.push_back(v);
    }
    json r = {{"supportedVersions", versions},
              {"capabilities", {{"tools", {{"listChanged", false}}}}},
              {"ttlMs", 3600000},
              {"cacheScope", "public"}};
    if (!instructions_.empty()) r["instructions"] = instructions_;
    return r;
}

json Server::initialize(const json& params) {
    // Answer with the client's version when we support it, else our newest
    // legacy version; the client decides whether it can continue.
    const auto requested = params.value("protocolVersion", std::string());
    legacy_version_ = is_legacy_version(requested) ? requested : std::string(kLegacyVersions[0]);
    json r = {{"protocolVersion", *legacy_version_},
              {"capabilities", {{"tools", {{"listChanged", false}}}}},
              {"serverInfo", info_.to_json()}};
    if (!instructions_.empty()) r["instructions"] = instructions_;
    return r;
}

json Server::list_tools(bool modern) const {
    json tools = json::array();
    for (const auto& t : tools_) {
        json d = {{"name", t.name}, {"description", t.description}, {"inputSchema", t.input_schema}};
        if (!t.title.empty()) d["title"] = t.title;
        d["annotations"] = {{"readOnlyHint", t.read_only}, {"openWorldHint", false}};
        tools.push_back(std::move(d));
    }
    json r = {{"tools", tools}};
    if (modern) {
        r["ttlMs"] = 3600000;
        r["cacheScope"] = "public";
        add_modern_fields(r);
    }
    return r;
}

json Server::call_tool(const json& params) {
    const auto name = params.at("name").get<std::string>();
    const json args = params.value("arguments", json::object());
    const Tool* tool = nullptr;
    for (const auto& t : tools_) {
        if (t.name == name) tool = &t;
    }
    if (!tool) throw RpcError(error_code::kInvalidParams, "unknown tool: " + name);

    // Bad arguments and failures inside the tool are results with isError, not
    // protocol errors, so the model can read them and correct itself.
    ToolResult out;
    if (const auto problem = validate_arguments(tool->input_schema, args)) {
        out = {{{"error", *problem}}, "invalid arguments: " + *problem, true};
    } else {
        try {
            out = tool->run(args);
        } catch (const std::exception& e) {
            out = {{{"error", e.what()}}, e.what(), true};
        }
    }
    json r = {{"content", json::array({{{"type", "text"}, {"text", out.text.empty() ? out.structured.dump() : out.text}}})},
              {"isError", out.is_error}};
    if (!out.structured.is_null()) r["structuredContent"] = out.structured;
    return r;
}

void Server::add_modern_fields(json& result) const {
    result["resultType"] = "complete";
    result["_meta"] = {{kMetaServerInfo, info_.to_json()}};
}

void serve(Server& server, std::istream& in, std::ostream& out) {
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line == "\r") continue;
        if (const auto reply = server.handle_line(line)) out << *reply << '\n' << std::flush;
    }
}

}  // namespace ll::mcp
