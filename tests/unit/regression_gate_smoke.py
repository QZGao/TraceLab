#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
from typing import Optional


def write_run(
        path: str,
        mode: str,
        duration_sec: float,
        syscall_total_sec: float,
        perf_status: str = "ok",
        include_cache_counter: bool = True,
) -> None:
    """Write a synthetic run_result JSON file with the given parameters."""
    perf_counters = {
        "cycles": 1000,
        "instructions": 2000,
        "branches": 500,
        "branch_misses": 10,
        "page_faults": 20,
    }
    if include_cache_counter:
        perf_counters["cache_misses"] = 5

    obj = {
        "schema_version": "0.1.0",
        "kind": "run_result",
        "timestamp_utc": "2026-02-20T00:00:00Z",
        "mode": mode,
        "command": "/bin/echo hello",
        "duration_sec": duration_sec,
        "exit_code": 0,
        "diagnosis": {
            "label": "inconclusive",
            "confidence": "low",
            "evidence": [
                {"metric": "wall_time_sec", "value": f"{duration_sec:.6f}", "detail": "synthetic"},
                {"metric": "collector_statuses", "value": "synthetic", "detail": "synthetic"},
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
                "status": perf_status,
                "counters": perf_counters,
            },
            "strace_summary": {"status": "ok", "total_time_sec": syscall_total_sec},
            "proc_status": {"status": "ok"},
        },
    }
    if mode == "qemu":
        obj["qemu"] = {"arch": "x86_64"}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2)


def write_compare(path: str, slowdown: float, cache_ratio: Optional[float]) -> None:
    """Write a synthetic compare_result JSON file with the given parameters."""
    perf_ratio = {}
    if cache_ratio is not None:
        perf_ratio["cache_misses"] = cache_ratio

    obj = {
        "schema_version": "0.1.0",
        "kind": "compare_result",
        "timestamp_utc": "2026-02-20T00:00:00Z",
        "comparison": {
            "slowdown_factor_qemu_vs_native": slowdown,
            "perf_counter_ratio_qemu_vs_native": perf_ratio,
        },
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2)


def write_config(path: str) -> None:
    """Write a synthetic threshold config JSON file."""
    obj = {
        "duration": {"max_slowdown_factor_qemu_vs_native": 10.0},
        "cache_misses": {"max_ratio_qemu_vs_native": 40.0},
        "syscall_time": {"max_median_share_native": 0.8, "max_median_share_qemu": 0.95},
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2)


def run_gate(config: str, compare: str, native: list[str], qemu: list[str]) -> subprocess.CompletedProcess[str]:
    """Run the regression gate checker script with the given config, compare, and run_result files."""
    cmd = [sys.executable, "scripts/check_regression.py", "--config", config, "--compare", compare]
    for item in native:
        cmd += ["--native-run", item]
    for item in qemu:
        cmd += ["--qemu-run", item]
    return subprocess.run(cmd, capture_output=True, text=True)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="tracelab_reg_gate_") as tmp:
        config = os.path.join(tmp, "thresholds.json")
        compare_ok = os.path.join(tmp, "compare_ok.json")
        compare_bad = os.path.join(tmp, "compare_bad.json")
        compare_no_cache = os.path.join(tmp, "compare_no_cache.json")
        native1 = os.path.join(tmp, "native1.json")
        native2 = os.path.join(tmp, "native2.json")
        qemu1 = os.path.join(tmp, "qemu1.json")
        qemu2 = os.path.join(tmp, "qemu2.json")
        native_no_perf = os.path.join(tmp, "native_no_perf.json")
        qemu_no_perf = os.path.join(tmp, "qemu_no_perf.json")

        write_config(config)
        write_compare(compare_ok, slowdown=3.0, cache_ratio=15.0)
        write_compare(compare_bad, slowdown=50.0, cache_ratio=300.0)
        write_compare(compare_no_cache, slowdown=2.0, cache_ratio=None)
        write_run(native1, mode="native", duration_sec=1.0, syscall_total_sec=0.2)
        write_run(native2, mode="native", duration_sec=2.0, syscall_total_sec=0.3)
        write_run(qemu1, mode="qemu", duration_sec=3.0, syscall_total_sec=1.0)
        write_run(qemu2, mode="qemu", duration_sec=2.0, syscall_total_sec=0.8)
        write_run(
            native_no_perf,
            mode="native",
            duration_sec=1.0,
            syscall_total_sec=0.2,
            perf_status="error",
            include_cache_counter=False,
        )
        write_run(
            qemu_no_perf,
            mode="qemu",
            duration_sec=2.0,
            syscall_total_sec=0.8,
            perf_status="error",
            include_cache_counter=False,
        )

        ok = run_gate(config, compare_ok, [native1, native2], [qemu1, qemu2])
        if ok.returncode != 0:
            print("expected passing regression gate case", file=sys.stderr)
            print(ok.stdout, file=sys.stderr)
            print(ok.stderr, file=sys.stderr)
            return 1

        bad = run_gate(config, compare_bad, [native1, native2], [qemu1, qemu2])
        if bad.returncode == 0:
            print("expected failing regression gate case", file=sys.stderr)
            print(bad.stdout, file=sys.stderr)
            print(bad.stderr, file=sys.stderr)
            return 1

        no_cache = run_gate(config, compare_no_cache, [native_no_perf], [qemu_no_perf])
        if no_cache.returncode != 0:
            print("expected passing regression gate when cache-miss metric is unavailable", file=sys.stderr)
            print(no_cache.stdout, file=sys.stderr)
            print(no_cache.stderr, file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
