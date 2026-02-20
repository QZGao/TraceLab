#!/usr/bin/env python3
import argparse
import json
import statistics
import sys
from pathlib import Path
from typing import Any, Optional


def load_json(path: Path) -> dict[str, Any]:
    """Load and return the JSON object from the given file path, ensuring it's a dict."""
    with path.open("r", encoding="utf-8") as f:
        obj = json.load(f)
    if not isinstance(obj, dict):
        raise ValueError(f"{path} is not a JSON object")
    return obj


def nested_get(obj: dict[str, Any], *keys: str) -> Any:
    """Safely get a nested value from a dict, returning None if any key is missing or if the path is not a dict."""
    cur: Any = obj
    for key in keys:
        if not isinstance(cur, dict) or key not in cur:
            return None
        cur = cur[key]
    return cur


def median_syscall_share(run_files: list[Path]) -> Optional[float]:
    """Compute the median syscall time share across the given run_result JSON files."""
    if not run_files:
        return None
    shares: list[float] = []
    for path in run_files:
        run = load_json(path)
        duration = nested_get(run, "duration_sec")
        syscall_total = nested_get(run, "collectors", "strace_summary", "total_time_sec")
        if not isinstance(duration, (int, float)) or duration <= 0:
            raise ValueError(f"{path} missing valid duration_sec")
        if not isinstance(syscall_total, (int, float)):
            raise ValueError(f"{path} missing collectors.strace_summary.total_time_sec")
        shares.append(float(syscall_total) / float(duration))
    return statistics.median(shares)


def median_perf_counter(run_files: list[Path], counter_name: str) -> Optional[float]:
    """Compute median perf counter value for a given counter across run_result files."""
    if not run_files:
        return None
    values: list[float] = []
    for path in run_files:
        run = load_json(path)
        value = nested_get(run, "collectors", "perf_stat", "counters", counter_name)
        if isinstance(value, (int, float)):
            values.append(float(value))
    if not values:
        return None
    return statistics.median(values)


