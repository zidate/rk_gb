#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
default_repo_dir="$(cd "${script_dir}/../.." && pwd)"

repo_dir="${ISSUE_BOT_AUTOFIX_REPO_DIR:-$default_repo_dir}"
state_root="${ISSUE_BOT_STATE_DIR:-${TMPDIR:-/tmp}/rk_gb-issue-bot-local}"
mirror_dir="${ISSUE_BOT_AUTOFIX_MIRROR_DIR:-${state_root}/repo}"
base_branch="${ISSUE_BOT_BASE_BRANCH:-silver}"
lock_file="${ISSUE_BOT_LOCK_FILE:-${state_root}/issue-bot.lock}"
write_back="${ISSUE_BOT_WRITE:-1}"
skip_build_verify="${ISSUE_BOT_SKIP_BUILD_VERIFY:-0}"
allow_manual="${ISSUE_BOT_ALLOW_MANUAL:-0}"
dry_run=0
issue_number=""

usage() {
    cat <<'EOF'
Usage: bash tools/issue_bot/local_cycle.sh [options]

Options:
  --repo-dir PATH          Bootstrap repository used to discover origin URL.
  --state-dir PATH         State root used for lock/mirror/logs.
  --base-branch NAME       Base branch for auto-fix worktrees. Default: silver.
  --issue-number N         Repair the specified issue instead of picking next candidate.
  --dry-run                Do not write labels/comments or push PR branches.
  --skip-build-verify      Skip build_verify.sh for this run.
  --allow-manual           Allow a manually specified issue to bypass auto-fix triage.
  -h, --help               Show this help.
EOF
}

log() {
    printf '[issue-bot][local-cycle] %s\n' "$*"
}

require_cmd() {
    local name="$1"
    if ! command -v "$name" >/dev/null 2>&1; then
        printf '[issue-bot][local-cycle] missing command: %s\n' "$name" >&2
        exit 10
    fi
}

resolve_repo_slug_from_remote() {
    local remote_url="$1"
    local slug="${remote_url}"
    slug="${slug#git@github.com:}"
    slug="${slug#ssh://git@github.com/}"
    slug="${slug#https://github.com/}"
    slug="${slug#http://github.com/}"
    slug="${slug%.git}"
    printf '%s' "$slug"
}

ensure_github_context() {
    if [ -z "${GITHUB_REPOSITORY:-}" ]; then
        local remote_url
        remote_url="$(git -C "$repo_dir" remote get-url origin)"
        export GITHUB_REPOSITORY
        GITHUB_REPOSITORY="$(resolve_repo_slug_from_remote "$remote_url")"
    fi

    if [ -z "${GITHUB_TOKEN:-}" ] && command -v gh >/dev/null 2>&1; then
        export GITHUB_TOKEN
        GITHUB_TOKEN="$(gh auth token)"
    fi

    if [ -z "${GITHUB_REPOSITORY:-}" ]; then
        printf '[issue-bot][local-cycle] GITHUB_REPOSITORY is not configured\n' >&2
        exit 11
    fi

    if [ -z "${GITHUB_TOKEN:-}" ]; then
        printf '[issue-bot][local-cycle] GITHUB_TOKEN is not configured and gh auth token is unavailable\n' >&2
        exit 12
    fi
}

prepare_isolated_repo() {
    local remote_url
    remote_url="$(git -C "$repo_dir" remote get-url origin)"

    mkdir -p "$state_root"
    if [ ! -d "${mirror_dir}/.git" ]; then
        log "cloning isolated repo into ${mirror_dir}"
        git clone "$remote_url" "$mirror_dir" >/dev/null 2>&1
    fi

    git -C "$mirror_dir" remote set-url origin "$remote_url"
    git -C "$mirror_dir" fetch origin --prune >/dev/null 2>&1
    git -C "$mirror_dir" checkout -B "$base_branch" "origin/$base_branch" >/dev/null 2>&1
    git -C "$mirror_dir" reset --hard "origin/$base_branch" >/dev/null 2>&1
    git -C "$mirror_dir" clean -fdx >/dev/null 2>&1
    git -C "$mirror_dir" worktree prune >/dev/null 2>&1
}

run_triage() {
    local triage_state="${state_root}/triage"
    local -a args
    mkdir -p "$triage_state"
    args=()
    if [ -n "$issue_number" ]; then
        args+=(--issue-number "$issue_number")
    fi
    if [ "$write_back" = "1" ] && [ "$dry_run" = "0" ]; then
        ISSUE_BOT_STATE_DIR="$triage_state" \
            python3 "$script_dir/triage_issues.py" --write "${args[@]}"
    else
        ISSUE_BOT_STATE_DIR="$triage_state" \
            python3 "$script_dir/triage_issues.py" --dry-run "${args[@]}"
    fi
}

run_repair() {
    local repair_state="${state_root}/repair"
    local -a args
    mkdir -p "$repair_state"
    args=(
        --repo-dir "$mirror_dir"
        --state-dir "$repair_state"
        --repair-command "${ISSUE_FIX_COMMAND:-}"
        --base-branch "$base_branch"
    )

    if [ -n "$issue_number" ]; then
        args+=(--issue-number "$issue_number")
    else
        args+=(--pick-next)
    fi

    if [ "$skip_build_verify" = "1" ]; then
        args+=(--skip-build-verify)
    fi

    if [ "$dry_run" = "1" ] || [ "$write_back" != "1" ]; then
        args+=(--dry-run)
    fi

    if [ "$allow_manual" = "1" ]; then
        args+=(--allow-manual)
    fi

    ISSUE_BOT_STATE_DIR="$repair_state" \
        python3 "$script_dir/repair_executor.py" "${args[@]}"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --repo-dir)
            repo_dir="$2"
            shift 2
            ;;
        --state-dir)
            state_root="$2"
            mirror_dir="${ISSUE_BOT_AUTOFIX_MIRROR_DIR:-$2/repo}"
            lock_file="${ISSUE_BOT_LOCK_FILE:-$2/issue-bot.lock}"
            shift 2
            ;;
        --base-branch)
            base_branch="$2"
            shift 2
            ;;
        --issue-number)
            issue_number="$2"
            shift 2
            ;;
        --dry-run)
            dry_run=1
            shift
            ;;
        --skip-build-verify)
            skip_build_verify=1
            shift
            ;;
        --allow-manual)
            allow_manual=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf '[issue-bot][local-cycle] unknown argument: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

require_cmd git
require_cmd python3
require_cmd bash
require_cmd flock

if [ ! -d "$repo_dir/.git" ]; then
    printf '[issue-bot][local-cycle] repo_dir is not a git repository: %s\n' "$repo_dir" >&2
    exit 3
fi

if [ -z "${ISSUE_FIX_COMMAND:-}" ]; then
    printf '[issue-bot][local-cycle] ISSUE_FIX_COMMAND is not configured\n' >&2
    exit 4
fi

ensure_github_context
mkdir -p "$state_root"

exec 9>"$lock_file"
if ! flock -n 9; then
    log "another cycle is already running; skip"
    exit 0
fi

log "repo_dir=${repo_dir}"
log "state_root=${state_root}"
log "mirror_dir=${mirror_dir}"
log "base_branch=${base_branch}"
log "issue_number=${issue_number:-<pick-next>}"
log "dry_run=${dry_run}"
log "skip_build_verify=${skip_build_verify}"
log "write_back=${write_back}"

prepare_isolated_repo
run_triage
run_repair

log "cycle completed"
