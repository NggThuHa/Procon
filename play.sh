#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1091
  source .env
  set +a
fi

BASE="${BASE_URL:-${PROCON_URL:-http://127.0.0.1:8000}}"
MATCH="${1:-${MATCH_ID:-}}"
TOKEN="${2:-${API_TOKEN:-}}"
PROXY_HOST="${PROXY_HOST:-127.0.0.1}"
PROXY_PORT="${PROXY_PORT:-8099}"
BOT="./hexudon-bot-cpp/bot"
PROXY_PID=""

usage() {
  printf 'Usage: %s [MATCH_ID] [API_TOKEN]\n' "$0"
  printf 'Or set BASE_URL, MATCH_ID, API_TOKEN in .env\n'
}

cleanup() {
  if [[ -n "${PROXY_PID}" ]] && kill -0 "$PROXY_PID" 2>/dev/null; then
    kill "$PROXY_PID" 2>/dev/null || true
    wait "$PROXY_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if [[ -z "$MATCH" || "$MATCH" == "local-demo" ]]; then
  usage
  printf 'ERROR: missing real MATCH_ID, e.g. ./play.sh m-20212\n' >&2
  exit 2
fi

if [[ -z "$TOKEN" ]]; then
  usage
  printf 'ERROR: missing API_TOKEN. Put it in .env or pass as arg 2.\n' >&2
  exit 2
fi

make -C hexudon-bot-cpp

BOT_URL="$BASE"
if [[ "$BASE" == https://* ]]; then
  UPSTREAM="${BASE#https://}"
  UPSTREAM="${UPSTREAM%%/*}"
  UPSTREAM="${UPSTREAM%%:*}"
  printf 'Starting TLS proxy: http://%s:%s -> https://%s\n' "$PROXY_HOST" "$PROXY_PORT" "$UPSTREAM"
  python3 -m tools.tls_proxy --upstream "$UPSTREAM" --host "$PROXY_HOST" --port "$PROXY_PORT" &
  PROXY_PID="$!"
  sleep 0.4
  BOT_URL="http://${PROXY_HOST}:${PROXY_PORT}"
fi

printf 'Running bot: %s match=%s base=%s\n' "$BOT" "$MATCH" "$BOT_URL"
"$BOT" "$BOT_URL" "$MATCH" "$TOKEN"
