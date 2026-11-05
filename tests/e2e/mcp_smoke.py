"""End-to-end check of ll-mcp over stdio, as an independent (non-C++) client.

    python3 -I tests/e2e/mcp_smoke.py ~/build/lecturelens/src/ll-mcp

Needs PostgreSQL and Qdrant running with indexed data. Exits non-zero on failure.
"""

import json
import subprocess
import sys
import time

MODERN = "2026-07-28"
META = {
    "io.modelcontextprotocol/protocolVersion": MODERN,
    "io.modelcontextprotocol/clientInfo": {"name": "mcp_smoke.py", "version": "1"},
    "io.modelcontextprotocol/clientCapabilities": {},
}


class Server:
    def __init__(self, argv):
        self.p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
        self.next_id = 1

    def send(self, msg):
        self.p.stdin.write(json.dumps(msg) + "\n")
        self.p.stdin.flush()

    def call(self, method, params=None, modern=True):
        params = dict(params or {})
        if modern:
            params["_meta"] = META
        rid = self.next_id
        self.next_id += 1
        t0 = time.perf_counter()
        self.send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        reply = json.loads(self.p.stdout.readline())
        assert reply["id"] == rid, reply
        return reply, (time.perf_counter() - t0) * 1000

    def close(self):
        self.p.stdin.close()
        return self.p.wait(timeout=10)


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        sys.exit(1)


def main():
    exe = sys.argv[1]

    print("== modern client (2026-07-28)")
    s = Server([exe])
    r, _ = s.call("server/discover")
    check(r["result"]["supportedVersions"][0] == MODERN, "server/discover lists 2026-07-28 first")
    r, _ = s.call("tools/list")
    names = [t["name"] for t in r["result"]["tools"]]
    check(names == ["search_lectures", "get_page", "list_lectures"], f"tools/list -> {names}")

    r, ms = s.call("tools/call", {"name": "search_lectures", "arguments": {"query": "零知識證明是什麼", "top_k": 3}})
    res = r["result"]["structuredContent"]["results"]
    check(not r["result"]["isError"] and len(res) == 3, f"Chinese query -> {len(res)} slides in {ms:.0f} ms")
    for x in res:
        print(f"       {x['lecture']} p.{x['page']} (doc {x['document_id']}, {x['source']})")
    check(any(x["lecture"] == "Digital signature" for x in res), "top results come from 'Digital signature'")

    first = res[0]
    r, _ = s.call("tools/call", {"name": "get_page", "arguments": {"document_id": first["document_id"], "page": first["page"]}})
    check(not r["result"]["isError"] and r["result"]["structuredContent"]["lecture"] == first["lecture"], "get_page reads that slide")

    r, _ = s.call("tools/call", {"name": "get_page", "arguments": {"document_id": 3, "page": 172}})
    sc = r["result"]["structuredContent"]
    check(len(sc["ocr_text"].splitlines()) > 10, f"p.172 has {len(sc['ocr_text'].splitlines())} OCR lines (code screenshot)")

    r, _ = s.call("tools/call", {"name": "get_page", "arguments": {"document_id": 3, "page": 99999}})
    check(r["result"]["isError"] and "no page" in r["result"]["content"][0]["text"], "missing page -> isError result")
    r, _ = s.call("tools/call", {"name": "search_lectures", "arguments": {"query": "x", "top_k": 500}})
    check(r["result"]["isError"], "top_k out of range -> isError result")
    r, _ = s.call("tools/call", {"name": "list_lectures", "arguments": {}})
    check(len(r["result"]["structuredContent"]["lectures"]) == 18, "list_lectures -> 18 lectures")

    meta = dict(META)
    meta["io.modelcontextprotocol/protocolVersion"] = "1900-01-01"
    s.send({"jsonrpc": "2.0", "id": 99, "method": "tools/list", "params": {"_meta": meta}})
    r = json.loads(s.p.stdout.readline())
    check(r["error"]["code"] == -32022, "unknown version -> -32022 UnsupportedProtocolVersion")
    check(s.close() == 0, "server exits cleanly on stdin EOF")

    print("== legacy client (initialize, 2025-06-18)")
    s = Server([exe])
    r, _ = s.call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                 "clientInfo": {"name": "legacy", "version": "1"}}, modern=False)
    check(r["result"]["protocolVersion"] == "2025-06-18", "initialize negotiates 2025-06-18")
    s.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    r, _ = s.call("tools/call", {"name": "search_lectures", "arguments": {"query": "buffer overflow canary"}}, modern=False)
    check(not r["result"]["isError"] and "resultType" not in r["result"], "legacy tools/call works, no modern fields")
    check(s.close() == 0, "server exits cleanly")


if __name__ == "__main__":
    main()
