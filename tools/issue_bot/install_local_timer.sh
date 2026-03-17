#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
default_repo_dir="$(cd "${script_dir}/../.." && pwd)"

repo_dir="$default_repo_dir"
env_file="${ISSUE_BOT_ENV_FILE:-$HOME/.config/rk-gb-issue-bot.env}"
schedule="${ISSUE_BOT_CRON_SCHEDULE:-*/15 * * * *}"
state_dir="${ISSUE_BOT_STATE_DIR:-${TMPDIR:-/tmp}/rk_gb-issue-bot-local}"
base_branch="${ISSUE_BOT_BASE_BRANCH:-silver}"
apply_changes=0

usage() {
    cat <<'EOF'
Usage: bash tools/issue_bot/install_local_timer.sh [options]

Options:
  --repo-dir PATH          Repository root. Default: auto-detect from script path.
  --env-file PATH          Environment file sourced by cron.
  --schedule SPEC          Cron schedule. Default: */15 * * * *
  --state-dir PATH         State root for local_cycle.sh.
  --base-branch NAME       Default auto-fix base branch. Default: silver.
  --apply                  Install/update the current user's crontab.
  -h, --help               Show this help.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --repo-dir)
            repo_dir="$2"
            shift 2
            ;;
        --env-file)
            env_file="$2"
            shift 2
            ;;
        --schedule)
            schedule="$2"
            shift 2
            ;;
        --state-dir)
            state_dir="$2"
            shift 2
            ;;
        --base-branch)
            base_branch="$2"
            shift 2
            ;;
        --apply)
            apply_changes=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf '[issue-bot][install-timer] unknown argument: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

mkdir -p "$(dirname "$env_file")"
if [ ! -f "$env_file" ]; then
    cp "$script_dir/runner_env.example" "$env_file"
fi

python3 - "$env_file" "$repo_dir" "$state_dir" "$base_branch" <<'PY'
from pathlib import Path
import sys

env_path = Path(sys.argv[1])
repo_dir = sys.argv[2]
state_dir = sys.argv[3]
base_branch = sys.argv[4]

lines = env_path.read_text(encoding="utf-8").splitlines()
desired = {
    "ISSUE_BOT_AUTOFIX_REPO_DIR": repo_dir,
    "ISSUE_BOT_STATE_DIR": state_dir,
    "ISSUE_BOT_BASE_BRANCH": base_branch,
}

present = set()
updated = []
for line in lines:
    key = line.split("=", 1)[0].strip() if "=" in line and not line.lstrip().startswith("#") else ""
    if key in desired:
        updated.append(f"{key}={desired[key]}")
        present.add(key)
    else:
        updated.append(line)

for key, value in desired.items():
    if key not in present:
        updated.append(f"{key}={value}")

env_path.write_text("\n".join(updated).rstrip() + "\n", encoding="utf-8")
PY

cron_tag="# rk_gb issue bot"
cron_cmd="${schedule} /bin/bash -lc 'set -a; source \"${env_file}\"; set +a; bash \"${script_dir}/local_cycle.sh\" >> \"${state_dir}/cron.log\" 2>&1' ${cron_tag}"

printf '[issue-bot][install-timer] env_file=%s\n' "$env_file"
printf '[issue-bot][install-timer] schedule=%s\n' "$schedule"
printf '[issue-bot][install-timer] cron entry:\n%s\n' "$cron_cmd"

if [ "$apply_changes" != "1" ]; then
    printf '[issue-bot][install-timer] dry-run only; rerun with --apply to install crontab\n'
    exit 0
fi

current_cron="$(crontab -l 2>/dev/null || true)"
new_cron="$(printf '%s\n' "$current_cron" | grep -Fv "$cron_tag" || true)"
{
    printf '%s\n' "$new_cron"
    printf '%s\n' "$cron_cmd"
} | crontab -

printf '[issue-bot][install-timer] crontab updated\n'
