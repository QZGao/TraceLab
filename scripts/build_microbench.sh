#!/usr/bin/env bash
set -euo pipefail

CC_BIN="${CC:-cc}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

mkdir -p "${REPO_ROOT}/microbench"

"${CC_BIN}" -O2 -std=c11 -Wall -Wextra -Wpedantic \
  "${REPO_ROOT}/microbench/mem_bw.c" -o "${REPO_ROOT}/microbench/mem_bw"

"${CC_BIN}" -O2 -std=c11 -Wall -Wextra -Wpedantic \
  "${REPO_ROOT}/microbench/syscall_rate.c" -o "${REPO_ROOT}/microbench/syscall_rate"

echo "Built microbench binaries:"
echo "  microbench/mem_bw"
echo "  microbench/syscall_rate"

