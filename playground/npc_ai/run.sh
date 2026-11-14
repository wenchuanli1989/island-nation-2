#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
preset="${1:-debug}"
cd "$repo_dir"
./scripts/build.sh playground-run playground/npc_ai/main.c "$preset"
python3 - "$repo_dir/build-$preset/bin/domino-playground" "$repo_dir/playground/sandbox/domino" <<'PYTEST'
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
binary, sample = sys.argv[1:]
subprocess.run([binary, "--thread-smoke"], check=True)
for mode in ("--illegal-start", "--illegal-destroy", "--illegal-thread", "--invalid-quota", "--time-overflow", "--id-overflow"):
    result = subprocess.run([binary, mode], capture_output=True, text=True)
    if result.returncode != 1 or "expected rejection did not occur" in result.stderr:
        raise SystemExit(f"{mode}: expected fatal rejection, got {result.returncode}\n{result.stderr}")
    print(f"{mode}: rejected")
with tempfile.TemporaryDirectory(prefix="domino-npc-version-") as temporary:
    destination = pathlib.Path(temporary) / "world"
    shutil.copytree(sample, destination)
    subprocess.run([binary, "--load", str(destination)], check=True)
    metadata = destination / "meta.json"
    value = json.loads(metadata.read_text())
    value["version"] = 25
    metadata.write_text(json.dumps(value))
    result = subprocess.run([binary, "--load", str(destination)], capture_output=True, text=True)
    if result.returncode != 1 or "version" not in result.stderr.lower():
        raise SystemExit(f"old format was not rejected correctly: {result.returncode}\n{result.stderr}")
    print("sample format 26 accepted; format 25 rejected")
PYTEST