def load_run_metrics(path: Path) -> dict[str, Any]:
    """Load a run_result JSON and extract key metrics used by regression checks."""
    run = load_json(path)
    duration = nested_get(run, "duration_sec")
    if not isinstance(duration, (int, float)) or float(duration) <= 0.0:
        raise ValueError(f"{path} missing valid duration_sec")

    syscall_total = nested_get(run, "collectors", "strace_summary", "total_time_sec")
    if not isinstance(syscall_total, (int, float)):
        syscall_total = None

    page_faults = nested_get(run, "collectors", "perf_stat", "counters", "page_faults")
    if not isinstance(page_faults, (int, float)):
        page_faults = None

    scenario_label = nested_get(run, "run_metadata", "scenario_label")
    if not isinstance(scenario_label, str):
        scenario_label = None

    cache_state = nested_get(run, "run_metadata", "cache_state")
    if not isinstance(cache_state, str):
        cache_state = None

    return {
        "duration_sec": float(duration),
        "syscall_total_sec": float(syscall_total) if isinstance(syscall_total, (int, float)) else None,
        "page_faults": float(page_faults) if isinstance(page_faults, (int, float)) else None,
        "scenario_label": scenario_label,
        "cache_state": cache_state,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Regression gate checker for TraceLab compare/run artifacts."
    )
    parser.add_argument("--config", required=True, help="Threshold config JSON path")
    parser.add_argument("--compare", required=True, help="compare_result JSON path")
    parser.add_argument("--native-run", action="append", default=[], help="native run_result JSON path")
    parser.add_argument("--qemu-run", action="append", default=[], help="qemu run_result JSON path")
    parser.add_argument("--cold-run", help="cold-cache run_result JSON path")
    parser.add_argument("--warm-run", help="warm-cache run_result JSON path")
    args = parser.parse_args()

    config = load_json(Path(args.config))
    compare = load_json(Path(args.compare))

    failures: list[str] = []
    warnings: list[str] = []
    reports: list[str] = []
    native_files = [Path(p) for p in args.native_run]
    qemu_files = [Path(p) for p in args.qemu_run]

    slowdown = nested_get(compare, "comparison", "slowdown_factor_qemu_vs_native")
    max_slowdown = nested_get(config, "duration", "max_slowdown_factor_qemu_vs_native")
    if not isinstance(slowdown, (int, float)):
        failures.append("compare JSON missing comparison.slowdown_factor_qemu_vs_native")
    elif not isinstance(max_slowdown, (int, float)):
        failures.append("config missing duration.max_slowdown_factor_qemu_vs_native")
    elif float(slowdown) > float(max_slowdown):
        failures.append(
            f"slowdown_factor_qemu_vs_native={float(slowdown):.6f} exceeds threshold {float(max_slowdown):.6f}"
        )

    cache_ratio = nested_get(compare, "comparison", "perf_counter_ratio_qemu_vs_native", "cache_misses")
    max_cache_ratio = nested_get(config, "cache_misses", "max_ratio_qemu_vs_native")
    if not isinstance(cache_ratio, (int, float)):
        native_cache = median_perf_counter(native_files, "cache_misses")
        qemu_cache = median_perf_counter(qemu_files, "cache_misses")
        if isinstance(native_cache, (int, float)) and native_cache > 0 and isinstance(qemu_cache, (int, float)):
            cache_ratio = float(qemu_cache) / float(native_cache)
        else:
            warnings.append("skipping cache-miss ratio gate: no usable cache_misses perf counter data")
            cache_ratio = None

    if cache_ratio is None:
        pass
    elif not isinstance(max_cache_ratio, (int, float)):
        failures.append("config missing cache_misses.max_ratio_qemu_vs_native")
    elif float(cache_ratio) > float(max_cache_ratio):
        failures.append(
            f"cache_miss_ratio_qemu_vs_native={float(cache_ratio):.6f} exceeds threshold {float(max_cache_ratio):.6f}"
        )

    if native_files:
        native_share = median_syscall_share(native_files)
        max_native_share = nested_get(config, "syscall_time", "max_median_share_native")
        if native_share is None:
            failures.append("unable to compute native syscall share")
        elif not isinstance(max_native_share, (int, float)):
            failures.append("config missing syscall_time.max_median_share_native")
        elif native_share > float(max_native_share):
            failures.append(
                f"native median syscall share={native_share:.6f} exceeds threshold {float(max_native_share):.6f}"
            )

    if qemu_files:
        qemu_share = median_syscall_share(qemu_files)
        max_qemu_share = nested_get(config, "syscall_time", "max_median_share_qemu")
        if qemu_share is None:
            failures.append("unable to compute qemu syscall share")
        elif not isinstance(max_qemu_share, (int, float)):
            failures.append("config missing syscall_time.max_median_share_qemu")
        elif qemu_share > float(max_qemu_share):
            failures.append(
                f"qemu median syscall share={qemu_share:.6f} exceeds threshold {float(max_qemu_share):.6f}"
            )

    if bool(args.cold_run) != bool(args.warm_run):
        failures.append("cold/warm guard requires both --cold-run and --warm-run")
    elif args.cold_run and args.warm_run:
        try:
            cold_metrics = load_run_metrics(Path(args.cold_run))
            warm_metrics = load_run_metrics(Path(args.warm_run))
        except ValueError as exc:
            failures.append(str(exc))
        else:
            cold_label = cold_metrics["scenario_label"]
            warm_label = warm_metrics["scenario_label"]
            cold_state = cold_metrics["cache_state"]
            warm_state = warm_metrics["cache_state"]

            if cold_label is None or warm_label is None:
                failures.append("cold/warm run metadata missing scenario_label")
            elif cold_label != warm_label:
                failures.append(
                    f"cold/warm scenario_label mismatch: cold='{cold_label}', warm='{warm_label}'"
                )

            if cold_state != "cold":
                failures.append(f"cold-run cache_state must be 'cold' (got '{cold_state}')")
            if warm_state != "warm":
                failures.append(f"warm-run cache_state must be 'warm' (got '{warm_state}')")

            cold_duration = float(cold_metrics["duration_sec"])
            warm_duration = float(warm_metrics["duration_sec"])
            duration_ratio = warm_duration / cold_duration
            duration_delta = warm_duration - cold_duration
            reports.append(
                "cold_vs_warm duration: "
                f"cold={cold_duration:.6f}s warm={warm_duration:.6f}s "
                f"ratio={duration_ratio:.6f} delta={duration_delta:.6f}s"
            )

            max_duration_ratio = nested_get(config, "cold_warm", "max_warm_to_cold_duration_ratio")
            if not isinstance(max_duration_ratio, (int, float)):
                failures.append("config missing cold_warm.max_warm_to_cold_duration_ratio")
            elif duration_ratio > float(max_duration_ratio):
                failures.append(
                    f"warm_to_cold_duration_ratio={duration_ratio:.6f} exceeds threshold "
                    f"{float(max_duration_ratio):.6f}"
                )

            cold_syscall_total = cold_metrics["syscall_total_sec"]
            warm_syscall_total = warm_metrics["syscall_total_sec"]
            if isinstance(cold_syscall_total, float) and isinstance(warm_syscall_total, float):
                cold_syscall_share = cold_syscall_total / cold_duration
                warm_syscall_share = warm_syscall_total / warm_duration
                syscall_share_delta = warm_syscall_share - cold_syscall_share
                reports.append(
                    "cold_vs_warm syscall_share: "
                    f"cold={cold_syscall_share:.6f} warm={warm_syscall_share:.6f} "
                    f"delta={syscall_share_delta:.6f}"
                )

                max_syscall_delta = nested_get(config, "cold_warm", "max_warm_minus_cold_syscall_share")
                if not isinstance(max_syscall_delta, (int, float)):
                    failures.append("config missing cold_warm.max_warm_minus_cold_syscall_share")
                elif syscall_share_delta > float(max_syscall_delta):
                    failures.append(
                        f"warm_minus_cold_syscall_share={syscall_share_delta:.6f} exceeds threshold "
                        f"{float(max_syscall_delta):.6f}"
                    )
            else:
                warnings.append("skipping cold/warm syscall-share gate: missing strace total_time_sec")

            cold_page_faults = cold_metrics["page_faults"]
            warm_page_faults = warm_metrics["page_faults"]
            if isinstance(cold_page_faults, float) and isinstance(warm_page_faults, float):
                if cold_page_faults > 0.0:
                    page_fault_ratio = warm_page_faults / cold_page_faults
                    reports.append(
                        "cold_vs_warm page_fault_ratio: "
                        f"cold={cold_page_faults:.0f} warm={warm_page_faults:.0f} "
                        f"ratio={page_fault_ratio:.6f}"
                    )

                    max_page_fault_ratio = nested_get(config, "cold_warm", "max_warm_to_cold_page_fault_ratio")
                    if not isinstance(max_page_fault_ratio, (int, float)):
                        failures.append("config missing cold_warm.max_warm_to_cold_page_fault_ratio")
                    elif page_fault_ratio > float(max_page_fault_ratio):
                        failures.append(
                            f"warm_to_cold_page_fault_ratio={page_fault_ratio:.6f} exceeds threshold "
                            f"{float(max_page_fault_ratio):.6f}"
                        )
                else:
                    warnings.append("skipping cold/warm page-fault gate: cold run has zero page_faults")
            else:
                warnings.append("skipping cold/warm page-fault gate: no usable perf page_faults data")

    if failures:
        print("regression gate: FAIL", file=sys.stderr)
        for line in reports:
            print(f"  - report: {line}", file=sys.stderr)
        for line in failures:
            print(f"  - {line}", file=sys.stderr)
        for line in warnings:
            print(f"  - warning: {line}", file=sys.stderr)
        return 1

    print("regression gate: PASS")
    if native_files:
        print(f"  native samples checked: {len(native_files)}")
    if qemu_files:
        print(f"  qemu samples checked: {len(qemu_files)}")
    for line in reports:
        print(f"  report: {line}")
    for line in warnings:
        print(f"  warning: {line}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
