# Baseline Artifact Management

This document defines how to capture, store, and update TraceLab regression baselines.

## What to keep

For each baseline update, keep:

- native measured run artifacts (`run_result`) for 5 measured runs
- qemu measured run artifacts (`run_result`) for 5 measured runs
- one compare artifact (`compare_result`)
- one labeled cold-cache startup run (`run_metadata.cache_state = cold`)
- one labeled warm-cache startup run (`run_metadata.cache_state = warm`)
- rendered report text for at least one native and one qemu run
- threshold config used (`config/regression_thresholds.json`)

Recommended storage layout:

```text
baselines/
  YYYY-MM-DD/
    native_run_1.json
    ...
    native_run_5.json
    qemu_run_1.json
    ...
    qemu_run_5.json
    compare.json
    startup_io_cold.json
    startup_io_warm.json
    native_report.txt
    qemu_report.txt
    thresholds.json
```

## How to capture a baseline

1. Build TraceLab and microbench binaries on a Linux host.
2. Run one warm-up execution per mode and discard.
3. Run 5 measured native executions and 5 measured qemu executions.
4. Run `tracelab compare` across those measured artifacts.
5. Capture one paired startup/I/O scenario (`cold` then `warm`) with explicit run metadata labels.
6. Run `scripts/check_regression.py` with the current threshold config.
7. If the gate passes and results look reasonable, store the artifacts under a date-stamped baseline directory.

## How to update thresholds safely

1. Compare current CI artifacts against the latest baseline.
2. Adjust one threshold at a time in `config/regression_thresholds.json`.
3. Validate on at least two independent runs to avoid noise-driven changes.
4. Document the reason for each threshold change in the PR description.

## CI artifact

The CI pipeline uploads the current run/compare/report outputs as artifacts so that:

- regressions can be inspected without rerunning CI
- baseline refreshes can reuse known-good CI outputs
- threshold tuning has concrete data attached to each PR
