# TraceLab

TraceLab is a small toolkit that runs a workload, collects tracing and performance signals, then produces a bottleneck report you can use to explain why the workload is slow (CPU vs syscalls vs I/O vs memory pressure).

It is designed to match work such as characterizing critical workloads and building infrastructure based on QEMU and tracing tools.

It has two main modes:

- Native mode: run the program on your machine and trace it.
- QEMU mode: run the same program under QEMU user-mode and trace it (useful for ISA/proxy execution and instrumentation workflows).

## Features

- `tracelab doctor`: checks required and optional toolchain dependencies.
- `tracelab run`: runs workloads in native or QEMU mode and emits structured JSON with reproducibility metadata (kernel, CPU model/governor hint, tool versions, git SHA).
- `tracelab report`: renders diagnosis, evidence, confidence, and limitations from `result.json`.
- `tracelab inspect`: ISA/ABI/linkage/symbol hints plus QEMU architecture selector hints.
- `tracelab compare`: compares native vs QEMU run artifacts (delta duration, throughput change, caveated counters).

## Repository Layout

```text
include/tracelab/        # public headers
src/                     # implementation
  main.cpp               # CLI entrypoint
  qemu.cpp               # qemu selector/wrapper helpers
  util.cpp               # shared helpers
  commands/              # doctor/run/report/inspect/compare
  collectors/            # perf/strace/proc collectors
  diagnosis/             # rule-based bottleneck diagnosis
  parsers/               # parser logic
config/                  # regression thresholds
docs/                    # baseline artifact docs
microbench/              # CI/demo benchmark workloads
scripts/                 # build/test/demo helper scripts
tests/                   # fixtures and unit/smoke tests
```

## Prerequisites

Required for Linux collection behavior:

- `perf`
- `strace`
- `qemu-user` (if using `--qemu`)
- CMake + C++ compiler toolchain

Optional:

- Python 3 + `jsonschema` (for schema-validation tests)

## Build

### Linux / macOS (single-config generators)

```bash
# Configure the project.
cmake -S . -B build

# Build the TraceLab binary and test targets.
cmake --build build
```

### Windows (Visual Studio multi-config generator)

```powershell
# Configure the project.
cmake -S . -B build

# Build the Debug binaries (required for Visual Studio generators).
cmake --build build --config Debug
```

## Basic Usage

### Quickstart

```bash
# Check whether required tools (perf/strace/qemu/etc.) are available.
./build/tracelab doctor

# Run a workload natively and save machine-readable results.
./build/tracelab run --native --json out/result.json -- /bin/echo hello

# Render a human summary (diagnosis + evidence) from the JSON artifact.
./build/tracelab report out/result.json

# Inspect binary ISA/ABI/linkage and get qemu selector hints.
./build/tracelab inspect /path/to/binary

# Run the same workload under QEMU user-mode.
./build/tracelab run --qemu x86_64 --json out/qemu.json -- /bin/echo hello

# Compare native vs QEMU artifacts (delta duration and throughput change).
./build/tracelab compare out/result.json out/qemu.json --json out/compare.json
```

For startup/I/O studies, label cache state explicitly:

```bash
# Cold-cache labeled run.
./build/tracelab run --native --scenario-label startup_io --cache-state cold --json out/startup_cold.json -- <cmd>

# Warm-cache labeled run (same command/input as cold run).
./build/tracelab run --native --scenario-label startup_io --cache-state warm --json out/startup_warm.json -- <cmd>
```

Run metadata flags:

- `--scenario-label <name>`: tags the run with a scenario identifier (default: `unspecified`).
- `--cache-state <cold|warm|unspecified>`: records cache condition for the run (default: `unspecified`).
- Cold/warm comparisons should use the same command and input, with only cache state changed.

On Windows Visual Studio builds:

```powershell
# Check tool availability.
./build/Debug/tracelab.exe doctor

# Run a native workload and emit JSON.
./build/Debug/tracelab.exe run --native --json out/result.json -- cmd /c echo hello

# Render a human-readable report from the JSON artifact.
./build/Debug/tracelab.exe report out/result.json
```

Supported QEMU selectors are `x86_64`, `aarch64`, and `riscv64`.
Alias inputs such as `amd64` and `arm64` are normalized automatically.
`inspect` output includes selector hints derived from binary ISA metadata.

For protocol-style comparisons (median over measured runs), pass multiple artifacts:

```bash
# Compare medians from five measured native runs vs five measured qemu runs.
./build/tracelab compare \
  --native out/native_run1.json --native out/native_run2.json --native out/native_run3.json \
  --native out/native_run4.json --native out/native_run5.json \
  --qemu out/qemu_run1.json --qemu out/qemu_run2.json --qemu out/qemu_run3.json \
  --qemu out/qemu_run4.json --qemu out/qemu_run5.json \
  --json out/compare.json
```

### Quick demo

On a Linux machine with dependencies installed:

```bash
# Run the end-to-end Linux demo script and write artifacts to out/demo/.
bash scripts/demo_linux.sh
```

Outputs are written to `out/demo/`.

## Threshold Config

`config/regression_thresholds.json` defines CI gate thresholds. It includes:

- `duration.max_slowdown_factor_qemu_vs_native`
- `cache_misses.max_ratio_qemu_vs_native`
- `syscall_time.max_median_share_native`
- `syscall_time.max_median_share_qemu`
- `cold_warm.max_warm_to_cold_duration_ratio`
- `cold_warm.max_warm_minus_cold_syscall_share`
- `cold_warm.max_warm_to_cold_page_fault_ratio`

## Troubleshooting

- `perf` unavailable: run `tracelab doctor`; on locked-down systems TraceLab still records fallback counters.
- `strace` unavailable: install `strace` and rerun `tracelab doctor`.
- `qemu-<arch>` missing: install `qemu-user`; `tracelab inspect` can suggest selector hints.
- WSL `perf` wrapper mismatch: ensure `perf --version` works in WSL before running strict comparisons.
- Regression gate failures: inspect uploaded CI artifacts and compare against `docs/baseline_artifacts.md`.
- Cold/warm page-fault checks may be skipped with a warning when `perf` counters are unavailable.

## Testing

Default tests:

```bash
# Run the default test suite.
ctest --test-dir build --output-on-failure
```

On Windows multi-config builds:

```powershell
# Run the default test suite for the Debug configuration.
ctest --test-dir build -C Debug --output-on-failure
```

Enable schema-validation test:

```bash
# Turn on schema-validation tests at configure time.
cmake -S . -B build -DTRACELAB_RUN_SCHEMA_TESTS=ON

# Rebuild after changing the test configuration.
cmake --build build

# Run tests, including schema-validation checks.
ctest --test-dir build --output-on-failure
```

### On WSL

From inside WSL:

```bash
# Run the Linux/WSL local end-to-end test suite.
bash scripts/wsl_local_tests.sh
```

From Windows PowerShell (runs the same script inside WSL):

```powershell
# Run the same WSL suite from Windows PowerShell.
.\scripts\run_wsl_tests.ps1
```

## Notes and Limitations

- Collection is Linux-first. On non-Linux hosts, Linux-specific collectors degrade to `unavailable`.
- `run` currently uses a replay strategy (`collection_strategy = main_run_plus_replay_collectors`) so collectors may run the workload more than once.
- `strict` mode requires usable collectors; otherwise the command exits with error.
- `run` includes a rule-based diagnosis with cited evidence metrics in output JSON.
- `compare` treats wall-clock/throughput as primary metrics and tags QEMU perf counters as emulation-affected.
- `inspect` relies on host tools (`readelf`, `objdump`/`llvm-objdump`) and degrades when unavailable.
