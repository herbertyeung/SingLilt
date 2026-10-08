#!/usr/bin/env python3
# Asynchronous recognition checks against a local fixture server.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

"""Run real Qt asynchronous-recognition scenarios against an isolated local server."""

from __future__ import annotations

import argparse
import base64
import http.server
import json
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import threading
import time


def response_score(title: str) -> dict:
    return {
        "title": title, "numberedLayout": "single", "tonic": 0, "bpm": 90, "beatsPerBar": 4, "beatUnit": 4,
        "notes": [
            {"degree": 1, "octave": 0, "accidental": 1, "durationTicks": 480,
             "measure": 0, "line": 0, "lyric": "fixture A\nfixture B",
             "verseLyrics": ["fixture A", "fixture B"], "confidence": 0.95,
             "keyOverride": 2, "tieToNext": True, "bbox": [8, 8, 16, 24]},
            {"degree": 1, "octave": 0, "accidental": 1, "durationTicks": 480,
             "measure": 0, "line": 0, "lyric": "", "verseLyrics": ["", ""],
             "confidence": 0.95, "keyOverride": -1, "tieToNext": False,
             "bbox": [32, 8, 16, 24]},
        ],
        "repeats": [],
    }


def run_scenario(executable: Path, directory: Path, scenario: str, env: dict, language: str) -> dict:
    directory.mkdir(parents=True, exist_ok=True)
    report_path = directory / "report.json"
    close_marker = directory / "close-requested"
    for path in (report_path, close_marker):
        path.unlink(missing_ok=True)
    for index in range(1, 4):
        for extension in ("received", "responded", "disconnected"):
            (directory / f"request-{index}.{extension}").unlink(missing_ok=True)
    requests: list[dict] = []
    requests_lock = threading.Lock()
    stopping = threading.Event()

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_args):
            pass

        def disconnected(self, record: dict, index: int) -> bool:
            if record.get("disconnected"):
                return True
            try:
                readable, _, _ = select.select([self.connection], [], [], 0)
                closed = bool(readable) and not self.connection.recv(1, socket.MSG_PEEK)
            except OSError:
                closed = True
            if closed:
                with requests_lock:
                    record["disconnected"] = True
                (directory / f"request-{index}.disconnected").write_text("closed\n", encoding="ascii")
            return closed

        def do_POST(self):
            record = {"path": self.path, "authorization_absent": "Authorization" not in self.headers,
                      "disconnected": False, "response_attempted": False}
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 8 * 1024 * 1024:
                    raise ValueError("unexpected fixture request size")
                body = json.loads(self.rfile.read(length))
                record["model"] = body.get("model")
                image_url = body["messages"][0]["content"][1]["image_url"]["url"]
                png = base64.b64decode(image_url.removeprefix("data:image/png;base64,"), validate=True)
                record["image_dimensions"] = list(struct.unpack_from(">II", png, 16))
            except (ValueError, KeyError, IndexError, TypeError, struct.error) as error:
                record["request_error"] = type(error).__name__
            with requests_lock:
                requests.append(record)
                index = len(requests)
            (directory / f"request-{index}.received").write_text("received\n", encoding="ascii")

            if scenario == "timeout":
                deadline = time.monotonic() + 38
                while not stopping.is_set() and time.monotonic() < deadline:
                    if self.disconnected(record, index):
                        break
                    stopping.wait(0.01)
                self.close_connection = True
                return

            delay = 5.0 if scenario == "close" else 0.15 if index > 1 else 1.5
            deadline = time.monotonic() + delay
            while not stopping.is_set() and time.monotonic() < deadline:
                self.disconnected(record, index)
                stopping.wait(0.01)
            if stopping.is_set():
                self.close_connection = True
                return
            status = 500 if scenario == "failure" else 200
            if status == 500:
                response = {"error": {"message": "Synthetic asynchronous recognition failure"}}
            else:
                title = "Async retry fixture" if scenario == "cancel" and index > 1 else "Async cloud fixture"
                response = {"choices": [{"finish_reason": "stop", "message": {
                    "content": json.dumps(response_score(title))}}]}
            encoded = json.dumps(response).encode("utf-8")
            with requests_lock:
                record["response_attempted"] = True
                record["response_status"] = status
            try:
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(encoded)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(encoded)
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                (directory / f"request-{index}.responded").write_text("attempted\n", encoding="ascii")
                self.close_connection = True

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    worker = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True)
    worker.start()
    command = [str(executable), "--async-recognition-check", scenario,
               "--vision-endpoint", f"http://127.0.0.1:{server.server_port}/v1/chat/completions",
               "--report", str(report_path)]
    if scenario != "baseline":
        command.extend(["--language", language])
    started = time.monotonic()
    process_error = None
    returncode = None
    completed_at = time.time()
    try:
        completed = subprocess.run(command, cwd=directory, env=env, capture_output=True,
                                   timeout=43 if scenario == "timeout" else 22, check=False)
        returncode = completed.returncode
        completed_at = time.time()
        if scenario in ("cancel", "close", "timeout"):
            deadline = time.monotonic() + 0.5
            while requests and not requests[0].get("disconnected") and time.monotonic() < deadline:
                stopping.wait(0.01)
    except (subprocess.TimeoutExpired, OSError) as error:
        process_error = type(error).__name__
        completed_at = time.time()
    finally:
        stopping.set()
        server.shutdown()
        server.server_close()
        worker.join(timeout=1)
    application = {}
    if report_path.is_file():
        try:
            application = json.loads(report_path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeError):
            pass
    checks = {
        "process_finished": process_error is None,
        "process_succeeded": returncode == 0,
        "application_checks_passed": application.get("passed") is True,
        "expected_request_count": len(requests) == (2 if scenario == "cancel" else 1),
        "loopback_requests_have_no_key": all(request.get("authorization_absent") for request in requests),
        "model_preserved": all(request.get("model") == "gpt-6.1-sol" for request in requests),
    }
    shutdown_seconds = None
    if scenario in ("cancel", "close", "timeout"):
        checks["client_disconnected"] = bool(requests) and requests[0].get("disconnected") is True
    if scenario in ("close", "timeout"):
        if close_marker.exists():
            shutdown_seconds = max(0.0, completed_at - close_marker.stat().st_mtime)
        checks["process_shutdown_under_2_seconds"] = shutdown_seconds is not None and shutdown_seconds < 2.0
    result = {
        "scenario": scenario, "passed": all(checks.values()), "checks": checks,
        "returncode": returncode, "process_error": process_error,
        "elapsed_ms": round((time.monotonic() - started) * 1000),
        "shutdown_seconds": shutdown_seconds, "application": application,
        "requests": requests, "command": command,
    }
    (directory / "scenario.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return result


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=Path("build/bin/Release/SingLilt.exe"))
    parser.add_argument("--output-dir", type=Path, default=Path("build/async-recognition/regression-Release"))
    parser.add_argument("--case", choices=("all", "quick", "baseline", "success", "failure", "cancel", "close", "timeout"),
                        default="all")
    parser.add_argument("--language", choices=("en_US", "zh_CN"), default="en_US")
    parser.add_argument("--platform", choices=("offscreen", "windows"), default="offscreen",
                        help="Use windows for native-font screenshots; offscreen is the default for state checks.")
    args = parser.parse_args()
    executable = (root / args.executable).resolve()
    output = (root / args.output_dir).resolve()
    if not executable.is_file():
        parser.error("The executable does not exist")
    if not output.is_relative_to((root / "build").resolve()):
        parser.error("Output must remain inside the repository build directory")
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.pop("OPENAI_API_KEY", None)
    for key in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"):
        env.pop(key, None)
    qt = root / "build/_deps/Qt/6.8.3/msvc2022_64"
    env["PATH"] = os.pathsep.join([str(executable.parent), str(root / "build/bin/Release"),
                                  str(qt / "bin"), env.get("PATH", "")])
    env["QT_PLUGIN_PATH"] = str(qt / "plugins")
    env["QT_QPA_PLATFORM"] = args.platform
    env["NO_PROXY"] = env["no_proxy"] = "127.0.0.1,localhost"
    scenarios = [args.case]
    if args.case in ("all", "quick"):
        scenarios = ["success", "failure", "cancel", "close"]
        if args.case == "all":
            scenarios.append("timeout")
    results = []
    for scenario in scenarios:
        result = run_scenario(executable, output / scenario, scenario, env, args.language)
        results.append(result)
        print(f"{'PASS' if result['passed'] else 'FAIL'} {scenario} exit={result['returncode']} "
              f"elapsed_ms={result['elapsed_ms']} shutdown_seconds={result['shutdown_seconds']}", flush=True)
    summary = {"passed": all(result["passed"] for result in results), "case_count": len(results),
               "passed_count": sum(result["passed"] for result in results),
               "real_api_key_used": False, "network_scope": "loopback fixture only", "results": results}
    (output / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"SUMMARY {summary['passed_count']}/{summary['case_count']}; report={output / 'summary.json'}")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
