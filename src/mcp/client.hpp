#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "mcp/protocol.hpp"

namespace ll::mcp {

class Server;

// Message channel to one MCP server.
class Transport {
public:
    virtual ~Transport() = default;
    virtual void send(const json& message) = 0;
    virtual json receive() = 0;  // blocks for the next message from the server
};

// Calls a Server object directly (tests, embedding).
class InProcessTransport final : public Transport {
public:
    explicit InProcessTransport(Server& server) : server_(server) {}
    void send(const json& message) override;
    json receive() override;

private:
    Server& server_;
    std::deque<json> inbox_;
};

// Launches a server process and speaks newline-delimited JSON over its
// stdin/stdout (the MCP stdio transport); its stderr is inherited.
class ProcessTransport final : public Transport {
public:
    explicit ProcessTransport(const std::vector<std::string>& argv);
    ~ProcessTransport() override;  // closes stdin, then waits for the process
    ProcessTransport(const ProcessTransport&) = delete;
    ProcessTransport& operator=(const ProcessTransport&) = delete;
    void send(const json& message) override;
    json receive() override;

private:
    int pid_ = -1;
    int to_child_ = -1;
    struct Reader;
    std::unique_ptr<Reader> reader_;
};

struct ToolInfo {
    std::string name;
    std::string description;
    json input_schema;
};

struct CallResult {
    std::string text;  // concatenated text content
    json structured;   // structuredContent, if any
    bool is_error = false;
};

// MCP client that works with both eras of server: it probes with
// server/discover (modern) and falls back to the initialize handshake when the
// server answers with anything other than a modern result or error.
class McpClient {
public:
    McpClient(Transport& transport, Implementation client_info);

    void connect();
    const std::string& protocol_version() const { return version_; }
    bool modern() const { return modern_; }
    const json& server_info() const { return server_info_; }
    const std::string& instructions() const { return instructions_; }

    std::vector<ToolInfo> list_tools();
    CallResult call_tool(const std::string& name, const json& arguments);

private:
    json request(const std::string& method, json params);
    json request_once(const std::string& method, const json& params);

    Transport& transport_;
    Implementation client_info_;
    std::string version_;
    bool modern_ = false;
    json server_info_;
    std::string instructions_;
    long next_id_ = 1;
};

}  // namespace ll::mcp
