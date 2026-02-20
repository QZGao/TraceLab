# Baseline Artifact Management

This document defines how to capture, store, and update TraceLab regression baselines.

## What to keep

For each baseline update, keep:

- native measured run artifacts (`run_result`) for 5 measured runs
- qemu measured run artifacts (`run_result`) for 5 measured runs
- one compare artifact (`compare_result`)
- one labeled cold-cache startup run (`run_metadata.cache_state = cold`)
- one labeled warm-cache startup run (`run_metadata.cache_state = warm`)
- both startup artifacts should share the same `run_metadata.scenario_label`
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
6. Use the same workload command/input for both startup runs; only cache state should differ.
7. Run `scripts/check_regression.py` with the current threshold config, including `--cold-run` and `--warm-run`.
8. Record the cold-vs-warm report lines (duration ratio/delta, syscall-share delta, page-fault ratio).
9. If page-fault data is unavailable (`perf` not usable), store the warning from the gate output with the baseline notes.
10. If the gate passes and results look reasonable, store the artifacts under a date-stamped baseline directory.

### Example commands

```bash
# Run a labeled cold-cache startup/I/O sample.
./build/tracelab run \
  --native \
  --scenario-label startup_io \
  --cache-state cold \
  --json out/baseline/startup_io_cold.json \
  -- python3 -c "import pathlib; data=pathlib.Path('out/ci/startup_io_payload.bin').read_bytes(); print(len(data))"

# Run the same workload as a labeled warm-cache sample.
./build/tracelab run \
  --native \
  --scenario-label startup_io \
  --cache-state warm \
  --json out/baseline/startup_io_warm.json \
  -- python3 -c "import pathlib; data=pathlib.Path('out/ci/startup_io_payload.bin').read_bytes(); print(len(data))"

# Run the regression gate using compare + protocol runs + cold/warm pair.
python scripts/check_regression.py \
  --config config/regression_thresholds.json \
  --compare out/baseline/compare.json \
  --native-run out/baseline/native_run_1.json \
  --native-run out/baseline/native_run_2.json \
  --native-run out/baseline/native_run_3.json \
  --native-run out/baseline/native_run_4.json \
  --native-run out/baseline/native_run_5.json \
  --qemu-run out/baseline/qemu_run_1.json \
  --qemu-run out/baseline/qemu_run_2.json \
  --qemu-run out/baseline/qemu_run_3.json \
  --qemu-run out/baseline/qemu_run_4.json \
  --qemu-run out/baseline/qemu_run_5.json \
  --cold-run out/baseline/startup_io_cold.json \
  --warm-run out/baseline/startup_io_warm.json
```

## How to update thresholds safely

1. Compare current CI artifacts against the latest baseline.
2. Adjust one threshold at a time in `config/regression_thresholds.json`.
3. For startup/I/O behavior, tune only keys under `cold_warm`:
   - `max_warm_to_cold_duration_ratio`
   - `max_warm_minus_cold_syscall_share`
   - `max_warm_to_cold_page_fault_ratio`
4. Validate on at least two independent runs to avoid noise-driven changes.
5. Document the reason for each threshold change in the PR description.

## CI artifact

The CI pipeline uploads the current run/compare/report outputs as artifacts so that:

- regressions can be inspected without rerunning CI
- baseline refreshes can reuse known-good CI outputs
- threshold tuning has concrete data attached to each PR
- cold-vs-warm startup behavior is auditable from stored labeled artifacts
