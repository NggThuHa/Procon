"""Run deterministic offline arena matches for the C++ bot.

The arena uses the repository's Simulator and a local HTTP-compatible mock
server, so it never needs credentials or network access.  The bot still runs
through its real setup/assignment/state/actions/result transport path.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from .errors import MatchError
from .simulator import Simulator


class MatchServer(ThreadingHTTPServer):
    def __init__(self, address, setup):
        super().__init__(address, MatchHandler)
        self.match = Simulator(setup)
        self.assignment = None
        self.actions_received = 0
        self.actions = []
        self.invalid_actions = []
        self.lock = threading.Lock()


class MatchHandler(BaseHTTPRequestHandler):
    server_version = "ProconArena/1.0"

    def log_message(self, fmt, *args):
        pass

    def _send_json(self, status, payload):
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self):
        try:
            size = int(self.headers.get("Content-Length", "0"))
            return json.loads(self.rfile.read(size))
        except (ValueError, json.JSONDecodeError):
            return None

    def do_GET(self):
        match = self.server.match
        if self.path.endswith("/setup"):
            self._send_json(200, match._setup)
        elif self.path.endswith("/state"):
            with self.server.lock:
                # The C++ client uses any non-200 state response as the
                # signal to query /result after the final action.
                if match.finished():
                    self._send_json(409, {"error": "finished"})
                else:
                    self._send_json(200, match.state())
        elif self.path.endswith("/result"):
            if not match.finished():
                self._send_json(409, {"error": "not finished"})
            else:
                self._send_json(200, {"standings": [match.score()]})
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self):
        payload = self._read_json()
        match = self.server.match
        if self.path.endswith("/assignment"):
            try:
                match.assign(payload)
                self.server.assignment = list(payload)
                self._send_json(200, {"valid": True})
            except (MatchError, TypeError, ValueError) as exc:
                self._send_json(200, {"valid": False, "reason": str(exc)})
        elif self.path.endswith("/actions"):
            with self.server.lock:
                try:
                    if self.server.assignment is None:
                        raise ValueError("assignment chưa được gửi")
                    match.step(payload)
                    self.server.actions_received += 1
                    self.server.actions.append(payload)
                    self._send_json(200, {"valid": True})
                except Exception as exc:
                    self.server.invalid_actions.append(str(exc))
                    self._send_json(200, {"valid": False, "reason": str(exc)})
        else:
            self._send_json(404, {"error": "not found"})


def run_bot(bot, server, match_id, timeout):
    base = f"http://127.0.0.1:{server.server_port}"
    process = subprocess.Popen(
        [str(bot), base, match_id, "local-test-token"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and process.poll() is None:
        if server.match.finished():
            try:
                process.wait(timeout=min(2, max(0.1, deadline - time.monotonic())))
            except subprocess.TimeoutExpired:
                break
        else:
            time.sleep(0.05)
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    stderr = process.stderr.read() if process.stderr else ""
    return process.returncode, stderr


def run_match(setup_path, bot, timeout):
    setup = json.loads(Path(setup_path).read_text(encoding="utf-8"))
    server = MatchServer(("127.0.0.1", 0), setup)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        return_code, stderr = run_bot(bot, server, "arena", timeout)
        return {
            "score": server.match.score(),
            "days": server.match.day,
            "expected_days": server.match.n_days,
            "positions": list(server.match.positions),
            "visited": server.match.visited,
            "actions_payloads": server.actions,
            "actions": server.actions_received,
            "invalid_actions": list(server.invalid_actions),
            "bot_returncode": return_code,
            "bot_stderr": stderr,
        }
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bot", default="hexudon-bot-cpp/bot")
    parser.add_argument("--map", dest="map_path", default="fixtures/default.json")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    bot = Path(args.bot)
    if not bot.is_file() or not os.access(bot, os.X_OK):
        parser.error(f"bot không executable: {bot}")
    result = run_match(args.map_path, bot, args.timeout)
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print(f"score={result['score']}")
        print(f"days={result['days']}/{result['expected_days']}, actions={result['actions']}")
        if result["invalid_actions"]:
            print("invalid_actions:")
            for error in result["invalid_actions"]:
                print(f"  {error}")
        if result["bot_returncode"] not in (0, 143, -15):
            print("bot stderr:", result["bot_stderr"], file=sys.stderr)
    return 0 if result["days"] == result["expected_days"] and not result["invalid_actions"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
