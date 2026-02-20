#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-wsl}"
OUT_DIR="${OUT_DIR:-out/wsl}"

log() {
  printf '[wsl-test] %s\n' "$*"
}

die() {
  printf '[wsl-test] ERROR: %s\n' "$*" >&2
  exit 1
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    die "missing required command: $1"
  fi
}

require_cmd cmake
require_cmd ctest
require_cmd python3

HAS_JSONSCHEMA=0
if python3 - <<'PY' >/dev/null 2>&1
import importlib.util, sys
sys.exit(0 if importlib.util.find_spec("jsonschema") else 1)
PY
then
  HAS_JSONSCHEMA=1
else
  log "jsonschema not found; schema-validation checks will be skipped"
fi

cd "${REPO_ROOT}"

log "Configuring CMake"
SCHEMA_TEST_FLAG=OFF
if [[ "${HAS_JSONSCHEMA}" -eq 1 ]]; then
  SCHEMA_TEST_FLAG=ON
fi
cmake -S . -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTRACELAB_BUILD_TESTS=ON \
  -DTRACELAB_RUN_SCHEMA_TESTS="${SCHEMA_TEST_FLAG}"

log "Building"
cmake --build "${BUILD_DIR}" --parallel

log "Running CTest"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

mkdir -p "${OUT_DIR}"
TRACELAB_BIN="./${BUILD_DIR}/tracelab"
[[ -x "${TRACELAB_BIN}" ]] || die "expected binary not found: ${TRACELAB_BIN}"

log "Running doctor"
set +e
"${TRACELAB_BIN}" doctor > "${OUT_DIR}/doctor.txt" 2>&1
doctor_exit=$?
set -e
log "doctor exit code: ${doctor_exit} (non-zero may be expected if tools/permissions are missing)"

log "Running native workload smoke"
"${TRACELAB_BIN}" run --native --json "${OUT_DIR}/result_native.json" -- /bin/echo hello-wsl
"${TRACELAB_BIN}" report "${OUT_DIR}/result_native.json"
if [[ "${HAS_JSONSCHEMA}" -eq 1 ]]; then
  python3 scripts/validate_schema.py \
    --schema schema/result.schema.json \
    --instance "${OUT_DIR}/result_native.json"
else
  log "Skipping schema validation for native result (jsonschema not installed)"
fi

log "Running inspect smoke"
"${TRACELAB_BIN}" inspect --json "${OUT_DIR}/inspect.json" "${TRACELAB_BIN}"

log "Checking collector statuses"
python3 - "${OUT_DIR}/result_native.json" <<'PY'
import json
import pathlib
import shutil
import sys

result_path = pathlib.Path(sys.argv[1])
obj = json.loads(result_path.read_text(encoding="utf-8"))
collectors = obj.get("collectors", {})
required = ("perf_stat", "strace_summary", "proc_status")
missing = [name for name in required if name not in collectors]
if missing:
    print(f"missing collector entries: {missing}", file=sys.stderr)
    sys.exit(1)

proc_status = collectors["proc_status"].get("status")
if proc_status == "unavailable":
    print("proc_status collector should not be unavailable on Linux", file=sys.stderr)
    sys.exit(1)

perf_status = collectors["perf_stat"].get("status")
strace_status = collectors["strace_summary"].get("status")
print(f"perf_stat status: {perf_status}")
print(f"strace_summary status: {strace_status}")

# If tool exists, status should not be unavailable.
if shutil.which("perf") and perf_status == "unavailable":
    print("perf exists but perf_stat reported unavailable", file=sys.stderr)
    sys.exit(1)
if shutil.which("strace") and strace_status == "unavailable":
    print("strace exists but strace_summary reported unavailable", file=sys.stderr)
    sys.exit(1)
PY

if command -v qemu-x86_64 >/dev/null 2>&1; then
  log "Running optional QEMU smoke"
  "${TRACELAB_BIN}" run --qemu x86_64 --json "${OUT_DIR}/result_qemu_x86_64.json" -- /bin/echo hello-wsl
  log "Running optional native-vs-qemu comparison smoke"
  "${TRACELAB_BIN}" compare \
    "${OUT_DIR}/result_native.json" \
    "${OUT_DIR}/result_qemu_x86_64.json" \
    --json "${OUT_DIR}/compare_native_qemu.json"
  log "Running optional regression gate check"
  python3 scripts/check_regression.py \
    --config config/regression_thresholds.json \
    --compare "${OUT_DIR}/compare_native_qemu.json" \
    --native-run "${OUT_DIR}/result_native.json" \
    --qemu-run "${OUT_DIR}/result_qemu_x86_64.json"
  if [[ "${HAS_JSONSCHEMA}" -eq 1 ]]; then
    python3 scripts/validate_schema.py \
      --schema schema/result.schema.json \
      --instance "${OUT_DIR}/result_qemu_x86_64.json"
  else
    log "Skipping schema validation for QEMU result (jsonschema not installed)"
  fi
else
  log "Skipping QEMU smoke (qemu-x86_64 not found)"
fi

log "WSL local test suite completed successfully"
