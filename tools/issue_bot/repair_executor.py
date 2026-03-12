from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from common import GitHubClient, RepoContext, detect_base_branch, ensure_dir, git_output, run_checked, slugify, write_json, write_text
from triage_rules import AUTO_FIX_LABEL, FAILED_LABEL, IN_PROGRESS_LABEL, PR_OPEN_LABEL, classify_issue


class LocalDryRunClient:
    def add_labels(self, number: int, labels: list[str]) -> None:
        label_list = sorted({label for label in labels if label})
        if label_list:
            print(f"[dry-run] add labels to #{number}: {label_list}")

    def remove_label(self, number: int, label: str) -> None:
        if label:
            print(f"[dry-run] remove label from #{number}: {label}")

    def create_comment(self, number: int, body: str) -> None:
        print(f"[dry-run] comment on #{number}:\n{body}")

    def create_pull_request(self, *, title: str, head: str, base: str, body: str) -> dict:
        print(f"[dry-run] create PR {head} -> {base}: {title}\n{body}")
        return {"number": 0, "html_url": ""}


class NoPendingIssue(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Repair GitHub issues from ha-candidate queue")
    parser.add_argument("--issue-number", type=int, help="Specific issue number to repair")
    parser.add_argument("--pick-next", action="store_true", help="Pick the next ha-candidate issue")
    parser.add_argument("--mock-issue-json", help="Read a single issue from local JSON for smoke tests")
    parser.add_argument("--repo-dir", default=".", help="Repository root path")
    parser.add_argument("--state-dir", default=os.environ.get("ISSUE_BOT_STATE_DIR", "/tmp/rk_gb-issue-bot"))
    parser.add_argument("--repair-command", default=os.environ.get("ISSUE_FIX_COMMAND", ""))
    parser.add_argument("--base-branch", help="Base branch to branch from")
    parser.add_argument("--dry-run", action="store_true", help="Do not push branches or create PRs")
    parser.add_argument("--skip-build-verify", action="store_true", help="Skip build_verify.sh during local smoke tests")
    return parser.parse_args()


def load_mock_issues(path: str) -> list[dict]:
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    if isinstance(payload, dict):
        return [payload]
    if isinstance(payload, list):
        return payload
    raise RuntimeError("mock issue JSON must contain a single issue object or a list of issues")


def select_issue_from_list(issues: list[dict], issue_number: int | None, pick_next: bool) -> dict:
    if issue_number is not None:
        for issue in issues:
            if int(issue.get("number", 0)) == issue_number:
                return issue
        raise RuntimeError(f"Issue #{issue_number} not found in mock queue")
    if len(issues) == 1 and not pick_next:
        return issues[0]
    if not pick_next:
        raise RuntimeError("Either --issue-number or --pick-next is required")
    for issue in issues:
        labels = {label.get("name", "") for label in issue.get("labels", [])}
        if AUTO_FIX_LABEL not in labels:
            continue
        if IN_PROGRESS_LABEL in labels or PR_OPEN_LABEL in labels or FAILED_LABEL in labels:
            continue
        return issue
    raise NoPendingIssue("No pending ha-candidate issues found; exiting without repair")


def select_issue(client: GitHubClient, issue_number: int | None, pick_next: bool) -> dict:
    if issue_number is not None:
        return client.get_issue(issue_number)
    if not pick_next:
        raise RuntimeError("Either --issue-number or --pick-next is required")
    for issue in client.list_open_issues(labels=[AUTO_FIX_LABEL]):
        labels = {label.get("name", "") for label in issue.get("labels", [])}
        if IN_PROGRESS_LABEL not in labels and PR_OPEN_LABEL not in labels and FAILED_LABEL not in labels:
            return issue
    raise NoPendingIssue("No pending ha-candidate issues found; exiting without repair")


def prepare_worktree(repo_dir: Path, worktree_dir: Path, branch_name: str, base_branch: str) -> None:
    if worktree_dir.exists():
        shutil.rmtree(worktree_dir)
    source_ref = f"origin/{base_branch}"
    try:
        run_checked(["git", "-C", str(repo_dir), "show-ref", "--verify", f"refs/remotes/{source_ref}"])
    except subprocess.CalledProcessError:
        source_ref = base_branch
    run_checked(["git", "-C", str(repo_dir), "worktree", "add", "-B", branch_name, str(worktree_dir), source_ref])


def run_fix_command(command: str, worktree_dir: Path, state_dir: Path, issue: dict, branch_name: str) -> None:
    if not command:
        raise RuntimeError("ISSUE_FIX_COMMAND is not configured on the runner")

    issue_json = state_dir / "issue.json"
    issue_body = state_dir / "issue_body.md"
    write_json(issue_json, issue)
    write_text(issue_body, issue.get("body", "") or "")

    env = os.environ.copy()
    env.update(
        {
            "ISSUE_NUMBER": str(issue["number"]),
            "ISSUE_TITLE": issue.get("title", ""),
            "ISSUE_JSON_FILE": str(issue_json),
            "ISSUE_BODY_FILE": str(issue_body),
            "WORKTREE_DIR": str(worktree_dir),
            "REPAIR_BRANCH": branch_name,
        }
    )
    subprocess.run(command, cwd=str(worktree_dir), env=env, shell=True, check=True, text=True)


def run_build_verify(worktree_dir: Path, state_dir: Path) -> None:
    env = os.environ.copy()
    env["ISSUE_BOT_BUILD_ROOT"] = str(state_dir / "build")
    run_checked(["bash", str(SCRIPT_DIR / "build_verify.sh"), str(worktree_dir)], env=env)


def commit_changes(worktree_dir: Path, issue: dict) -> bool:
    status = git_output(worktree_dir, ["status", "--porcelain"])
    if not status:
        return False
    try:
        git_output(worktree_dir, ["config", "user.name"])
    except subprocess.CalledProcessError:
        run_checked(["git", "-C", str(worktree_dir), "config", "user.name", "github-actions[bot]"])
        run_checked(
            [
                "git",
                "-C",
                str(worktree_dir),
                "config",
                "user.email",
                "41898282+github-actions[bot]@users.noreply.github.com",
            ]
        )
    run_checked(["git", "-C", str(worktree_dir), "add", "-A"])
    commit_message = f"issue-bot: fix #{issue['number']} {slugify(issue.get('title', ''), 30)}"
    run_checked(["git", "-C", str(worktree_dir), "commit", "-m", commit_message])
    return True


def push_branch(worktree_dir: Path, branch_name: str) -> None:
    run_checked(["git", "-C", str(worktree_dir), "push", "-u", "origin", branch_name])


def cleanup_worktree(repo_dir: Path, worktree_dir: Path, branch_name: str) -> None:
    if worktree_dir.exists():
        try:
            run_checked(["git", "-C", str(repo_dir), "worktree", "remove", "--force", str(worktree_dir)])
        except subprocess.CalledProcessError:
            shutil.rmtree(worktree_dir, ignore_errors=True)
    try:
        run_checked(["git", "-C", str(repo_dir), "branch", "-D", branch_name], capture_output=True)
    except subprocess.CalledProcessError:
        pass


def write_run_status(
    state_root: Path,
    *,
    status: str,
    message: str,
    issue: dict | None = None,
    branch_name: str = "",
    base_branch: str = "",
    dry_run: bool = False,
    pr_url: str = "",
    skipped_build_verify: bool = False,
) -> None:
    payload = {
        "status": status,
        "message": message,
        "issue_number": issue.get("number") if issue else None,
        "issue_title": issue.get("title") if issue else "",
        "branch_name": branch_name,
        "base_branch": base_branch,
        "dry_run": dry_run,
        "pr_url": pr_url,
        "skipped_build_verify": skipped_build_verify,
    }
    if issue:
        payload["issue_state_dir"] = str(state_root / f"issue-{issue['number']}")
        payload["build_logs_dir"] = str(state_root / f"issue-{issue['number']}" / "build" / "logs")
    write_json(state_root / "last-run.json", payload)

    lines = [
        "# Issue Repair Summary",
        "",
        f"- status: {status}",
        f"- message: {message}",
        f"- dry_run: {'true' if dry_run else 'false'}",
        f"- skipped_build_verify: {'true' if skipped_build_verify else 'false'}",
    ]
    if issue:
        issue_state_dir = state_root / f"issue-{issue['number']}"
        build_logs_dir = issue_state_dir / "build" / "logs"
        lines.extend(
            [
                f"- issue: #{issue['number']} {issue.get('title', '')}",
                f"- branch: {branch_name or '(none)'}",
                f"- base_branch: {base_branch or '(unknown)'}",
                f"- issue_state_dir: {issue_state_dir}",
                f"- build_logs_dir: {build_logs_dir}",
            ]
        )
    if pr_url:
        lines.append(f"- pr_url: {pr_url}")
    write_text(state_root / "last-run-summary.md", "\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    repo_dir = Path(args.repo_dir).resolve()
    state_root = ensure_dir(Path(args.state_dir).resolve())
    pr_url = ""
    try:
        if args.mock_issue_json:
            client = LocalDryRunClient()
            issue = select_issue_from_list(load_mock_issues(args.mock_issue_json), args.issue_number, args.pick_next)
        else:
            client = GitHubClient(RepoContext.from_env(), dry_run=args.dry_run)
            issue = select_issue(client, args.issue_number, args.pick_next)
    except NoPendingIssue as exc:
        write_run_status(
            state_root,
            status="no_pending_issue",
            message=str(exc),
            dry_run=args.dry_run,
            skipped_build_verify=args.skip_build_verify,
        )
        print(f"[issue-bot] {exc}")
        return 0
    decision = classify_issue(issue)
    if not decision.auto_fixable:
        write_run_status(
            state_root,
            status="rejected",
            message=decision.reason,
            issue=issue,
            dry_run=args.dry_run,
            skipped_build_verify=args.skip_build_verify,
        )
        raise RuntimeError(f"Issue #{issue['number']} is no longer auto-fixable: {decision.reason}")

    issue_dir = ensure_dir(state_root / f"issue-{issue['number']}")
    branch_name = f"ha/issue-{issue['number']}-{slugify(issue.get('title', 'issue'))}"
    base_branch = args.base_branch or detect_base_branch(repo_dir)
    worktree_dir = issue_dir / "worktree"

    try:
        client.add_labels(issue["number"], [IN_PROGRESS_LABEL])
        client.remove_label(issue["number"], FAILED_LABEL)

        prepare_worktree(repo_dir, worktree_dir, branch_name, base_branch)
        run_fix_command(args.repair_command, worktree_dir, issue_dir, issue, branch_name)
        if not args.skip_build_verify:
            run_build_verify(worktree_dir, issue_dir)

        if not commit_changes(worktree_dir, issue):
            raise RuntimeError("修复器执行完成，但仓库中没有检测到任何改动")

        if not args.dry_run:
            push_branch(worktree_dir, branch_name)
            pr = client.create_pull_request(
                title=f"[issue-bot] fix #{issue['number']} {issue.get('title', '')}",
                head=branch_name,
                base=base_branch,
                body=(
                    f"Auto-generated fix for #{issue['number']}.\n\n"
                    f"- issue: #{issue['number']}\n"
                    f"- build verify: passed on self-hosted runner\n"
                    f"- branch: `{branch_name}`"
                ),
            )
            pr_url = pr.get("html_url", "")
            client.add_labels(issue["number"], [PR_OPEN_LABEL])
            client.remove_label(issue["number"], AUTO_FIX_LABEL)
            client.remove_label(issue["number"], IN_PROGRESS_LABEL)
            client.create_comment(
                issue["number"],
                (
                    "HelloAGENTS 自动修复已完成并创建 PR:\n"
                    f"- 分支: `{branch_name}`\n"
                    f"- PR: {pr_url or '(dry-run)'}"
                ),
            )
        else:
            client.create_comment(
                issue["number"],
                f"HelloAGENTS dry-run: 已在本地完成 issue #{issue['number']} 的修复流程演练，分支 `{branch_name}` 未推送。",
            )
        write_run_status(
            state_root,
            status="success",
            message="repair flow completed",
            issue=issue,
            branch_name=branch_name,
            base_branch=base_branch,
            dry_run=args.dry_run,
            pr_url=pr_url,
            skipped_build_verify=args.skip_build_verify,
        )
    except Exception as exc:
        client.add_labels(issue["number"], [FAILED_LABEL])
        client.remove_label(issue["number"], AUTO_FIX_LABEL)
        client.remove_label(issue["number"], IN_PROGRESS_LABEL)
        client.create_comment(
            issue["number"],
            (
                "HelloAGENTS 自动修复失败:\n"
                f"- issue: #{issue['number']}\n"
                f"- 分支: `{branch_name}`\n"
                f"- 原因: {exc}\n"
                "- 下一步: 已移出自动修复候选队列，请人工确认后再重新添加 `ha-candidate`"
            ),
        )
        write_run_status(
            state_root,
            status="failed",
            message=str(exc),
            issue=issue,
            branch_name=branch_name,
            base_branch=base_branch,
            dry_run=args.dry_run,
            skipped_build_verify=args.skip_build_verify,
        )
        raise
    finally:
        cleanup_worktree(repo_dir, worktree_dir, branch_name)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
