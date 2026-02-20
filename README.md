# TraceLab

TraceLab is a small toolkit that runs a workload, collects tracing and performance signals, then produces a bottleneck report you can use to explain why the workload is slow (CPU vs syscalls vs I/O vs memory pressure).

It is designed to match work such as characterizing critical workloads and building infrastructure based on QEMU and tracing tools.

It has two main modes:

- Native mode: run the program on your machine and trace it.
- QEMU mode: run the same program under QEMU user-mode and trace it (useful for ISA/proxy execution and instrumentation workflows).

## Features

- `tracelab doctor`: checks required and optional toolchain dependencies.
- `tracelab run`: runs workloads in native or QEMU mode and emits structured JSON.
- `tracelab report`: renders diagnosis, evidence, confidence, and limitations from `result.json`.
- `tracelab inspect`: ISA/ABI/linkage/symbol hints plus QEMU architecture selector hints.

## Repository Layout

```text
include/tracelab/        # public headers
src/                     # implementation
src/collectors/          # perf/strace/proc collectors
src/parsers/             # parser logic
schema/result.schema.json
tests/
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
cmake -S . -B build
cmake --build build
```

### Windows (Visual Studio multi-config generator)

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

## Basic Usage

### 1) Check environment

```bash
./build/tracelab doctor
```

On Windows Visual Studio builds:

```powershell
./build/Debug/tracelab.exe doctor
```

### 2) Run workload and emit JSON

```bash
./build/tracelab run --native --json out/result.json -- /bin/echo hello
```

### 3) Render report from JSON

```bash
./build/tracelab report out/result.json
```

### 4) Inspect binary metadata

```bash
./build/tracelab inspect /path/to/binary
```

`inspect` output includes:

- supported QEMU selectors (`x86_64`, `aarch64`, `riscv64`)
- selector hints derived from binary ISA metadata

### 5) QEMU mode

```bash
./build/tracelab run --qemu x86_64 --json out/qemu.json -- /bin/echo hello
```

Supported selectors are `x86_64`, `aarch64`, and `riscv64`.
Alias inputs such as `amd64` and `arm64` are normalized automatically.

## Testing

Default tests:

```bash
ctest --test-dir build --output-on-failure
```

On Windows multi-config builds:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Enable schema-validation test:

```bash
cmake -S . -B build -DTRACELAB_RUN_SCHEMA_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### On WSL

From inside WSL:

```bash
bash scripts/wsl_local_tests.sh
```

From Windows PowerShell (runs the same script inside WSL):

```powershell
.\scripts\run_wsl_tests.ps1
```

## Schema Validation

Schema lives at:

- `schema/result.schema.json`

Validate an instance:

```bash
python scripts/validate_schema.py \
  --schema schema/result.schema.json \
  --instance out/result.json
```

## Notes and Limitations

- Collection is Linux-first. On non-Linux hosts, Linux-specific collectors degrade to `unavailable`.
- `run` currently uses a replay strategy (`collection_strategy = main_run_plus_replay_collectors`) so collectors may run the workload more than once.
- `strict` mode requires usable collectors; otherwise the command exits with error.
- `run` includes a rule-based diagnosis with cited evidence metrics in output JSON.
- `inspect` relies on host tools (`readelf`, `objdump`/`llvm-objdump`) and degrades when unavailable.
