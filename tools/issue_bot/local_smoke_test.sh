#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp_root="$(mktemp -d /tmp/rk-gb-issue-bot-smoke.XXXXXX)"
trap 'rm -rf "$tmp_root"' EXIT

remote_repo="$tmp_root/remote.git"
work_repo="$tmp_root/repo"
state_dir="${ISSUE_BOT_STATE_DIR:-$tmp_root/state}"
issue_json="$tmp_root/issue.json"

git init --bare "$remote_repo" >/dev/null
git clone "$remote_repo" "$work_repo" >/dev/null 2>&1
git -C "$work_repo" config user.name tester
git -C "$work_repo" config user.email tester@example.com

printf 'temporary smoke repo\n' > "$work_repo/README.md"
git -C "$work_repo" add README.md
git -C "$work_repo" commit -m 'init' >/dev/null
git -C "$work_repo" branch -M main
git -C "$work_repo" push -u origin main >/dev/null 2>&1

cat > "$issue_json" <<'EOF'
{
  "number": 201,
  "title": "build: smoke test candidate",
  "body": "Cross compile fails because of missing header during build.",
  "labels": [
    {"name": "bug"},
    {"name": "build"},
    {"name": "ha-candidate"}
  ]
}
EOF

python3 "$script_dir/repair_executor.py" \
  --repo-dir "$work_repo" \
  --state-dir "$state_dir" \
  --repair-command "$script_dir/example_fix.sh" \
  --mock-issue-json "$issue_json" \
  --dry-run \
  --skip-build-verify

if [ -d "$state_dir/issue-201/worktree" ]; then
  echo "smoke test failed: worktree was not cleaned" >&2
  exit 1
fi

if git -C "$work_repo" show-ref --verify --quiet "refs/heads/ha/issue-201-build-smoke-test-candidate"; then
  echo "smoke test failed: local repair branch still exists" >&2
  exit 1
fi

echo "local smoke test passed"
