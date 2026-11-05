#include "mcp/client.hpp"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "mcp/server.hpp"

extern char** environ;

namespace ll::mcp {

// ---- InProcessTransport ------------------------------------------------------

void InProcessTransport::send(const json& message) {
    // Round-trip through text so tests see exactly what a pipe would carry.
    if (auto reply = server_.handle_line(message.dump())) inbox_.push_back(json::parse(*reply));
}

json InProcessTransport::receive() {
    if (inbox_.empty()) throw std::runtime_error("in-process server sent no reply");
    json m = std::move(inbox_.front());
    inbox_.pop_front();
    return m;
}

// ---- ProcessTransport --------------------------------------------------------

struct ProcessTransport::Reader {
    FILE* in = nullptr;
    ~Reader() {
        if (in) std::fclose(in);
    }
};

ProcessTransport::ProcessTransport(const std::vector<std::string>& argv) : reader_(std::make_unique<Reader>()) {
    if (argv.empty()) throw std::invalid_argument("empty server command");
    int in_pipe[2], out_pipe[2];  // [0] read end, [1] write end
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) throw std::runtime_error(std::strerror(errno));

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    for (const int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1]}) {
        posix_spawn_file_actions_addclose(&actions, fd);
    }
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    const int rc = posix_spawnp(&pid_, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(in_pipe[0]);
    close(out_pipe[1]);
    if (rc != 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        throw std::runtime_error("cannot start " + argv[0] + ": " + std::strerror(rc));
    }
    to_child_ = in_pipe[1];
    reader_->in = fdopen(out_pipe[0], "r");
}

ProcessTransport::~ProcessTransport() {
    if (to_child_ >= 0) close(to_child_);  // EOF on stdin: the server exits
    reader_.reset();
    if (pid_ > 0) waitpid(pid_, nullptr, 0);
}

void ProcessTransport::send(const json& message) {
    std::string line = message.dump() + "\n";
    const char* p = line.data();
    std::size_t left = line.size();
    while (left > 0) {
        const auto n = write(to_child_, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("write to server: ") + std::strerror(errno));
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
}

json ProcessTransport::receive() {
    char* buf = nullptr;
    std::size_t cap = 0;
    while (true) {
        const auto n = getline(&buf, &cap, reader_->in);
        if (n < 0) {
            std::free(buf);
            throw std::runtime_error("server closed its output");
        }
        std::string line(buf, static_cast<std::size_t>(n));
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (line.empty()) continue;
        std::free(buf);
        return json::parse(line);
    }
}

// ---- McpClient ---------------------------------------------------------------

McpClient::McpClient(Transport& transport, Implementation client_info)
    : transport_(transport), client_info_(std::move(client_info)) {}

json McpClient::request_once(const std::string& method, const json& params) {
    const long id = next_id_++;
    transport_.send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
    while (true) {
        const json m = transport_.receive();
        // Skip server notifications and anything that is not our reply.
        if (!m.contains("id") || m["id"] != id) continue;
        if (m.contains("error")) {
            const auto& e = m["error"];
            throw RpcError(e.value("code", 0), e.value("message", std::string("error")), e.value("data", json()));
        }
        return m.value("result", json::object());
    }
}

json McpClient::request(const std::string& method, json params) {
    if (modern_) {
        params["_meta"] = {{kMetaProtocolVersion, version_},
                           {kMetaClientInfo, client_info_.to_json()},
                           {kMetaClientCapabilities, json::object()}};
    }
    return request_once(method, params);
}

void McpClient::connect() {
    // Modern probe first (stdio backward-compatibility rule of 2026-07-28).
    std::string try_version(kModernVersions[0]);
    for (int attempt = 0; attempt < 2; ++attempt) {
        const json meta = {{kMetaProtocolVersion, try_version},
                           {kMetaClientInfo, client_info_.to_json()},
                           {kMetaClientCapabilities, json::object()}};
        try {
            const json r = request_once("server/discover", {{"_meta", meta}});
            modern_ = true;
            version_ = try_version;
            server_info_ = r.value("_meta", json::object()).value(kMetaServerInfo, json::object());
            instructions_ = r.value("instructions", std::string());
            return;
        } catch (const RpcError& e) {
            if (e.code() != error_code::kUnsupportedProtocolVersion) break;  // not a modern server
            // Modern server, different modern version: pick one we share.
            std::string pick;
            for (const auto& v : e.data().value("supported", json::array())) {
                if (pick.empty() && is_modern_version(v.get<std::string>())) pick = v.get<std::string>();
            }
            if (pick.empty()) break;  // only legacy versions offered: use initialize
            if (pick == try_version) {
                throw std::runtime_error("no protocol version in common with server: " + e.data().dump());
            }
            try_version = pick;
        }
    }

    // Legacy server: initialize handshake.
    const json r = request_once("initialize", {{"protocolVersion", std::string(kLegacyVersions[0])},
                                               {"capabilities", json::object()},
                                               {"clientInfo", client_info_.to_json()}});
    version_ = r.value("protocolVersion", std::string());
    if (!is_legacy_version(version_)) throw std::runtime_error("server chose unsupported protocol version " + version_);
    modern_ = false;
    server_info_ = r.value("serverInfo", json::object());
    instructions_ = r.value("instructions", std::string());
    transport_.send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
}

std::vector<ToolInfo> McpClient::list_tools() {
    std::vector<ToolInfo> out;
    json params = json::object();
    while (true) {
        const json r = request("tools/list", params);
        for (const auto& t : r.value("tools", json::array())) {
            out.push_back({t.at("name"), t.value("description", std::string()),
                           t.value("inputSchema", json{{"type", "object"}})});
        }
        if (!r.contains("nextCursor") || r["nextCursor"].is_null()) break;
        params = {{"cursor", r["nextCursor"]}};
    }
    return out;
}

CallResult McpClient::call_tool(const std::string& name, const json& arguments) {
    const json r = request("tools/call", {{"name", name}, {"arguments", arguments}});
    CallResult out;
    for (const auto& c : r.value("content", json::array())) {
        if (c.value("type", "") == "text") {
            if (!out.text.empty()) out.text += "\n";
            out.text += c.value("text", "");
        }
    }
    out.structured = r.value("structuredContent", json());
    out.is_error = r.value("isError", false);
    return out;
}

}  // namespace ll::mcp
