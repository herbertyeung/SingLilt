# Application startup with only deployed Qt DLLs and plugins available.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys


def version_identity(executable):
    version_api = ctypes.WinDLL("version")
    version_api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version_api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    version_api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    version_api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p),
                                         ctypes.POINTER(wintypes.UINT)]
    size = version_api.GetFileVersionInfoSizeW(str(executable), None)
    if not size:
        raise RuntimeError("Executable version resource is missing")
    buffer = ctypes.create_string_buffer(size)
    if not version_api.GetFileVersionInfoW(str(executable), 0, size, buffer):
        raise RuntimeError("Cannot read executable version resource")
    pointer = ctypes.c_void_p()
    length = wintypes.UINT()
    if not version_api.VerQueryValueW(buffer, "\\", ctypes.byref(pointer), ctypes.byref(length)) or length.value < 52:
        raise RuntimeError("Invalid executable version resource")
    fields = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD * 13)).contents
    if fields[0] != 0xFEEF04BD:
        raise RuntimeError("Invalid executable version signature")
    product = (fields[4] >> 16, fields[4] & 0xFFFF, fields[5] >> 16, fields[5] & 0xFFFF)
    file_version = (fields[2] >> 16, fields[2] & 0xFFFF, fields[3] >> 16, fields[3] & 0xFFFF)
    return {"product": product, "file": file_version, "debug": bool(fields[7] & 1)}


def main():
    executable = Path(sys.argv[1]).resolve()
    version = sys.argv[2]
    report_path = Path(sys.argv[3])
    environment = dict(os.environ)
    for key in list(environment):
        if key.upper() in {"QTDIR", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM"}:
            del environment[key]
    system_root = Path(environment.get("SystemRoot", "C:/Windows"))
    environment["PATH"] = os.pathsep.join([str(system_root / "System32"), str(system_root)])
    configuration = executable.parent.name
    suffix = "d" if configuration == "Debug" else ""
    required = [f"Qt6{module}{suffix}.dll" for module in ("Core", "Gui", "Widgets", "Network")]
    required.append(f"platforms/qwindows{suffix}.dll")
    missing = [name for name in required if not (executable.parent / name).is_file()]
    # Suppress the Windows loader dialog so a missing DLL fails the test rather than hanging it.
    previous_error_mode = ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    try:
        process = subprocess.run(
            [str(executable), "--version", "--language", "en_US"],
            cwd=executable.parent,
            env=environment,
            capture_output=True,
            text=True,
            timeout=20,
        )
    finally:
        ctypes.windll.kernel32.SetErrorMode(previous_error_mode)
    expected = f"SingLilt {version}"
    identity = version_identity(executable)
    expected_numbers = tuple(int(part) for part in version.split(".")) + (0,)
    identity_matches = (identity["product"] == expected_numbers and identity["file"] == expected_numbers
                        and identity["debug"] == (configuration == "Debug"))
    passed = not missing and process.returncode == 0 and process.stdout.strip() == expected and identity_matches
    report = {
        "passed": passed,
        "configuration": configuration,
        "missingFiles": missing,
        "childExitCode": process.returncode,
        "stdout": process.stdout,
        "stderr": process.stderr,
        "path": environment["PATH"],
        "versionResource": identity,
        "versionMatches": identity_matches,
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if not passed:
        print(f"Runtime {configuration}: FAIL missing={len(missing)} child-exit={process.returncode}")
        return 3
    print(f"Runtime {configuration}: PASS isolated-PATH; {expected}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
