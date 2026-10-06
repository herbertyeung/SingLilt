# Public CLI, practice round-trip, and report-failure regressions.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

import json
from pathlib import Path
import subprocess
import sys


def main():
    executable = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    expected_version = f"SingLilt {sys.argv[3]}"
    output.mkdir(parents=True, exist_ok=True)
    checks = []

    def run(arguments, expected_exit=0):
        process = subprocess.run(
            [str(executable), *arguments], capture_output=True, text=True,
            encoding="utf-8", timeout=15,
        )
        if process.returncode != expected_exit:
            raise AssertionError(f"{arguments}: exit={process.returncode}, stderr={process.stderr}")
        return process

    for flag in ("--version", "-v"):
        for language in ("en_US", "zh_CN"):
            process = run([flag, "--language", language])
            if process.stdout.strip() != expected_version:
                raise AssertionError(process.stdout)
            checks.append(f"{flag} {language}")

    help_output = run(["--help", "--language", "en_US"]).stdout
    if "SingLilt" not in help_output or "--render-wave" not in help_output:
        raise AssertionError("Help is missing public commands.")
    checks.append("help")

    project = output / "practice.jpp"
    report = output / "practice.json"
    run(["--generate-accompaniment", "--out", str(project), "--report", str(report)])
    run(["--inspect", str(project), "--report", str(report)])
    parsed = json.loads(report.read_text(encoding="utf-8"))
    if not parsed["valid"] or parsed["notes"] <= 0:
        raise AssertionError("Default practice project did not round-trip.")
    checks.append("authored practice round-trip")

    process = run(["--inspect", str(output / "missing.jpp"), "--report", str(output)], expected_exit=1)
    if not process.stderr:
        raise AssertionError("Report failure was silent.")
    checks.append("missing input and unwritable report return failure")
    (output / "report.json").write_text(json.dumps({"passed": True, "checks": checks}, indent=2), encoding="utf-8")
    print(f"CLI tests: {len(checks)}/{len(checks)} passed")


if __name__ == "__main__":
    main()
