#!/usr/bin/env bash
# Dev helper, run inside WSL. Sources live on the Windows drive; the build tree
# lives on the Linux filesystem, which is much faster to compile in.
#
#   scripts/dev.sh build     configure + build
#   scripts/dev.sh test      build + run unit tests
#   scripts/dev.sh up        start PostgreSQL + Qdrant and apply migrations
#   scripts/dev.sh down      stop them
#   scripts/dev.sh reset-db  drop all data and re-apply migrations
#   scripts/dev.sh ingest    ingest the private course PDFs
#   scripts/dev.sh ocr ...   run ll-ocr (see ll-ocr --help)
#   scripts/dev.sh psql      open a SQL shell
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${LL_BUILD_DIR:-$HOME/build/lecturelens}"
COMPOSE=(docker compose -f "$SRC/deploy/docker-compose.yml")
COURSE_DIR="${LL_COURSE_DIR:-/mnt/c/Users/ertja/Desktop/security_ppt}"

build() {
  cmake -S "$SRC" -B "$BUILD" -G Ninja ${LL_CMAKE_ARGS:-}
  cmake --build "$BUILD"
}

up() {
  "${COMPOSE[@]}" up -d --wait
  bash "$SRC/scripts/migrate.sh"
}

case "${1:-build}" in
  build)    build ;;
  test)     build; ctest --test-dir "$BUILD" --output-on-failure ;;
  up)       up ;;
  down)     "${COMPOSE[@]}" down ;;
  reset-db) "${COMPOSE[@]}" down -v; up ;;
  ingest)   build; up; "$BUILD/src/ll-ingest" "${@:2}" "$COURSE_DIR"/*.pdf ;;
  ocr)      build; up; "$BUILD/src/ll-ocr" --models "$SRC/models" "${@:2}" ;;
  psql)     "${COMPOSE[@]}" exec postgres psql -U lecturelens lecturelens ;;
  *)        sed -n '2,14p' "$0"; exit 2 ;;
esac
