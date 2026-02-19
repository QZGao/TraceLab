#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: run_nonzero_smoke.py <tracelab_exe>", file=sys.stderr)
        return 2

    tracelab_exe = sys.argv[1]
    if not os.path.exists(tracelab_exe):
        print(f"tracelab executable not found: {tracelab_exe}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="tracelab_nonzero_") as tmp:
        result_path = os.path.join(tmp, "result.json")
        if os.name == "nt":
            workload = ["cmd", "/c", "exit 7"]
        else:
            workload = ["sh", "-c", "exit 7"]

        cmd = [tracelab_exe, "run", "--native", "--json", result_path, "--"] + workload
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 7:
            print("expected tracelab to propagate workload exit code 7", file=sys.stderr)
            print(proc.stdout, file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return 1

        if not os.path.exists(result_path):
            print("result.json missing after run", file=sys.stderr)
            return 1

        with open(result_path, "r", encoding="utf-8") as f:
            data = json.load(f)

        if data.get("kind") != "run_result":
            print("unexpected kind in result.json", file=sys.stderr)
            return 1
        if data.get("exit_code") != 7:
            print("exit_code mismatch in result.json", file=sys.stderr)
            return 1
        fallback = data.get("fallback", {})
        if fallback.get("exit_classification") not in ("exit_code", "signal", "unknown"):
            print("invalid fallback.exit_classification", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
