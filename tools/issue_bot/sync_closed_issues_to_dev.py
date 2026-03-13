from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from common import GitHubClient, RepoContext, detect_base_branch, ensure_dir, run_checked, write_json, write_text


@dataclass
class SyncResult:
    issue_number: int
    pr_number: int
    source_ref: str
    status: str
    message: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Sync closed-issue fix branches into dev")
    parser.add_argument("--repo-dir", default=".", help="Repository root path")
    parser.add_argument("--state-dir", default=os.environ.get("ISSUE_BOT_STATE_DIR", "/tmp/rk_gb-issue-bot"))
    parser.add_argument("--target-branch", default="dev", help="Target integration branch")
    parser.add_argument("--base-branch", default="", help="Fallback source branch when target branch is created")
    parser.add_argument("--issue-numbers", default="", help="Comma-separated issue numbers")
    parser.add_argument("--write-comments", action="store_true", help="Write sync comments back to issues")
    return parser.parse_args()


def load_issue_numbers(args: argparse.Namespace, client: GitHubClient) -> list[int]:
    if args.issue_numbers.strip():
        values: list[int] = []
        for item in args.issue_numbers.split(","):
            text = item.strip()
            if not text:
                continue
            values.append(int(text))
        return sorted(set(values))

    event_name = os.environ.get("GITHUB_EVENT_NAME", "")
    if event_name == "issues":
        event_path = os.environ.get("GITHUB_EVENT_PATH", "")
        if event_path:
            payload = json.loads(Path(event_path).read_text(encoding="utf-8"))
            issue_number = int(payload.get("issue", {}).get("number", 0))
            if issue_number > 0:
                return [issue_number]

    payload = client._request(
        "GET",
        f"/repos/{client.context.owner}/{client.context.repo}/issues",
        query={"state": "closed", "per_page": 100, "sort": "updated", "direction": "desc"},
    )
    issue_numbers: list[int] = []
    for item in payload:
        if "pull_request" in item:
            continue
        number = int(item.get("number", 0))
        if number > 0:
            issue_numbers.append(number)
    return issue_numbers


def list_pull_requests(client: GitHubClient, *, state: str) -> list[dict]:
    return client._request(
        "GET",
        f"/repos/{client.context.owner}/{client.context.repo}/pulls",
        query={"state": state, "per_page": 100, "sort": "updated", "direction": "desc"},
    )


def pr_matches_issue(pr: dict, issue_number: int) -> bool:
    body = pr.get("body") or ""
    title = pr.get("title") or ""
    haystack = f"{title}\n{body}"
    pattern = re.compile(rf"(?i)(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\s*:?\s*#\s*{issue_number}\b|#\s*{issue_number}\b")
    return pattern.search(haystack) is not None


def ensure_git_identity(repo_dir: Path) -> None:
    run_checked(["git", "-C", str(repo_dir), "config", "user.name", "github-actions[bot]"])
    run_checked(
        [
            "git",
            "-C",
            str(repo_dir),
            "config",
            "user.email",
            "41898282+github-actions[bot]@users.noreply.github.com",
        ]
    )


def remote_branch_exists(repo_dir: Path, branch: str) -> bool:
    completed = subprocess.run(
        ["git", "-C", str(repo_dir), "show-ref", "--verify", f"refs/remotes/origin/{branch}"],
        text=True,
        capture_output=True,
    )
    return completed.returncode == 0


def checkout_target_branch(repo_dir: Path, target_branch: str, base_branch: str) -> str:
    if remote_branch_exists(repo_dir, target_branch):
        run_checked(["git", "-C", str(repo_dir), "checkout", "-B", target_branch, f"origin/{target_branch}"])
        return f"origin/{target_branch}"

    source_branch = base_branch or detect_base_branch(repo_dir)
    run_checked(["git", "-C", str(repo_dir), "checkout", "-B", target_branch, f"origin/{source_branch}"])
    run_checked(["git", "-C", str(repo_dir), "push", "-u", "origin", target_branch])
    return f"origin/{source_branch}"


def commit_already_contains(repo_dir: Path, target_branch: str, source_ref: str) -> bool:
    completed = subprocess.run(
        ["git", "-C", str(repo_dir), "merge-base", "--is-ancestor", source_ref, target_branch],
        text=True,
    )
    return completed.returncode == 0


