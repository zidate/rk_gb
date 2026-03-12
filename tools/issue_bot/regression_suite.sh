#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/../.." && pwd)"
run_build_verify=0

usage() {
    cat <<'EOF'
Usage: bash tools/issue_bot/regression_suite.sh [options]

Options:
  --repo-dir PATH          Repository root. Default: auto-detect from script path.
  --with-build-verify      Include RK830 build verify in runner preflight phase.
  -h, --help               Show this help.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --repo-dir)
            repo_dir="$2"
            shift 2
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

state_root="${ISSUE_BOT_STATE_DIR:-$(mktemp -d /tmp/rk-gb-issue-bot-regression.XXXXXX)}"
mkdir -p "$state_root"
triage_state="$state_root/triage"
preflight_state="$state_root/preflight"
triage_mock="$state_root/triage-issues.json"

if [ -z "${RK_TOOLCHAIN_BIN:-}" ] && [ -d "/home/jerry/silver/RK/arm-rockchip830-linux-uclibcgnueabihf/bin" ]; then
    export RK_TOOLCHAIN_BIN="/home/jerry/silver/RK/arm-rockchip830-linux-uclibcgnueabihf/bin"
fi

if [ -z "${CMAKE_BIN:-}" ] && [ -x "/home/jerry/silver/.tools/cmake-4.2.3-linux-x86_64/bin/cmake" ]; then
    export CMAKE_BIN="/home/jerry/silver/.tools/cmake-4.2.3-linux-x86_64/bin/cmake"
fi

if [ -z "${ISSUE_FIX_COMMAND:-}" ]; then
    export ISSUE_FIX_COMMAND="$script_dir/example_fix.sh"
fi

cat > "$triage_mock" <<'EOF'
[
  {
    "number": 501,
    "title": "build: regression suite compile error",
    "body": "Cross compile fails with missing header and cmake build error.",
    "labels": [{"name": "bug"}]
  },
  {
    "number": 502,
    "title": "GB28181 semantic discussion",
    "body": "Need to discuss GB28181 invite behavior and bye semantics.",
    "labels": [{"name": "question"}]
  }
]
EOF

echo "[issue-bot] regression: running triage mock"
ISSUE_BOT_STATE_DIR="$triage_state" python3 "$script_dir/triage_issues.py" --mock-json "$triage_mock" --dry-run

echo "[issue-bot] regression: running runner preflight"
preflight_args=(--repo-dir "$repo_dir" --with-local-smoke)
if [ "$run_build_verify" -eq 1 ]; then
    preflight_args+=(--with-build-verify)
fi
ISSUE_BOT_STATE_DIR="$preflight_state" bash "$script_dir/runner_preflight.sh" "${preflight_args[@]}"

echo "[issue-bot] regression suite passed"
echo "[issue-bot] triage summary: $triage_state/triage-last-run-summary.md"
echo "[issue-bot] preflight summary: $preflight_state/preflight-summary.md"
echo "[issue-bot] smoke repair summary: $preflight_state/smoke-test/last-run-summary.md"
if [ "$run_build_verify" -eq 1 ]; then
    echo "[issue-bot] build verify logs: $preflight_state/build/logs"
fi
