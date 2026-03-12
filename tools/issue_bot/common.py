from __future__ import annotations

import json
import os
import re
import subprocess
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


def slugify(value: str, max_length: int = 48) -> str:
    lowered = value.strip().lower()
    lowered = re.sub(r"[^a-z0-9]+", "-", lowered)
    lowered = lowered.strip("-")
    if not lowered:
        lowered = "issue"
    return lowered[:max_length].rstrip("-")


def ensure_dir(path: str | Path) -> Path:
    directory = Path(path)
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def write_text(path: str | Path, content: str) -> Path:
    target = Path(path)
    ensure_dir(target.parent)
    target.write_text(content, encoding="utf-8")
    return target


def write_json(path: str | Path, payload: Any) -> Path:
    target = Path(path)
    ensure_dir(target.parent)
    target.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    return target


def run_checked(
    command: list[str],
    *,
    cwd: str | Path | None = None,
    env: dict[str, str] | None = None,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd) if cwd is not None else None,
        env=env,
        text=True,
        check=True,
        capture_output=capture_output,
    )


def git_output(repo_dir: str | Path, args: list[str]) -> str:
    completed = run_checked(["git", "-C", str(repo_dir), *args], capture_output=True)
    return completed.stdout.strip()


def detect_base_branch(repo_dir: str | Path) -> str:
    try:
        ref = git_output(repo_dir, ["symbolic-ref", "refs/remotes/origin/HEAD"])
        return ref.rsplit("/", 1)[-1]
    except subprocess.CalledProcessError:
        branch = git_output(repo_dir, ["rev-parse", "--abbrev-ref", "HEAD"])
        return branch or "master"


@dataclass
class RepoContext:
    owner: str
    repo: str
    api_url: str
    token: str | None

    @classmethod
    def from_env(cls) -> "RepoContext":
        repository = os.environ.get("GITHUB_REPOSITORY", "")
        if "/" not in repository:
            raise RuntimeError("GITHUB_REPOSITORY is not set")
        owner, repo = repository.split("/", 1)
        return cls(
            owner=owner,
            repo=repo,
            api_url=os.environ.get("GITHUB_API_URL", "https://api.github.com"),
            token=os.environ.get("GITHUB_TOKEN"),
        )


class GitHubClient:
    def __init__(self, context: RepoContext, *, dry_run: bool = False) -> None:
        self.context = context
        self.dry_run = dry_run

    def _request(
        self,
        method: str,
        path: str,
        *,
        query: dict[str, Any] | None = None,
        body: Any | None = None,
    ) -> Any:
        url = self.context.api_url.rstrip("/") + path
        if query:
            pairs: list[tuple[str, str]] = []
            for key, value in query.items():
                if value is None:
                    continue
                if isinstance(value, (list, tuple, set)):
                    pairs.append((key, ",".join(str(item) for item in value)))
                else:
                    pairs.append((key, str(value)))
            if pairs:
                url += "?" + urllib.parse.urlencode(pairs)

        data = None
        if body is not None:
            data = json.dumps(body).encode("utf-8")

        headers = {
            "Accept": "application/vnd.github+json",
            "Content-Type": "application/json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "rk-gb-issue-bot",
        }
        if self.context.token:
            headers["Authorization"] = f"Bearer {self.context.token}"

        request = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request) as response:
                raw = response.read().decode("utf-8")
                if not raw:
                    return {}
                return json.loads(raw)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"GitHub API {method} {path} failed: {exc.code} {detail}") from exc

    def list_open_issues(
        self,
        *,
        labels: Iterable[str] | None = None,
        per_page: int = 100,
    ) -> list[dict[str, Any]]:
        query = {"state": "open", "per_page": per_page, "sort": "created", "direction": "asc"}
        if labels:
            query["labels"] = list(labels)
        payload = self._request(
            "GET",
            f"/repos/{self.context.owner}/{self.context.repo}/issues",
            query=query,
        )
        return [item for item in payload if "pull_request" not in item]

    def get_issue(self, number: int) -> dict[str, Any]:
        payload = self._request(
            "GET",
            f"/repos/{self.context.owner}/{self.context.repo}/issues/{number}",
        )
        if "pull_request" in payload:
            raise RuntimeError(f"Issue #{number} is a pull request")
        return payload

    def add_labels(self, number: int, labels: Iterable[str]) -> None:
        label_list = sorted({label for label in labels if label})
        if not label_list:
            return
        if self.dry_run:
            print(f"[dry-run] add labels to #{number}: {label_list}")
            return
        self._request(
            "POST",
            f"/repos/{self.context.owner}/{self.context.repo}/issues/{number}/labels",
            body={"labels": label_list},
        )

    def remove_label(self, number: int, label: str) -> None:
        if not label:
            return
        if self.dry_run:
            print(f"[dry-run] remove label from #{number}: {label}")
            return
        try:
            self._request(
                "DELETE",
                f"/repos/{self.context.owner}/{self.context.repo}/issues/{number}/labels/{urllib.parse.quote(label, safe='')}",
            )
        except RuntimeError as exc:
            if "404" not in str(exc):
                raise

    def create_comment(self, number: int, body: str) -> None:
        if self.dry_run:
            print(f"[dry-run] comment on #{number}:\n{body}")
            return
        self._request(
            "POST",
            f"/repos/{self.context.owner}/{self.context.repo}/issues/{number}/comments",
            body={"body": body},
        )

    def create_pull_request(
        self,
        *,
        title: str,
        head: str,
        base: str,
        body: str,
    ) -> dict[str, Any]:
        if self.dry_run:
            print(f"[dry-run] create PR {head} -> {base}: {title}")
            return {"number": 0, "html_url": ""}
        return self._request(
            "POST",
            f"/repos/{self.context.owner}/{self.context.repo}/pulls",
            body={"title": title, "head": head, "base": base, "body": body},
        )