def merge_ref(repo_dir: Path, target_branch: str, source_ref: str, issue_number: int, pr_number: int) -> tuple[str, str]:
    run_checked(["git", "-C", str(repo_dir), "checkout", target_branch])
    if source_ref.startswith("origin/"):
        run_checked(["git", "-C", str(repo_dir), "fetch", "origin", source_ref.removeprefix("origin/")])

    if commit_already_contains(repo_dir, target_branch, source_ref):
        return "skipped", f"{source_ref} is already contained in {target_branch}"

    message = f"sync issue #{issue_number} from PR #{pr_number} into {target_branch}"
    completed = subprocess.run(
        [
            "git",
            "-C",
            str(repo_dir),
            "merge",
            "--no-ff",
            "-m",
            message,
            source_ref,
        ],
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        subprocess.run(["git", "-C", str(repo_dir), "merge", "--abort"], text=True, capture_output=True)
        detail = (completed.stderr or completed.stdout or "merge failed").strip()
        return "failed", detail

    return "merged", message


def resolve_pr_source(pr: dict) -> tuple[str | None, str]:
    state = (pr.get("state") or "").lower()
    merged_at = pr.get("merged_at")
    head_ref = pr.get("head", {}).get("ref", "")
    merge_commit_sha = pr.get("merge_commit_sha") or ""

    if state == "open" and head_ref:
        return f"origin/{head_ref}", head_ref
    if merged_at and merge_commit_sha:
        return merge_commit_sha, merge_commit_sha[:12]
    if head_ref:
        return f"origin/{head_ref}", head_ref
    return None, ""


def push_target(repo_dir: Path, target_branch: str) -> None:
    run_checked(["git", "-C", str(repo_dir), "push", "origin", target_branch])


def write_summary(state_dir: Path, target_branch: str, issue_numbers: list[int], results: list[SyncResult], base_source: str) -> None:
    summary_lines = [
        "# Closed Issue Sync Summary",
        "",
        f"- target_branch: `{target_branch}`",
        f"- base_source: `{base_source}`",
        f"- requested_issues: `{','.join(str(number) for number in issue_numbers) if issue_numbers else 'none'}`",
        "",
        "| issue | pr | source | status | message |",
        "| --- | --- | --- | --- | --- |",
    ]
    if results:
        for item in results:
            summary_lines.append(
                f"| #{item.issue_number} | #{item.pr_number if item.pr_number else '-'} | `{item.source_ref or '-'}` | `{item.status}` | {item.message.replace('|', '/')} |"
            )
    else:
        summary_lines.append("| - | - | - | `noop` | no matching PR found |")
    summary_lines.append("")
    write_text(state_dir / "closed-issue-sync-summary.md", "\n".join(summary_lines))
    write_json(
        state_dir / "closed-issue-sync-summary.json",
        {
            "target_branch": target_branch,
            "base_source": base_source,
            "issue_numbers": issue_numbers,
            "results": [item.__dict__ for item in results],
        },
    )


def comment_results(client: GitHubClient, target_branch: str, results: list[SyncResult]) -> None:
    by_issue: dict[int, list[SyncResult]] = {}
    for item in results:
        by_issue.setdefault(item.issue_number, []).append(item)

    for issue_number, items in by_issue.items():
        lines = [
            f"Closed-issue sync attempted for `{target_branch}`.",
            "",
        ]
        for item in items:
            source_label = item.source_ref or "-"
            if item.pr_number > 0:
                lines.append(f"- PR #{item.pr_number} `{source_label}`: `{item.status}` - {item.message}")
            else:
                lines.append(f"- `{source_label}`: `{item.status}` - {item.message}")
        client.create_comment(issue_number, "\n".join(lines))


def main() -> int:
    args = parse_args()
    repo_dir = Path(args.repo_dir).resolve()
    state_dir = ensure_dir(Path(args.state_dir).resolve())

    context = RepoContext.from_env()
    client = GitHubClient(context)

    issue_numbers = load_issue_numbers(args, client)
    run_checked(["git", "-C", str(repo_dir), "fetch", "origin", "--prune"])
    ensure_git_identity(repo_dir)
    base_source = checkout_target_branch(repo_dir, args.target_branch, args.base_branch)

    open_prs = list_pull_requests(client, state="open")
    closed_prs = list_pull_requests(client, state="closed")
    results: list[SyncResult] = []
    merged_any = False

    for issue_number in issue_numbers:
        matched = [pr for pr in open_prs if pr_matches_issue(pr, issue_number)]
        if not matched:
            matched = [pr for pr in closed_prs if pr_matches_issue(pr, issue_number)]
        if not matched:
            results.append(SyncResult(issue_number, 0, "", "skipped", "no matching PR found"))
            continue

        matched.sort(key=lambda item: (0 if (item.get("state") or "").lower() == "open" else 1, int(item.get("number", 0))))
        for pr in matched:
            pr_number = int(pr.get("number", 0))
            source_ref, source_label = resolve_pr_source(pr)
            if not source_ref:
                results.append(SyncResult(issue_number, pr_number, "", "failed", "missing merge source"))
                continue

            status, message = merge_ref(repo_dir, args.target_branch, source_ref, issue_number, pr_number)
            if status == "merged":
                merged_any = True
            results.append(SyncResult(issue_number, pr_number, source_label, status, message))

    if merged_any:
        push_target(repo_dir, args.target_branch)

    write_summary(state_dir, args.target_branch, issue_numbers, results, base_source)
    if args.write_comments and results:
        comment_results(client, args.target_branch, results)

    failures = [item for item in results if item.status == "failed"]
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
