#!/usr/bin/env bash
# Cho bot vào một trận trên judge: build bot, dựng proxy TLS, rồi chơi.
#
#   ./play.sh --queue                  xếp hàng đấu đội THẬT rồi vào ngay
#   ./play.sh --join <MATCH_ID>        vào trận ĐÃ CÓ
#   ./play.sh --new [--days 4 ...]     tạo trận luyện tập mới với bot AI của BTC
#
# Chế độ --join/--queue lấy token từ $MATCH_TOKEN, không có thì $API_TOKEN (.env).
# Cố ý KHÔNG nhận token qua tham số dòng lệnh, để nó không lọt vào shell
# history. Bot mẫu vẫn nhận token qua argv nên nó vẫn hiện trong `ps` —
# giới hạn của main.cpp, không sửa được từ đây.
#
# Chế độ --new chuyển mọi tham số còn lại cho `python3 -m tools.live_match`.
#
# TRANSPORT=ws (mặc định cho --join/--queue) nối qua WebSocket: server đẩy
# day_state ngay khi ngày mở nên server đo response_ms = 6ms, thay vì ~274ms
# của HTTP polling. TRANSPORT=http quay lại proxy TLS cũ.
set -euo pipefail

cd "$(dirname "$0")"

PROXY_PORT="${PROXY_PORT:-8099}"
BOT="${BOT:-hexudon-bot-cpp/bot}"
TRANSPORT="${TRANSPORT:-ws}"

usage() {
  sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//' >&2
  exit 2
}

# 1. Nạp .env. `set -a` để export mọi biến cho tool Python đọc.
if [ -f .env ]; then
  set -a
  # shellcheck disable=SC1091
  . ./.env
  set +a
fi

: "${PROCON_URL:=http://localhost:8000}"
export PROCON_URL

mode=""
match_id=""
case "${1:-}" in
  --join)
    mode=join
    match_id="${2:-}"
    [ -n "$match_id" ] || usage
    shift 2
    [ $# -eq 0 ] || usage
    ;;
  --queue)
    mode=queue
    shift
    [ $# -eq 0 ] || usage
    ;;
  --new)
    mode=new
    shift
    ;;
  *) usage ;;
esac

if [ "$mode" = queue ] || [ "$mode" = join ]; then
  token="${MATCH_TOKEN:-${API_TOKEN:-}}"
  if [ -z "$token" ]; then
    echo "Thiếu token của bot. Đặt MATCH_TOKEN=... hoặc API_TOKEN=... trong .env." >&2
    echo "Trận queue dùng API_TOKEN cố định của đội (dạng 'bot-...')." >&2
    echo "Trận /practice thì dùng 'your_token' riêng của trận đó." >&2
    exit 1
  fi
else
  if [ -z "${PROCON_USERNAME:-}" ] || [ -z "${PROCON_PASSWORD:-}" ]; then
    echo "Thiếu PROCON_USERNAME/PROCON_PASSWORD — xem .env.example." >&2
    exit 1
  fi
fi

mkdir -p logs

# 2. Build bot. Makefile tự bỏ qua nếu binary đã mới hơn source.
make -C hexudon-bot-cpp

# 3. Bot mẫu không có TLS. Judge chạy https nên phải chèn proxy vào giữa;
#    mock local chạy http thì bot nối thẳng, không cần proxy.
upstream="${PROCON_URL#https://}"        # bỏ scheme
upstream="${upstream%%/*}"               # bỏ path
upstream="${upstream%%:*}"               # bỏ port

use_ws=0
if [ "$TRANSPORT" = ws ] && [ "$mode" != new ]; then use_ws=1; fi

if [ "$use_ws" = 1 ]; then
  bot_url="http://127.0.0.1:${PROXY_PORT}"
