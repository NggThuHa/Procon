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
MATCH=""
TOKEN=""
PROXY_HOST="${PROXY_HOST:-127.0.0.1}"
PROXY_PORT="${PROXY_PORT:-8099}"
BOT="./hexudon-bot-cpp/bot"
PROXY_PID=""

usage() {
  printf 'Usage: %s --join MATCH_ID\n' "$0"
  printf '   or: %s MATCH_ID [API_TOKEN]\n' "$0"
  printf 'Or set BASE_URL, MATCH_ID, API_TOKEN in .env\n'
}

case "${1:-}" in
  --join)
    if [[ $# -ne 2 || -z "${2:-}" ]]; then
      usage
      exit 2
    fi
    MATCH="$2"
    TOKEN="${MATCH_TOKEN:-${API_TOKEN:-}}"
    ;;
  --*)
    usage
    printf 'ERROR: unknown option: %s\n' "$1" >&2
    exit 2
    ;;
  *)
    if [[ $# -gt 2 ]]; then
      usage
      exit 2
    fi
    MATCH="${1:-${MATCH_ID:-}}"
    TOKEN="${2:-${MATCH_TOKEN:-${API_TOKEN:-}}}"
    ;;
esac

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

  # Chỉ chạy bot sau khi đúng tiến trình proxy đã mở cổng. Tránh hai lỗi khó
  # thấy: proxy chết vì cổng bị chiếm, hoặc bot khởi động trước proxy.
  proxy_ready=0
  for _ in $(seq 50); do
    if ! kill -0 "$PROXY_PID" 2>/dev/null; then
      printf 'ERROR: TLS proxy stopped during startup\n' >&2
      exit 1
    fi
    if (exec 3<>"/dev/tcp/${PROXY_HOST}/${PROXY_PORT}") 2>/dev/null; then
      exec 3>&-
      proxy_ready=1
      break
    fi
    sleep 0.1
  done
  if [[ "$proxy_ready" -ne 1 ]]; then
    printf 'ERROR: TLS proxy did not open %s:%s\n' "$PROXY_HOST" "$PROXY_PORT" >&2
    exit 1
  fi
  BOT_URL="http://${PROXY_HOST}:${PROXY_PORT}"
fi

printf 'Running bot: %s match=%s base=%s\n' "$BOT" "$MATCH" "$BOT_URL"
"$BOT" "$BOT_URL" "$MATCH" "$TOKEN"
