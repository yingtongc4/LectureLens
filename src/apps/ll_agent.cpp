// ll-agent: answer a question about the course with Claude, using the tools of
// an MCP server (ll-mcp by default) that it launches over stdio.
//
//   ll-agent [options] "QUESTION"
//   ll-agent [--mcp CMD] --list-tools        connect and list tools (no API key needed)
// options:
//   --mcp "CMD ARGS"   server command (default: ll-mcp next to this binary)
//   --model ID         default claude-opus-5-5
//   --effort LEVEL     low | medium | high | xhigh | max (default medium)
//   --max-turns N      model calls per question (default 8)
//   --no-fallbacks     do not send fallbacks: "default"
//   --trace FILE       write the full message transcript as JSON
//
// Needs ANTHROPIC_API_KEY. Tool calls are printed to stderr, the answer to stdout.

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "agent/agent.hpp"
#include "agent/claude_client.hpp"
#include "mcp/client.hpp"

namespace {

std::vector<std::string> split_words(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

int usage() {
    std::cerr << "usage: ll-agent [--mcp CMD] [--model ID] [--effort LEVEL] [--max-turns N] [--no-fallbacks]\n"
                 "                [--trace FILE] QUESTION\n"
                 "       ll-agent [--mcp CMD] --list-tools\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);  // a dead server must surface as an error, not kill us
    const auto self_dir = std::filesystem::read_symlink("/proc/self/exe").parent_path();
    std::string mcp_cmd = (self_dir / "ll-mcp").string();
    std::string question, trace_file;
    bool list_only = false;
    ll::agent::AgentOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        const bool has_value = i + 1 < argc;
        if (s == "--mcp" && has_value) mcp_cmd = argv[++i];
        else if (s == "--model" && has_value) opts.model = argv[++i];
        else if (s == "--effort" && has_value) opts.effort = argv[++i];
        else if (s == "--max-turns" && has_value) opts.max_turns = std::stoi(argv[++i]);
        else if (s == "--trace" && has_value) trace_file = argv[++i];
        else if (s == "--no-fallbacks") opts.server_fallbacks = false;
        else if (s == "--list-tools") list_only = true;
        else if (!s.empty() && s[0] != '-' && question.empty()) question = s;
        else return usage();
    }
    if (question.empty() && !list_only) return usage();

    try {
        ll::mcp::ProcessTransport transport(split_words(mcp_cmd));
        ll::mcp::McpClient mcp(transport, {"ll-agent", "0.1.0"});
        mcp.connect();
        std::cerr << "connected to " << mcp.server_info().value("name", std::string("?")) << " (MCP "
                  << mcp.protocol_version() << (mcp.modern() ? ", stateless" : ", initialize handshake") << ")\n";
        if (list_only) {
            for (const auto& t : mcp.list_tools()) {
                std::cout << t.name << ": " << t.description << "\n  input: " << t.input_schema.dump() << "\n";
            }
            return 0;
        }

        ll::agent::ClaudeClient claude;
        ll::agent::Agent agent(claude, mcp, opts);
        const auto r = agent.ask(question, [](const ll::agent::ToolCall& c) {
            std::cerr << "  -> " << c.name << ' ' << c.input.dump() << std::fixed << std::setprecision(0) << "  ("
                      << c.ms << " ms" << (c.is_error ? ", error" : "") << ")\n";
        });
        std::cout << r.answer << '\n';
        std::cerr << "[" << opts.model << ", " << r.turns << " model calls, " << r.calls.size() << " tool calls, "
                  << r.input_tokens << " input / " << r.output_tokens << " output tokens, stop: " << r.stop_reason
                  << "]\n";
        if (!trace_file.empty()) std::ofstream(trace_file) << r.messages.dump(2) << '\n';
        return r.stop_reason == "end_turn" ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "ll-agent: " << e.what() << '\n';
        return 1;
    }
}
