# Language-pack installation without rebuilding the application executable.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    executable = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    directory = executable.parent / "language"
    config = json.loads((directory / "config.json").read_text(encoding="utf-8"))
    pack = directory / "fr_FR"
    if pack.exists():
        raise RuntimeError("External test locale already exists; existing packs are not overwritten.")
    before = hashlib.sha256(executable.read_bytes()).hexdigest()
    shutil.copytree(directory / config["fallbackLocale"], pack)
    try:
        for path in pack.glob("*.json"):
            catalog = json.loads(path.read_text(encoding="utf-8-sig"))
            if "ui.menu.file" in catalog:
                catalog["ui.menu.file"] = "Fichier (fixture)"
                path.write_text(json.dumps(catalog, ensure_ascii=False), encoding="utf-8")
                break
        else:
            raise AssertionError("Fallback menu key is missing.")
        path = pack / "common.json"
        catalog = json.loads(path.read_text(encoding="utf-8-sig"))
        catalog["common.language_name"] = "Français (fixture)"
        path.write_text(json.dumps(catalog, ensure_ascii=False), encoding="utf-8")

        def run(arguments):
            result = subprocess.run([str(executable), *arguments], capture_output=True,
                                    text=True, encoding="utf-8", timeout=30)
            if result.returncode:
                raise AssertionError(f"{arguments}: exit={result.returncode}; {result.stderr}")
            return result

        run(["--language", "FR-fr", "--version"])
        run(["--language", "fr", "--catalog-check", "--report", str(output / "catalog.json")])
        run(["--language", "fr_FR", "--language-pack-check", "--audio-backend", "system",
             "--report", str(output / "ui.json")])
        report = json.loads((output / "ui.json").read_text(encoding="utf-8"))
        if not report["passed"] or report["language"] != "fr_FR" or report["menuTitle"] != "Fichier (fixture)":
            raise AssertionError(report)
        after = hashlib.sha256(executable.read_bytes()).hexdigest()
        if before != after:
            raise AssertionError("Language installation changed the executable.")
        report["executableSHA256"] = after
        report["binaryUnchanged"] = True
        (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print("External language: fr_FR discovered, UI/settings=PASS, EXE unchanged=PASS")
    finally:
        if pack.resolve().parent != directory.resolve():
            raise RuntimeError("Unexpected test pack path")
        shutil.rmtree(pack)


if __name__ == "__main__":
    main()
