#!/usr/bin/env bash
# Failure drills: stop PostgreSQL or Qdrant under the running system and check
# that it degrades, retries and recovers as designed. Uses the docker-compose
# stack and the indexed course data; leaves both services running at the end.
#
#   bash tests/e2e/failure_drills.sh
set -uo pipefail

SRC="$(cd "$(dirname "$0")/../.." && pwd)"
B="${LL_BUILD_DIR:-$HOME/build/lecturelens}/src"
COMPOSE=(docker compose -f "$SRC/deploy/docker-compose.yml")
PORT=8082
PASS=0
FAIL=0

psql_q() { "${COMPOSE[@]}" exec -T postgres psql -U lecturelens -d lecturelens -tAc "$1"; }
check() {
  if eval "$2"; then echo "ok   $1"; PASS=$((PASS + 1)); else echo "FAIL $1"; FAIL=$((FAIL + 1)); fi
}
api_up() { for _ in $(seq 1 60); do curl -s -o /dev/null "localhost:$PORT/health" && return 0; sleep 0.5; done; return 1; }
search() { curl -s -X POST "localhost:$PORT/api/search" -H 'Content-Type: application/json' -d "{\"query\": \"$1\", \"top_k\": 3}"; }
cleanup() {
  [[ -n "${API_PID:-}" ]] && kill "$API_PID" 2>/dev/null
  [[ -n "${IDX_PID:-}" ]] && kill "$IDX_PID" 2>/dev/null
  "${COMPOSE[@]}" start postgres qdrant >/dev/null 2>&1
}
trap cleanup EXIT

"${COMPOSE[@]}" up -d --wait >/dev/null 2>&1
POINTS=$(curl -s localhost:6333/collections/chunks | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['points_count'])")
echo "baseline: $POINTS points in Qdrant"

echo "== 1. Qdrant down: search degrades to keyword-only, API stays up"
"$B/ll-api" --port $PORT >/tmp/ll-drill-api.log 2>&1 &
API_PID=$!
api_up
"${COMPOSE[@]}" stop qdrant >/dev/null 2>&1
H=$(curl -s -w ' %{http_code}' "localhost:$PORT/health")
check "health reports degraded with HTTP 200 ($H)" '[[ "$H" == *"\"degraded\""* && "$H" == *" 200" ]]'
R=$(search "prepared statements against SQL injection")
check "English query still returns keyword results" \
  '[[ $(echo "$R" | python3 -c "import sys,json; r=json.load(sys.stdin); print(len(r[\"results\"]) > 0 and \"vector\" in r[\"degraded\"])") == True ]]'
T0=$(date +%s%N)
search "零知識證明是什麼" >/dev/null
MS=$(( ($(date +%s%N) - T0) / 1000000 ))
check "Chinese query answers fast instead of hanging (${MS} ms)" '[[ $MS -lt 3000 ]]'
"${COMPOSE[@]}" start qdrant >/dev/null 2>&1
for _ in $(seq 1 30); do curl -sf localhost:6333/readyz >/dev/null && break; sleep 1; done
H=$(curl -s "localhost:$PORT/health")
check "health back to ok after Qdrant restarts" '[[ "$H" == *"\"ok\""* ]]'
R=$(search "零知識證明是什麼")
check "vector results return without restarting the API" \
  '[[ $(echo "$R" | python3 -c "import sys,json; r=json.load(sys.stdin); print(\"degraded\" not in r and r[\"results\"][0][\"lecture\"])") == "Digital signature" ]]'

pending() { psql_q "SELECT count(*) FROM outbox_events WHERE processed_at IS NULL AND NOT dead"; }
wait_for() { for _ in $(seq 1 "$1"); do eval "$2" && return 0; sleep 1; done; return 1; }
qdrant_ready() { wait_for 30 'curl -sf localhost:6333/readyz >/dev/null'; }

echo "== 2. Qdrant fails mid-run: events retried with backoff, nothing lost"
"$B/ll-indexer" >/tmp/ll-drill-daemon.log 2>&1 &   # resident; starts while Qdrant is up
IDX_PID=$!
wait_for 30 'grep -q "" /tmp/ll-drill-daemon.log || [[ $(pending) == 0 ]]'
sleep 5  # model loaded, collection checked
"${COMPOSE[@]}" stop qdrant >/dev/null 2>&1
psql_q "INSERT INTO outbox_events (aggregate, aggregate_id, version, op)
        SELECT 'chunk', id, version, 'upsert' FROM chunks ORDER BY id LIMIT 20" >/dev/null
