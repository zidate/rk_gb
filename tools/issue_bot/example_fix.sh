#!/usr/bin/env bash
set -euo pipefail

: "${WORKTREE_DIR:?WORKTREE_DIR is required}"

marker_dir="$WORKTREE_DIR/.issue-bot-demo"
marker_file="$marker_dir/repair-result.txt"

mkdir -p "$marker_dir"
{
  printf 'issue=%s\n' "${ISSUE_NUMBER:-unknown}"
  printf 'title=%s\n' "${ISSUE_TITLE:-unknown}"
  printf 'branch=%s\n' "${REPAIR_BRANCH:-unknown}"
} > "$marker_file"

printf 'example fix wrote %s\n' "$marker_file"
