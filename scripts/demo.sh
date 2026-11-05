#!/usr/bin/env bash
# A short guided tour for a screen recording: search in two languages, a slide
# whose text exists only in an image, the MCP server, degraded mode, and the
# agent (when ANTHROPIC_API_KEY is set). Needs the stack up and data indexed.
#
#   bash scripts/demo.sh            pauses between steps (press Enter)
#   DEMO_PAUSE=0 bash scripts/demo.sh
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
B="${LL_BUILD_DIR:-$HOME/build/lecturelens}/src"
COMPOSE=(docker compose -f "$SRC/deploy/docker-compose.yml")
PORT=8083

step() {
  printf '\n\033[1;36m== %s\033[0m\n' "$1"
  if [[ "${DEMO_PAUSE:-1}" != 0 ]]; then read -r -p "(Enter) " _; fi
}
show() { printf '\033[2m$ %s\033[0m\n' "$*"; eval "$@"; }
search() {
  curl -s -X POST "localhost:$PORT/api/search" -H 'Content-Type: application/json' \
    -d "{\"query\": \"$1\", \"top_k\": 3}" | python3 -c '
import json, sys
r = json.load(sys.stdin)
if "degraded" in r: print("  (degraded: keyword-only)")
for x in r["results"]:
    print("  %s, p.%d  [%s, vector #%d, keyword #%d]"
          % (x["lecture"], x["page"], x["source"], x["vector_rank"], x["keyword_rank"]))
print("  %.0f ms" % sum(r["timings_ms"].values()))'
}

"${COMPOSE[@]}" up -d --wait >/dev/null 2>&1
"$B/ll-api" --port $PORT >/tmp/ll-demo-api.log 2>&1 &
API_PID=$!
trap 'kill $API_PID 2>/dev/null; "${COMPOSE[@]}" start qdrant >/dev/null 2>&1' EXIT
for _ in $(seq 1 60); do curl -sf "localhost:$PORT/health" >/dev/null && break; sleep 0.5; done

step "1. A Chinese question against English slides (vector search carries it)"
show search "'零知識證明是什麼'"

step "2. An English paraphrase (keyword and vector both contribute)"
show search "'spoof a UDP reply after sniffing the request'"

step "3. Page 172: the text layer is one title line; the code comes from OCR"
show "curl -s localhost:$PORT/api/pages/3/172 | python3 -c 'import json,sys; r=json.load(sys.stdin); print(\"text layer:\", repr(r[\"text_layer\"])); [print(\"  ocr:\", l[\"text\"]) for l in r[\"ocr_lines\"][:8]]'"

step "4. The same knowledge base as an MCP server"
show "$B/ll-agent --list-tools 2>&1 | grep -v '^  input'"

step "5. Qdrant goes down: search degrades instead of failing"
show "${COMPOSE[*]} stop qdrant 2>/dev/null"
show "curl -s localhost:$PORT/health; echo"
show search "'prepared statements against SQL injection'"
show "${COMPOSE[*]} start qdrant 2>/dev/null"

if [[ -n "${ANTHROPIC_API_KEY:-}" ]]; then
  step "6. The agent: Claude answers through the MCP tools, with citations"
  show "$B/ll-agent 'SQL injection 的根本原因是什麼？要怎麼防禦？'"
else
  step "6. (skipped: set ANTHROPIC_API_KEY to run the agent)"
fi