wait_for 30 '[[ $(psql_q "SELECT count(*) FROM outbox_events WHERE processed_at IS NULL AND attempts >= 1") == 20 ]]'
S=$(psql_q "SELECT count(*) FROM outbox_events WHERE processed_at IS NULL AND attempts >= 1
              AND next_attempt_at > now() - interval '1 second' AND last_error LIKE 'qdrant%'")
check "20 events rescheduled with attempts, backoff and the error kept ($S)" '[[ "$S" == 20 ]]'
check "indexer keeps running through the outage" 'kill -0 $IDX_PID'
"${COMPOSE[@]}" start qdrant >/dev/null 2>&1
qdrant_ready
wait_for 90 '[[ $(pending) == 0 ]]'
check "backlog drains by itself once Qdrant is back" '[[ $(pending) == 0 ]]'
NOW=$(curl -s localhost:6333/collections/chunks | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['points_count'])")
check "re-delivered upserts are idempotent ($NOW points)" '[[ "$NOW" == "$POINTS" ]]'

echo "== 3. Repeated failures: dead letter, then operator replay"
"${COMPOSE[@]}" stop qdrant >/dev/null 2>&1
psql_q "INSERT INTO outbox_events (aggregate, aggregate_id, version, op, attempts)
        SELECT 'chunk', id, version, 'upsert', 7 FROM chunks ORDER BY id LIMIT 5" >/dev/null
wait_for 30 '[[ $(psql_q "SELECT count(*) FROM outbox_events WHERE dead") == 5 ]]'
check "8th failure moves events to the dead letter" '[[ $(psql_q "SELECT count(*) FROM outbox_events WHERE dead") == 5 ]]'
"${COMPOSE[@]}" start qdrant >/dev/null 2>&1
qdrant_ready
sleep 6
check "dead events are not retried automatically" '[[ $(psql_q "SELECT count(*) FROM outbox_events WHERE dead AND processed_at IS NULL") == 5 ]]'
psql_q "UPDATE outbox_events SET dead = false, attempts = 0, next_attempt_at = now() WHERE dead" >/dev/null
wait_for 30 '[[ $(pending) == 0 ]]'
check "replayed dead letters are processed" '[[ $(pending) == 0 ]]'

echo "== 4. PostgreSQL down: API reports 503, resident indexer waits and recovers"
"${COMPOSE[@]}" stop postgres >/dev/null 2>&1
C=$(curl -s -o /dev/null -w '%{http_code}' "localhost:$PORT/health")
check "health returns 503 when PostgreSQL is down" '[[ "$C" == 503 ]]'
C=$(curl -s -o /tmp/ll-drill-body.json -w '%{http_code}' -X POST "localhost:$PORT/api/search" -d '{"query": "x"}')
check "search fails with a JSON 500, not a crash" '[[ "$C" == 500 ]] && python3 -c "import json; json.load(open(\"/tmp/ll-drill-body.json\"))[\"error\"]"'
sleep 6
check "indexer is still running and backing off" 'kill -0 $IDX_PID && grep -q "retrying in" /tmp/ll-drill-daemon.log'
"${COMPOSE[@]}" start postgres >/dev/null 2>&1
"${COMPOSE[@]}" up -d --wait >/dev/null 2>&1
psql_q "INSERT INTO outbox_events (aggregate, aggregate_id, version, op)
        SELECT 'chunk', id, version, 'upsert' FROM chunks ORDER BY id LIMIT 10" >/dev/null
for _ in $(seq 1 60); do [[ $(psql_q "SELECT count(*) FROM outbox_events WHERE processed_at IS NULL") == 0 ]] && break; sleep 1; done
check "indexer picks up new events after PostgreSQL returns" '[[ $(psql_q "SELECT count(*) FROM outbox_events WHERE processed_at IS NULL") == 0 ]]'
C=$(curl -s -o /dev/null -w '%{http_code}' "localhost:$PORT/health")
check "API recovers without a restart" '[[ "$C" == 200 ]]'

echo
echo "$PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
