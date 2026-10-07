# Relocated runtime checks, independent of the development Qt installation.
# Copyright (c) 2026 Herbert Yeung
# SPDX-License-Identifier: MIT

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    prefix = Path(sys.argv[1]).resolve()
    launcher = prefix / "bin/singlilt"
    candidates = [path for path in prefix.rglob("SingLilt") if path.is_file()]
    if len(candidates) != 1:
        raise AssertionError(f"Expected one SingLilt executable, found {len(candidates)}")
    executable = candidates[0]
    qt_libraries = executable.parent / "qt/lib"
    environment = dict(os.environ)
    for key in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QTDIR", "QML2_IMPORT_PATH"):
        environment.pop(key, None)
    environment["QT_QPA_PLATFORM"] = "offscreen"

    def run(command):
        return subprocess.run(command, env=environment, capture_output=True, text=True,
                              encoding="utf-8", timeout=30, check=True).stdout

    metadata = run(["readelf", "-d", str(executable)])
    runpath = re.search(r"\((?:RUNPATH|RPATH)\).*\[(.*?)\]", metadata)
    if not runpath or runpath.group(1) != "$ORIGIN/qt/lib":
        raise AssertionError(f"Expected package-relative RPATH: {metadata}")
    for library in ("libQt6Core.so.6", "libQt6Widgets.so.6", "libQt6Multimedia.so.6"):
        if f"[{library}]" not in metadata:
            raise AssertionError(f"Missing expected NEEDED entry: {library}")
    dependencies = run(["ldd", str(executable)])
    for line in dependencies.splitlines():
        match = re.search(r"(libQt6\S+|libicu\S+|libav(?:codec|format|util)\S+|libsw(?:resample|scale)\S+) => (.*?) \(", line)
        if match and not Path(match.group(2)).resolve().is_relative_to(qt_libraries.resolve()):
            raise AssertionError(f"Runtime escaped the package: {line}")
    if "not found" in dependencies:
        raise AssertionError(dependencies)
    if not run([str(launcher), "--version", "--language", "en_US"]).strip().startswith("SingLilt "):
        raise AssertionError("Installed version command failed")
    run([str(launcher), "--catalog-check", "--language", "en_US"])
    with tempfile.TemporaryDirectory(prefix="singlilt link ") as directory:
        alias = Path(directory) / "linked singlilt"
        alias.symlink_to(launcher)
        run([str(alias), "--version", "--language", "en_US"])
        run([str(alias), "--catalog-check", "--language", "en_US"])
    print("Linux package checks: PASS relative-RPATH bundled-Qt symlink startup/catalog")


if __name__ == "__main__":
    main()
