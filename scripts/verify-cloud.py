#!/usr/bin/env python3
# Vision-response validation against a local fixture server.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

"""Exercise the real recognition CLI against a loopback-only fake vision server.

No service, model download, API key, or third-party Python package is required.
The request prompt hash intentionally locks the existing 64x64 recognition prompt;
update it only after an intentional prompt change has been reviewed.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.server
import json
import os
from pathlib import Path
import struct
import subprocess
import threading
import time
import zlib


MODEL = "gpt-6.1-sol"
SYNTHETIC_SECRET = "fixture-only-key-7bf203-cloud-regression"
PROMPT_SHA256 = "e4f89fefbe5dee062744ff63476ac8d90fa2fa39b3cf78e2c265790569aa36c0"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def fixture_png() -> tuple[bytes, bytes]:
    pixels = bytearray([255] * (64 * 64 * 3))
    digit = ("00100", "01100", "00100", "00100", "00100", "00100", "01110")
    for row, line in enumerate(digit):
        for col, value in enumerate(line):
            if value == "1":
                for dy in range(4):
                    for dx in range(4):
                        offset = ((18 + row * 4 + dy) * 64 + 22 + col * 4 + dx) * 3
                        pixels[offset:offset + 3] = b"\0\0\0"

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    rows = b"".join(b"\0" + pixels[y * 192:(y + 1) * 192] for y in range(64))
    png = PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", 64, 64, 8, 2, 0, 0, 0))
    return png + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""), bytes(pixels)


def png_rgb(data: bytes) -> tuple[int, int, bytes]:
    """Decode Qt's small, non-interlaced 8-bit PNG without comparing compression."""
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("image_url is not a PNG")
    position, compressed, palette = 8, bytearray(), b""
    width = height = color = 0
    while position + 12 <= len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4:position + 8]
        body = data[position + 8:position + 8 + length]
        if len(body) != length or position + length + 12 > len(data):
            raise ValueError("incomplete PNG chunk")
        crc = struct.unpack_from(">I", data, position + 8 + length)[0]
        if crc != zlib.crc32(kind + body):
            raise ValueError("invalid PNG chunk checksum")
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace or (width, height) != (64, 64):
                raise ValueError("unexpected PNG dimensions or encoding")
        elif kind == b"PLTE":
            palette = body
        elif kind == b"IDAT":
            compressed.extend(body)
        elif kind == b"IEND":
            break
        position += length + 12
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    stride = width * channels
    raw = zlib.decompress(compressed)
    if len(raw) != height * (stride + 1):
        raise ValueError("unexpected PNG scanline length")
    previous = bytearray(stride)
    pixels = bytearray()
    for y in range(height):
        mode = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            above = previous[x]
            upper_left = previous[x - channels] if x >= channels else 0
            if mode == 0:
                predictor = 0
            elif mode == 1:
                predictor = left
            elif mode == 2:
                predictor = above
            elif mode == 3:
                predictor = (left + above) // 2
            elif mode == 4:
                p = left + above - upper_left
                distances = (abs(p - left), abs(p - above), abs(p - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            else:
                raise ValueError("unknown PNG row filter")
            row[x] = (row[x] + predictor) & 255
        for x in range(width):
            offset = x * channels
            if color in (0, 4):
                pixels.extend([row[offset]] * 3)
            elif color == 3:
                start = row[offset] * 3
                pixels.extend(palette[start:start + 3])
            else:
                pixels.extend(row[offset:offset + 3])
            if color in (4, 6) and row[offset + channels - 1] != 255:
                raise ValueError("PNG opacity changed")
        previous = row
    return width, height, bytes(pixels)


def valid_score() -> dict:
    return {
        "title": "Cloud regression fixture", "tonic": 0, "bpm": 90,
        "beatsPerBar": 4, "beatUnit": 4,
        "notes": [{"degree": 1, "octave": 0, "accidental": 0, "durationTicks": 480,
                   "measure": 0, "line": 0, "lyric": "", "verseLyrics": [],
                   "confidence": 1.0, "keyOverride": -1, "tieToNext": False,
                   "bbox": [22, 18, 20, 28]}],
        "repeats": [],
    }


def response_for(mode: str) -> tuple[int, bytes]:
    if mode == "redirect":
        return 302, b'{"error":{"message":"fixture redirect"}}'
    if mode == "secret_echo":
        return 401, json.dumps({"error": {"message": f"Rejected key {SYNTHETIC_SECRET}."}}).encode("utf-8")
    if mode in ("http401", "http404"):
        return int(mode[-3:]), b'{"error":{"message":"fixture rejection"}}'
    if mode == "invalid_envelope":
        return 200, b'{"choices":'
    if mode == "no_choices":
        return 200, b'{"choices":[]}'
    score = valid_score()
    if mode == "outside_image":
        score["notes"][0]["bbox"] = [60, 18, 20, 28]
    if mode == "invalid_score":
        score["notes"][0]["degree"] = 9
    content = json.dumps(score)
    if mode == "no_score_json":
        content = "The fixture returned no score object."
    elif mode == "malformed_score_json":
        content = "{not valid JSON}"
    response = {"choices": [{"finish_reason": "length" if mode == "truncated" else "stop",
                             "message": {"role": "assistant", "content": content}}]}
    if mode == "oversized":
        # A valid score with surplus data must fail because of size, not parsing.
        response["padding"] = "x" * (8 * 1024 * 1024 + 1024)
    return 200, json.dumps(response).encode("utf-8")


def validate_request(body: bytes, expected_pixels: bytes) -> dict:
    request = json.loads(body)
    if not isinstance(request, dict):
        raise ValueError("request is not a JSON object")
    messages = request.get("messages", [])
    content = messages[0]["content"] if len(messages) == 1 else []
    text = content[0].get("text", "") if len(content) == 2 else ""
    image = content[1].get("image_url", {}) if len(content) == 2 else {}
    url = image.get("url", "")
    png = base64.b64decode(url.removeprefix("data:image/png;base64,"), validate=True)
    width, height, uploaded_pixels = png_rgb(png)
    prompt_hash = hashlib.sha256(text.encode("utf-8")).hexdigest()
    return {
        "model": request.get("model"), "model_preserved": request.get("model") == MODEL,
        "prompt_sha256": prompt_hash, "prompt_preserved": prompt_hash == PROMPT_SHA256,
        "image_dimensions": [width, height],
        "image_pixels_preserved": uploaded_pixels == expected_pixels,
        "image_is_png": url.startswith("data:image/png;base64,"),
        "image_detail_preserved": image.get("detail") == "high",
        "message_shape_preserved": len(content) == 2 and messages[0].get("role") == "user"
                                   and content[0].get("type") == "text"
                                   and content[1].get("type") == "image_url",
        "response_format_preserved": request.get("response_format") == {"type": "json_object"},
        "token_limit_preserved": request.get("max_completion_tokens") == 24000,
    }


def run_case(executable: Path, directory: Path, image: Path, pixels: bytes,
             env: dict, name: str, suffix: str, expected_path: str, mode: str) -> dict:
    events: list[dict] = []
    release_response = threading.Event()
    invalid_timeout = mode.startswith("invalid_timeout_")
    timeout_override = "30" if mode == "timeout" else mode.rsplit("_", 1)[-1] if invalid_timeout else None

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_args):
            pass  # Never log headers, payloads, or authentication information.

        def do_GET(self):
            events.append({"method": "GET", "path": self.path})
            self.send_response(405)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True

        def do_POST(self):
            event = {"method": "POST", "path": self.path,
                     "authorization_absent": "Authorization" not in self.headers,
                     "content_type_json": self.headers.get_content_type() == "application/json"}
            if mode == "secret_echo":
                event["authorization_matches_synthetic_fixture"] = self.headers.get("Authorization") == "Bearer " + SYNTHETIC_SECRET
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 2 * 1024 * 1024:
                    raise ValueError("unexpected request length")
                body = self.rfile.read(length)
                event.update(validate_request(body, pixels))
            except (ValueError, KeyError, IndexError, TypeError, AttributeError, struct.error, zlib.error) as error:
                event["validation_error"] = type(error).__name__
            events.append(event)
            if mode == "timeout" and self.path == expected_path:
                # Accept the whole upload, then keep the connection open without
                # sending even HTTP headers. The application must end this wait.
                event["response_withheld"] = True
                release_response.wait(timeout=45)
                self.close_connection = True
                return
            status, response = response_for(mode) if self.path == expected_path else (404, b'{"error":"wrong fixture route"}')
            event["response_status"] = status
            event["response_size_bytes"] = len(response)
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(response)))
            self.send_header("Connection", "close")
            if mode == "redirect" and self.path == expected_path:
                self.send_header("Location", f"http://127.0.0.1:{self.server.server_port}/redirect-target")
            self.end_headers()
            try:
                self.wfile.write(response)
            except (BrokenPipeError, ConnectionResetError):
                pass
            self.close_connection = True

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True)
    thread.start()
    endpoint = f"http://127.0.0.1:{server.server_port}{suffix}"
    report_path = directory / f"{name}-app-report.json"
    project_path = directory / f"{name}.jpp"
    # Prevent a prior successful run from masking a failure before a new report.
    for path in (report_path, project_path):
        path.unlink(missing_ok=True)
    command = [str(executable), "--recognize", str(image), "--vision-endpoint", endpoint,
               "--vision-model", MODEL, "--language", "en_US",
               "--out", str(project_path), "--report", str(report_path)]
    if timeout_override is not None:
        command.extend(["--vision-timeout", timeout_override])
    start = time.monotonic()
    timed_out = False
    launch_error = None
    captured_output = b""
    child_env = env.copy()
    if mode == "secret_echo":
        child_env["OPENAI_API_KEY"] = SYNTHETIC_SECRET
    try:
        # subprocess.run waits for a Windows GUI-subsystem executable to really exit.
        completed = subprocess.run(command, env=child_env, cwd=directory, capture_output=True,
                                   timeout=40 if mode == "timeout" else 25, check=False)
        returncode = completed.returncode
        captured_output = completed.stdout + completed.stderr
    except subprocess.TimeoutExpired:
        timed_out, returncode = True, None
    except OSError as error:
        launch_error, returncode = type(error).__name__, None
    finally:
        release_response.set()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    report = {}
    if report_path.is_file():
        try:
            parsed = json.loads(report_path.read_text(encoding="utf-8"))
            report = parsed if isinstance(parsed, dict) else {}
        except (UnicodeError, json.JSONDecodeError):
            pass
    success_case = mode == "success"
    elapsed_seconds = time.monotonic() - start
    checks = {"process_finished": not timed_out and launch_error is None}
    if invalid_timeout:
        checks["no_request_for_invalid_timeout"] = len(events) == 0
    else:
        checks.update(one_POST_no_retry=len(events) == 1 and events[0].get("method") == "POST",
                      normalized_or_preserved_path=len(events) == 1 and events[0].get("path") == expected_path)
        for key in ("content_type_json", "model_preserved", "prompt_preserved",
                    "image_pixels_preserved", "image_is_png", "image_detail_preserved",
                    "message_shape_preserved", "response_format_preserved", "token_limit_preserved"):
            checks[key] = len(events) == 1 and events[0].get(key) is True
        auth_key = "authorization_matches_synthetic_fixture" if mode == "secret_echo" else "authorization_absent"
        checks[auth_key] = len(events) == 1 and events[0].get(auth_key) is True
    if success_case:
        checks.update(exit_success=returncode == 0, one_note=report.get("noteCount") == 1,
                      valid_timeline=report.get("validTimeline") is True,
                      saved_project=project_path.is_file(), no_error="error" not in report)
    else:
        checks.update(exit_failure=returncode not in (None, 0),
                      error_reported=isinstance(report.get("error"), str) and bool(report["error"]),
                      no_project_saved=not project_path.exists())
        if mode in ("http401", "http404"):
            checks["http_status_reported"] = mode[-3:] in str(report.get("error", ""))
        elif mode == "redirect":
            checks["redirect_status_reported"] = "302" in str(report.get("error", ""))
        elif mode == "secret_echo":
            checks["http_status_reported"] = "401" in str(report.get("error", ""))
            checks["secret_redacted_from_report"] = SYNTHETIC_SECRET not in json.dumps(report, ensure_ascii=False)
            checks["secret_redacted_from_process_output"] = SYNTHETIC_SECRET.encode() not in captured_output
        elif mode == "oversized":
            checks["oversized_response_sent"] = len(events) == 1 and events[0].get("response_size_bytes", 0) > 8 * 1024 * 1024
            checks["size_limit_reported"] = "8 MiB" in str(report.get("error", ""))
        elif mode == "timeout":
            message = str(report.get("error", "")).lower()
            checks["exit_is_one"] = returncode == 1
            checks["deadline_28_to_38_seconds"] = 28 <= elapsed_seconds <= 38
            checks["timeout_30_reported"] = "30" in message and ("timeout" in message or "timed out" in message)
            checks["server_did_not_respond"] = len(events) == 1 and events[0].get("response_withheld") is True
        elif invalid_timeout:
            checks["exit_is_one"] = returncode == 1
    return {"name": name, "passed": all(checks.values()), "checks": checks,
            "returncode": returncode, "elapsed_ms": round(elapsed_seconds * 1000),
            "launch_error": launch_error,
            "vision_timeout_override": timeout_override,
            "expected_path": expected_path, "requests": events,
            "application_error": str(report.get("error", "")).replace(SYNTHETIC_SECRET, "<synthetic-secret>")[:2000],
            "report_path": str(report_path), "command": command}


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=Path("build/bin/Release/SingLilt.exe"),
                        help="Executable path; relative paths are resolved from the repository root.")
    parser.add_argument("--output-dir", type=Path, default=Path("build/cloud-fix/regression-Release"),
                        help="Output directory inside build/; relative paths are resolved from the repository root.")
    parser.add_argument("--case", choices=("base", "timeout", "all"), default="all",
                        help="all includes one bounded 30-second application-deadline check.")
    args = parser.parse_args()
    executable = (args.executable if args.executable.is_absolute() else root / args.executable).resolve()
    if not executable.is_file():
        parser.error("--executable must name the actual SingLilt executable")
    directory = (args.output_dir if args.output_dir.is_absolute() else root / args.output_dir).resolve()
    if not directory.is_relative_to((root / "build").resolve()):
        parser.error("--output-dir must resolve inside the repository build directory")
    directory.mkdir(parents=True, exist_ok=True)
    png, pixels = fixture_png()
    image = directory / "fixture-score.png"
    image.write_bytes(png)
    qt = root / "build/_deps/Qt/6.8.3/msvc2022_64"
    env = os.environ.copy()
    env.pop("OPENAI_API_KEY", None)
    for key in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"):
        env.pop(key, None)
    env["NO_PROXY"] = env["no_proxy"] = "127.0.0.1,localhost"
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_PLUGIN_PATH"] = str(qt / "plugins")
    env["PATH"] = os.pathsep.join([str(executable.parent), str(root / "build/bin/Release"),
                                  str(qt / "bin"), env.get("PATH", "")])
    expected = "/v1/chat/completions"
    cases = [("base", "", expected, "success")]
    if args.case == "timeout":
        cases = [("timeout", expected, expected, "timeout")]
    if args.case == "all":
        cases += [("slash", "/", expected, "success"), ("v1", "/v1", expected, "success"),
                  ("v1_slash", "/v1/", expected, "success"),
                  ("full_path", expected, expected, "success"),
                  ("custom_path", "/custom/completions", "/custom/completions", "success"),
                  ("custom_query", "/custom/completions?fixture=1", "/custom/completions?fixture=1", "success")]
        cases += [(mode, expected, expected, mode) for mode in
                  ("http401", "http404", "invalid_envelope", "no_choices", "no_score_json",
                   "malformed_score_json", "truncated", "outside_image", "invalid_score", "redirect", "secret_echo")]
        cases.append(("oversized", expected, expected, "oversized"))
        cases.append(("timeout", expected, expected, "timeout"))
        cases += [("invalid_timeout_" + value, expected, expected, "invalid_timeout_" + value)
                  for value in ("oops", "29", "1801")]
    results = []
    for name, suffix, path, mode in cases:
        result = run_case(executable, directory, image, pixels, env, name, suffix, path, mode)
        results.append(result)
        failed = [key for key, ok in result["checks"].items() if not ok]
        print(f"{'PASS' if result['passed'] else 'FAIL'} {name} exit={result['returncode']}"
              + (f" checks={','.join(failed)}" if failed else ""), flush=True)
    summary = {"passed": all(result["passed"] for result in results), "case_count": len(results),
               "passed_count": sum(result["passed"] for result in results),
               "network_scope": "random-port loopback fake server only", "real_api_key_used": False,
               "synthetic_key_case": args.case == "all",
               "fixture_png_sha256": hashlib.sha256(png).hexdigest(), "results": results}
    summary_path = directory / "summary.json"
    summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"SUMMARY {summary['passed_count']}/{len(results)} passed; report={summary_path}")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
