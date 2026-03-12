#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/../.." && pwd)"
run_local_smoke=0
run_build_verify=0
state_dir="${ISSUE_BOT_STATE_DIR:-${TMPDIR:-/tmp}/rk_gb-issue-bot-preflight}"
cmake_path=""

write_status() {
    local status="$1"
    local message="$2"
    mkdir -p "$state_dir"
    cat > "$state_dir/preflight.json" <<EOF
{
  "status": "$(printf '%s' "$status")",
  "message": "$(printf '%s' "$message" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read())[1:-1])')",
  "repo_dir": "$(printf '%s' "$repo_dir")",
  "state_dir": "$(printf '%s' "$state_dir")",
  "cmake": "$(printf '%s' "$cmake_path")",
  "toolchain": "$(printf '%s' "${RK_TOOLCHAIN_BIN:-}")",
  "run_local_smoke": ${run_local_smoke},
  "run_build_verify": ${run_build_verify}
}
EOF
    cat > "$state_dir/preflight-summary.md" <<EOF
# Runner Preflight Summary

- status: $status
- message: $message
- repo_dir: $repo_dir
- state_dir: $state_dir
- cmake: ${cmake_path:-"(unset)"}
- toolchain: ${RK_TOOLCHAIN_BIN:-"(unset)"}
- run_local_smoke: $run_local_smoke
- run_build_verify: $run_build_verify
EOF
}

on_exit() {
    local rc="$1"
    if [ "$rc" -eq 0 ]; then
        write_status "success" "runner preflight passed"
    else
        write_status "failed" "runner preflight failed with exit code $rc"
    fi
}

trap 'on_exit $?' EXIT

usage() {
    cat <<'EOF'
Usage: bash tools/issue_bot/runner_preflight.sh [options]

Options:
  --repo-dir PATH          Repository root. Default: auto-detect from script path.
  --with-local-smoke       Run local_smoke_test.sh after environment checks.
  --with-build-verify      Run build_verify.sh after environment checks.
  -h, --help               Show this help.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --repo-dir)
            repo_dir="$2"
            shift 2
            ;;
        --with-local-smoke)
            run_local_smoke=1
            shift
            ;;
        --with-build-verify)
            run_build_verify=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "[issue-bot] unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

assert_cmd() {
    local name="$1"
    if ! command -v "$name" >/dev/null 2>&1; then
        echo "[issue-bot] missing command: $name" >&2
        exit 10
    fi
    echo "[issue-bot] ok command: $name -> $(command -v "$name")"
}

assert_file() {
    local path="$1"
    if [ ! -f "$path" ]; then
        echo "[issue-bot] missing file: $path" >&2
        exit 11
    fi
    echo "[issue-bot] ok file: $path"
}

assert_exec() {
    local path="$1"
    if [ ! -x "$path" ]; then
        echo "[issue-bot] path is not executable: $path" >&2
        exit 12
    fi
    echo "[issue-bot] ok executable: $path"
}

assert_dir() {
    local path="$1"
    if [ ! -d "$path" ]; then
        echo "[issue-bot] missing directory: $path" >&2
        exit 13
    fi
    echo "[issue-bot] ok directory: $path"
}

assert_cmd git
assert_cmd python3
assert_cmd bash
assert_dir "$repo_dir"
assert_file "$script_dir/build_verify.sh"
assert_file "$script_dir/local_smoke_test.sh"
assert_file "$script_dir/repair_executor.py"

if [ -n "${CMAKE_BIN:-}" ]; then
    assert_exec "${CMAKE_BIN}"
    cmake_path="${CMAKE_BIN}"
elif command -v cmake >/dev/null 2>&1; then
    cmake_path="$(command -v cmake)"
    echo "[issue-bot] ok command: cmake -> ${cmake_path}"
else
    echo "[issue-bot] CMAKE_BIN is not configured and cmake is unavailable" >&2
    exit 14
fi

if [ -z "${RK_TOOLCHAIN_BIN:-}" ]; then
    echo "[issue-bot] RK_TOOLCHAIN_BIN is not configured" >&2
    exit 15
fi
assert_dir "${RK_TOOLCHAIN_BIN}"
assert_exec "${RK_TOOLCHAIN_BIN}/arm-rockchip830-linux-uclibcgnueabihf-gcc"
assert_exec "${RK_TOOLCHAIN_BIN}/arm-rockchip830-linux-uclibcgnueabihf-g++"

if [ -z "${ISSUE_FIX_COMMAND:-}" ]; then
    echo "[issue-bot] ISSUE_FIX_COMMAND is not configured" >&2
    exit 16
fi
echo "[issue-bot] ok ISSUE_FIX_COMMAND is configured"

echo "[issue-bot] preflight summary:"
echo "  repo_dir=${repo_dir}"
echo "  cmake=${cmake_path}"
echo "  toolchain=${RK_TOOLCHAIN_BIN}"
echo "  state_dir=${state_dir}"
echo "  run_local_smoke=${run_local_smoke}"
echo "  run_build_verify=${run_build_verify}"

if [ "${run_local_smoke}" -eq 1 ]; then
    echo "[issue-bot] running local smoke test"
    ISSUE_BOT_STATE_DIR="$state_dir/smoke-test" bash "$script_dir/local_smoke_test.sh"
fi

if [ "${run_build_verify}" -eq 1 ]; then
    echo "[issue-bot] running build verify"
    bash "$script_dir/build_verify.sh" "$repo_dir"
fi

echo "[issue-bot] runner preflight passed"
