#!/usr/bin/env bash
# Applies migrations/*.sql in order, each once, each in its own transaction.
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
PSQL=(docker compose -f "$SRC/deploy/docker-compose.yml" exec -T postgres
      psql -U lecturelens -d lecturelens -v ON_ERROR_STOP=1 -q -t -A)

"${PSQL[@]}" -c "CREATE TABLE IF NOT EXISTS schema_migrations (
                   version TEXT PRIMARY KEY, applied_at TIMESTAMPTZ NOT NULL DEFAULT now())"

for f in "$SRC"/migrations/*.sql; do
  v="$(basename "$f" .sql)"
  if [[ -n "$("${PSQL[@]}" -c "SELECT 1 FROM schema_migrations WHERE version = '$v'")" ]]; then
    continue
  fi
  echo "applying $v"
  { echo "BEGIN;"; cat "$f"; echo "INSERT INTO schema_migrations (version) VALUES ('$v');"; echo "COMMIT;"; } \
    | "${PSQL[@]}"
done
