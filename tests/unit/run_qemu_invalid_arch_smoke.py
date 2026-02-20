#!/usr/bin/env python3
import os
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: run_qemu_invalid_arch_smoke.py <tracelab_exe>", file=sys.stderr)
        return 2

    tracelab_exe = sys.argv[1]
    if not os.path.exists(tracelab_exe):
        print(f"tracelab executable not found: {tracelab_exe}", file=sys.stderr)
        return 2

    proc = subprocess.run(
        [tracelab_exe, "run", "--qemu", "not-a-real-arch", "--", "echo", "hello"],
        capture_output=True,
        text=True,
    )
    if proc.returncode != 2:
        print("expected return code 2 for unsupported qemu selector", file=sys.stderr)
        print(proc.stdout, file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        return 1

    stderr = proc.stderr
    if "unsupported qemu architecture selector" not in stderr:
        print("missing unsupported-architecture error text", file=sys.stderr)
        print(stderr, file=sys.stderr)
        return 1
    if "supported selectors:" not in stderr:
        print("missing actionable supported selectors list", file=sys.stderr)
        print(stderr, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

