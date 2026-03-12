from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from common import GitHubClient, RepoContext, ensure_dir, write_json, write_text
from triage_rules import classify_issue, format_triage_comment


def load_mock_issues(mock_path: str) -> list[dict]:
    payload = json.loads(Path(mock_path).read_text(encoding="utf-8"))
    if isinstance(payload, list):
        return payload
    return [payload]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Triage GitHub issues for auto-fix candidates")
    parser.add_argument("--issue-number", type=int, help="Only triage the specified issue number")
    parser.add_argument("--mock-json", help="Read issues from a local JSON file instead of GitHub API")
    parser.add_argument("--dry-run", action="store_true", help="Print actions without writing labels/comments")
    parser.add_argument("--write", action="store_true", help="Write labels/comments back to GitHub")
    return parser.parse_args()


def resolve_state_root(args: argparse.Namespace) -> Path:
    state_dir_env = os.environ.get("ISSUE_BOT_STATE_DIR", "")
    if state_dir_env:
        return ensure_dir(Path(state_dir_env).resolve())
    if args.mock_json:
        return ensure_dir(Path("/tmp/rk_gb-issue-bot-triage").resolve())
    return ensure_dir(Path.cwd().resolve())


def write_triage_status(
    state_root: Path,
    *,
    status: str,
    results: list[dict],
    dry_run: bool,
    source: str,
    message: str = "",
) -> None:
    candidate_count = sum(1 for item in results if item.get("auto_fixable"))
    manual_count = sum(1 for item in results if not item.get("auto_fixable"))
    payload = {
        "status": status,
        "message": message,
        "source": source,
        "dry_run": dry_run,
        "total": len(results),
        "candidate_count": candidate_count,
        "manual_count": manual_count,
        "results": results,
    }
    write_json(state_root / "triage-last-run.json", payload)

    lines = [
        "# Issue Triage Summary",
        "",
        f"- status: {status}",
        f"- message: {message or '(none)'}",
        f"- source: {source}",
        f"- dry_run: {'true' if dry_run else 'false'}",
        f"- total: {len(results)}",
        f"- candidate_count: {candidate_count}",
        f"- manual_count: {manual_count}",
    ]
    if results:
        lines.append("")
        lines.append("## Results")
        lines.append("")
        for item in results:
            lines.append(
                f"- #{item.get('number')} | auto_fixable={'true' if item.get('auto_fixable') else 'false'} | "
                f"risk={item.get('risk_level')} | {item.get('title', '')}"
            )
    else:
        lines.extend(["", "- summary: no issues found"])
    write_text(state_root / "triage-last-run-summary.md", "\n".join(lines) + "\n")


def main() -> int:
    args = parse_args()
    dry_run = args.dry_run or not args.write
    state_root = resolve_state_root(args)
    source = "mock-json" if args.mock_json else "github-api"
    results: list[dict] = []
    try:
        if args.mock_json:
            issues = load_mock_issues(args.mock_json)
            client = None
        else:
            client = GitHubClient(RepoContext.from_env(), dry_run=dry_run)
            if args.issue_number:
                issues = [client.get_issue(args.issue_number)]
            else:
                issues = client.list_open_issues()

        for issue in issues:
            decision = classify_issue(issue)
            current_labels = {label.get("name", "") for label in issue.get("labels", [])}
            results.append(
                {
                    "number": issue.get("number"),
                    "title": issue.get("title"),
                    "auto_fixable": decision.auto_fixable,
                    "risk_level": decision.risk_level,
                    "reason": decision.reason,
                }
            )
            print(json.dumps(results[-1], ensure_ascii=False))
            if client is None:
                continue
            target_label = decision.labels_to_add[0] if decision.labels_to_add else ""
            if target_label in current_labels and not any(label in current_labels for label in decision.labels_to_remove):
                continue
            client.add_labels(issue["number"], decision.labels_to_add)
            for label in decision.labels_to_remove:
                client.remove_label(issue["number"], label)
            client.create_comment(issue["number"], format_triage_comment(issue, decision))

        if not issues:
            print('{"message": "no open issues found"}')
            write_triage_status(
                state_root,
                status="no_open_issues",
                results=results,
                dry_run=dry_run,
                source=source,
                message="no open issues found",
            )
        else:
            write_triage_status(
                state_root,
                status="success",
                results=results,
                dry_run=dry_run,
                source=source,
                message="triage completed",
            )
        return 0
    except Exception as exc:
        write_triage_status(
            state_root,
            status="failed",
            results=results,
            dry_run=dry_run,
            source=source,
            message=str(exc),
        )
        raise


if __name__ == "__main__":
    raise SystemExit(main())
