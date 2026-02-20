#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

BUILD_DIR="${BUILD_DIR:-build}"
OUT_DIR="${OUT_DIR:-out/demo}"
TRACELAB_BIN="${TRACELAB_BIN:-./${BUILD_DIR}/tracelab}"

if [[ ! -x "${TRACELAB_BIN}" ]]; then
  if [[ "${TRACELAB_BIN}" == "./build/tracelab" ]] && [[ -x "./build-wsl/tracelab" ]]; then
    TRACELAB_BIN="./build-wsl/tracelab"
  fi
fi

if [[ ! -x "${TRACELAB_BIN}" ]]; then
  echo "demo: expected tracelab binary at ${TRACELAB_BIN}" >&2
  echo "demo: build first with: cmake -S . -B ${BUILD_DIR} && cmake --build ${BUILD_DIR}" >&2
  exit 2
fi

mkdir -p "${OUT_DIR}"

echo "[demo] building microbench workloads"
bash scripts/build_microbench.sh

echo "[demo] environment probe"
"${TRACELAB_BIN}" doctor > "${OUT_DIR}/doctor.txt" || true

echo "[demo] native run (syscall workload)"
"${TRACELAB_BIN}" run --native --json "${OUT_DIR}/native_syscall.json" -- ./microbench/syscall_rate 50000
"${TRACELAB_BIN}" report "${OUT_DIR}/native_syscall.json" > "${OUT_DIR}/native_report.txt"

echo "[demo] selecting deterministic qemu target"
QEMU_ARCH=""
if [[ "$(uname -m)" == "x86_64" ]] && command -v qemu-x86_64 >/dev/null 2>&1; then
  QEMU_ARCH="x86_64"
elif command -v qemu-aarch64 >/dev/null 2>&1; then
  QEMU_ARCH="aarch64"
elif command -v qemu-riscv64 >/dev/null 2>&1; then
  QEMU_ARCH="riscv64"
fi

if [[ -n "${QEMU_ARCH}" ]]; then
  echo "[demo] qemu run (${QEMU_ARCH})"
  "${TRACELAB_BIN}" run --qemu "${QEMU_ARCH}" --json "${OUT_DIR}/qemu_syscall.json" -- ./microbench/syscall_rate 50000
  "${TRACELAB_BIN}" compare "${OUT_DIR}/native_syscall.json" "${OUT_DIR}/qemu_syscall.json" --json "${OUT_DIR}/compare.json"
else
  echo "[demo] qemu binary not found; skipping qemu/compare demo"
fi

echo "[demo] demo artifacts written under ${OUT_DIR}"