elif [ "${PROCON_URL#https://}" != "$PROCON_URL" ]; then
  bot_url="http://127.0.0.1:${PROXY_PORT}"

  python3 -m tools.tls_proxy --upstream "$upstream" --port "$PROXY_PORT" \
    >logs/tls-proxy.log 2>&1 &
  proxy_pid=$!
  trap 'kill "$proxy_pid" 2>/dev/null || true' EXIT

  # Đợi proxy nghe cổng, tối đa 5s — đừng sleep mù rồi bot chết vì connect sớm.
  for _ in $(seq 50); do
    if (exec 3<>"/dev/tcp/127.0.0.1/${PROXY_PORT}") 2>/dev/null; then
      exec 3>&-
      break
    fi
    if ! kill -0 "$proxy_pid" 2>/dev/null; then
      echo "proxy chết khi khởi động — xem logs/tls-proxy.log" >&2
      exit 1
    fi
    sleep 0.1
  done
else
  bot_url="$PROCON_URL"
fi

# Dựng cầu WS cho một match id. Phải gọi SAU khi biết match id vì id nằm
# trong URL WebSocket.
start_ws_bridge() {
  # Cổng phải TRỐNG trước đã. Nếu không kiểm, một proxy sót lại từ lần chạy
  # trước sẽ giữ cổng, vòng đợi bên dưới thấy "cổng đã mở" rồi báo thành công —
  # bot chạy qua proxy cũ trong khi cầu WS đã chết, và lỗi bị che hoàn toàn.
  if (exec 3<>"/dev/tcp/127.0.0.1/${PROXY_PORT}") 2>/dev/null; then
    exec 3>&-
    echo "cổng $PROXY_PORT đang bị chiếm — tắt tiến trình cũ hoặc đổi PROXY_PORT" >&2
    return 1
  fi

  MATCH_TOKEN="$token" python3 -m tools.ws_bridge \
    --match "$1" --upstream "$upstream" --port "$PROXY_PORT" \
    >logs/ws-bridge.log 2>&1 &
  bridge_pid=$!
  trap 'kill "$bridge_pid" 2>/dev/null || true' EXIT
  for _ in $(seq 50); do
    # Tiến trình còn sống mới tính; cổng mở mà tiến trình chết là cổng của kẻ khác.
    if ! kill -0 "$bridge_pid" 2>/dev/null; then
      echo "cầu WS chết khi khởi động — xem logs/ws-bridge.log" >&2
      sed -n '1,5p' logs/ws-bridge.log >&2
      return 1
    fi
    if (exec 3<>"/dev/tcp/127.0.0.1/${PROXY_PORT}") 2>/dev/null; then
      exec 3>&-
      return 0
    fi
    sleep 0.1
  done
  echo "cầu WS không mở được cổng $PROXY_PORT" >&2
  return 1
}

if [ "$mode" = queue ]; then
  # 4a. Xếp hàng ghép cặp. Queue ghép xong là trận CHẠY NGAY — chậm một nhịp
  #     là bot ăn E_STALE_DAY và mất trắng, nên proxy phải dựng SẴN ở trên rồi
  #     mới enqueue, và bot bật ngay khi có match id.
  match_id=$(python3 -m tools.queue_match)
  echo "đã ghép trận $match_id — vào ngay qua $bot_url (transport $TRANSPORT)"
  [ "$use_ws" = 1 ] && { start_ws_bridge "$match_id" || exit 1; }
  "$BOT" "$bot_url" "$match_id" "$token"
elif [ "$mode" = join ]; then
  # 4b. Vào thẳng trận đã có. main.cpp: argv = <URL> <MATCH_ID> <TOKEN>.
  echo "vào trận $match_id qua $bot_url (transport $TRANSPORT)"
  [ "$use_ws" = 1 ] && { start_ws_bridge "$match_id" || exit 1; }
  "$BOT" "$bot_url" "$match_id" "$token"
else
  # 4c. Tạo trận mới rồi chơi. live_match trả 2 nếu đội mình thu 0 loại udon.
  out="logs/match-$(date +%Y%m%d-%H%M%S).md"
  python3 -m tools.live_match \
    --bot "$BOT" \
    --bot-url "$bot_url" \
    --out "$out" \
    "$@"
fi
