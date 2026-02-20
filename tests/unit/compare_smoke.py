#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile


def write_run_result(path: str, mode: str, command: str, duration_sec: float, arch: str = "") -> None:
    data = {
        "schema_version": "0.1.0",
        "kind": "run_result",
        "timestamp_utc": "2026-02-20T00:00:00Z",
        "mode": mode,
        "command": command,
        "duration_sec": duration_sec,
        "exit_code": 0,
        "diagnosis": {
            "label": "inconclusive",
            "confidence": "low",
            "evidence": [
                {
                    "metric": "wall_time_sec",
                    "value": f"{duration_sec:.6f}",
                    "detail": "Elapsed runtime from fallback timer.",
                },
                {
                    "metric": "collector_statuses",
                    "value": "perf=ok, strace=ok, proc=ok",
                    "detail": "Collector availability influences diagnosis confidence.",
                },
            ],
            "limitations": [],
        },
        "host": {
            "os": "linux",
            "arch": "x86_64",
            "kernel_version": "6.8.0-test",
            "cpu_model": "Synthetic CPU",
            "cpu_governor_hint": "performance",
            "git_sha": "deadbee",
            "tool_versions": {
                "perf": "perf version 6.8.0",
                "strace": "strace -- version 6.7",
                "qemu-x86_64": "qemu-x86_64 version 8.2.2",
                "qemu-aarch64": "qemu-aarch64 version 8.2.2",
                "qemu-riscv64": "qemu-riscv64 version 8.2.2",
            },
        },
        "collectors": {
            "perf_stat": {
                "status": "ok",
                "counters": {
                    "cycles": 1_000_000,
                    "instructions": 2_000_000,
                    "branches": 500_000,
                    "branch_misses": 10_000,
                    "cache_misses": 5_000,
                    "page_faults": 100,
                },
            },
            "strace_summary": {"status": "ok", "total_time_sec": 0.01},
            "proc_status": {"status": "ok"},
        },
    }
    if mode == "qemu":
        data["qemu"] = {"arch": arch or "x86_64"}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: compare_smoke.py <tracelab_exe>", file=sys.stderr)
        return 2

    tracelab_exe = sys.argv[1]
    if not os.path.exists(tracelab_exe):
        print(f"tracelab executable not found: {tracelab_exe}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="tracelab_compare_") as tmp:
        native_result = os.path.join(tmp, "native.json")
        qemu_result = os.path.join(tmp, "qemu.json")
        compare_result = os.path.join(tmp, "compare.json")

        write_run_result(native_result, mode="native", command="/bin/echo hello", duration_sec=0.050)
        write_run_result(
            qemu_result,
            mode="qemu",
            command="/bin/echo hello",
            duration_sec=0.150,
            arch="x86_64",
        )

        proc = subprocess.run(
            [
                tracelab_exe,
                "compare",
                native_result,
                qemu_result,
                "--json",
                compare_result,
            ],
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            print("compare command failed unexpectedly", file=sys.stderr)
            print(proc.stdout, file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return 1

        if not os.path.exists(compare_result):
            print("compare json output missing", file=sys.stderr)
            return 1

        with open(compare_result, "r", encoding="utf-8") as f:
            data = json.load(f)

        if data.get("kind") != "compare_result":
            print("compare result kind mismatch", file=sys.stderr)
            return 1

        comparison = data.get("comparison", {})
        slowdown = comparison.get("slowdown_factor_qemu_vs_native")
        if not isinstance(slowdown, (int, float)) or slowdown <= 1.0:
            print("expected slowdown_factor_qemu_vs_native > 1.0", file=sys.stderr)
            return 1

        protocol = data.get("protocol", {})
        if protocol.get("recommended_measured_runs") != 5:
            print("protocol metadata mismatch", file=sys.stderr)
            return 1

        caveats = data.get("caveats", [])
        if not any("emulation-affected" in str(c) for c in caveats):
            print("missing emulation counter caveat", file=sys.stderr)
            return 1

        # Protocol-style comparison: 5 measured runs per mode should set the
        # recommended-sample-count marker to true and use medians.
        native_paths = []
        qemu_paths = []
        native_durations = [0.09, 0.10, 0.11, 0.12, 0.08]
        qemu_durations = [0.27, 0.31, 0.30, 0.29, 0.28]
        for i, d in enumerate(native_durations):
            path = os.path.join(tmp, f"native_{i}.json")
            write_run_result(path, mode="native", command="/bin/echo hello", duration_sec=d)
            native_paths.append(path)
        for i, d in enumerate(qemu_durations):
            path = os.path.join(tmp, f"qemu_{i}.json")
            write_run_result(path, mode="qemu", command="/bin/echo hello", duration_sec=d, arch="x86_64")
            qemu_paths.append(path)

        compare_protocol = os.path.join(tmp, "compare_protocol.json")
        cmd = [tracelab_exe, "compare"]
        for path in native_paths:
            cmd += ["--native", path]
        for path in qemu_paths:
            cmd += ["--qemu", path]
        cmd += ["--json", compare_protocol]

        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            print("protocol compare command failed unexpectedly", file=sys.stderr)
            print(proc.stdout, file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return 1

        with open(compare_protocol, "r", encoding="utf-8") as f:
            protocol_data = json.load(f)
        protocol = protocol_data.get("protocol", {})
        if protocol.get("uses_recommended_sample_count") is not True:
            print("expected protocol uses_recommended_sample_count=true for 5/5 inputs", file=sys.stderr)
            return 1
        if protocol_data.get("native", {}).get("sample_count") != 5:
            print("expected native sample_count=5", file=sys.stderr)
            return 1
        if protocol_data.get("qemu", {}).get("sample_count") != 5:
            print("expected qemu sample_count=5", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
