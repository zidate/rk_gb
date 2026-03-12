from __future__ import annotations

from dataclasses import dataclass


AUTO_FIX_LABEL = "ha-candidate"
MANUAL_LABEL = "ha-manual"
FAILED_LABEL = "ha-failed"
IN_PROGRESS_LABEL = "ha-in-progress"
PR_OPEN_LABEL = "ha-pr-open"

LOW_RISK_LABELS = {"bug", "build", "regression", "good first issue"}
MANUAL_ONLY_LABELS = {
    "question",
    "enhancement",
    "feature",
    "discussion",
    "need-info",
    "duplicate",
    "wontfix",
    "invalid",
}
PROTOCOL_KEYWORDS = {"gb28181", "gat1400", "onvif", "tuya"}
BUILD_KEYWORDS = {
    "build",
    "compile",
    "compilation",
    "cmake",
    "toolchain",
    "link",
    "linker",
    "undefined reference",
    "no such file",
    "missing header",
    "cross compile",
}
HIGH_RISK_KEYWORDS = {
    "password",
    "token",
    "secret",
    "credential",
    "production",
    "live environment",
    "drop table",
    "rm -rf",
    "firmware upgrade",
    "factory reset",
}


@dataclass
class TriageDecision:
    auto_fixable: bool
    risk_level: str
    reason: str
    labels_to_add: list[str]
    labels_to_remove: list[str]


def _normalize_labels(issue: dict) -> set[str]:
    return {label.get("name", "").strip().lower() for label in issue.get("labels", [])}


def _combined_text(issue: dict) -> str:
    title = issue.get("title", "")
    body = issue.get("body", "") or ""
    return f"{title}\n{body}".lower()


def classify_issue(issue: dict) -> TriageDecision:
    labels = _normalize_labels(issue)
    text = _combined_text(issue)

    if labels & MANUAL_ONLY_LABELS:
        return TriageDecision(
            auto_fixable=False,
            risk_level="low",
            reason="标签表明这是人工需求或讨论类 issue",
            labels_to_add=[MANUAL_LABEL],
            labels_to_remove=[AUTO_FIX_LABEL],
        )

    high_risk_hits = sorted(keyword for keyword in HIGH_RISK_KEYWORDS if keyword in text)
    if high_risk_hits:
        return TriageDecision(
            auto_fixable=False,
            risk_level="high",
            reason=f"检测到高风险关键词: {', '.join(high_risk_hits)}",
            labels_to_add=[MANUAL_LABEL],
            labels_to_remove=[AUTO_FIX_LABEL],
        )

    if not issue.get("body"):
        return TriageDecision(
            auto_fixable=False,
            risk_level="medium",
            reason="issue 缺少正文，信息不足以自动修复",
            labels_to_add=[MANUAL_LABEL],
            labels_to_remove=[AUTO_FIX_LABEL],
        )

    protocol_hits = sorted(keyword for keyword in PROTOCOL_KEYWORDS if keyword in text)
    build_hits = sorted(keyword for keyword in BUILD_KEYWORDS if keyword in text)

    if protocol_hits and not build_hits:
        return TriageDecision(
            auto_fixable=False,
            risk_level="medium",
            reason=f"协议类问题默认转人工处理: {', '.join(protocol_hits)}",
            labels_to_add=[MANUAL_LABEL],
            labels_to_remove=[AUTO_FIX_LABEL],
        )

    if labels & LOW_RISK_LABELS or build_hits:
        return TriageDecision(
            auto_fixable=True,
            risk_level="low",
            reason="命中低风险构建/缺依赖/回归类规则",
            labels_to_add=[AUTO_FIX_LABEL],
            labels_to_remove=[MANUAL_LABEL, FAILED_LABEL],
        )

    return TriageDecision(
        auto_fixable=False,
        risk_level="medium",
        reason="未命中自动修复白名单规则",
        labels_to_add=[MANUAL_LABEL],
        labels_to_remove=[AUTO_FIX_LABEL],
    )


def format_triage_comment(issue: dict, decision: TriageDecision) -> str:
    title = issue.get("title", "").strip()
    lines = [
        "HelloAGENTS 自动巡检结果:",
        f"- issue: #{issue.get('number')} {title}",
        f"- 自动处理: {'是' if decision.auto_fixable else '否'}",
        f"- 风险等级: {decision.risk_level}",
        f"- 原因: {decision.reason}",
    ]
    if decision.auto_fixable:
        lines.append("- 下一步: 已标记为候选问题，等待自动修复工作流或人工触发 repair 流程")
    else:
        lines.append("- 下一步: 已标记为人工处理，暂不进入自动修复")
    return "\n".join(lines)
