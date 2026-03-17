# Issue Bot

本目录存放 `rk_gb` 的 GitHub issue 自动巡检与自动修复骨架实现。

## 文件说明

- `triage_rules.py`
  定义 issue 白名单/黑名单、风险分级和标签规则。
- `triage_issues.py`
  周期巡检 open issues，输出候选问题，并按需回写标签和评论。
- `repair_executor.py`
  从 `ha-candidate` 队列中选择 issue，创建独立 worktree，调用 runner 上的修复器并执行交叉编译验证。
- `build_verify.sh`
  复用 RK830 隔离交叉编译命令，验证修复结果至少能在 self-hosted runner 上完成构建。
- `example_fix.sh`
  `ISSUE_FIX_COMMAND` 的最小示例，只用于临时仓库或 smoke test 演练。
- `local_smoke_test.sh`
  本地自测脚本。自动创建临时 git 仓库和 mock issue，验证 repair 执行器的 worktree / commit / cleanup 主流程。
- `runner_preflight.sh`
  runner 上线前自检脚本。检查 `git/python3/cmake/toolchain/ISSUE_FIX_COMMAND`，并可选执行 smoke test 与 build verify。
- `runner_env.example`
  runner 侧推荐环境变量示例，不包含真实敏感信息。
- `regression_suite.sh`
  一键本地回归脚本。串联 triage mock、runner preflight 和 local smoke，可选附带 build verify。

## Runner 前提

- 必须运行在专用 self-hosted runner 上，不与人工开发共用工作目录。
- 建议先把 GitHub repo variable `ISSUE_BOT_RUNNER_READY` 保持为空或 `false`；待 runner 和 `ISSUE_FIX_COMMAND` 都就绪后，再切换为 `true`。
- 如需启用定时自动修复，再额外设置 GitHub repo variable `ISSUE_BOT_REPAIR_ENABLED=true`；未启用前，定时 repair workflow 会在 GitHub 托管机上快速跳过并写明原因，不再长时间排队。
- `issue-repair.yml` 的定时任务会直接拾取 `ha-candidate` 队列；手工触发才会读取 `workflow_dispatch` 输入，避免在 `schedule` 事件中错误引用 `inputs` 上下文。
- runner 需要预装:
  - `git`
  - `python3`
  - 可用的 `cmake`，或通过 `CMAKE_BIN` 指定私有路径
  - Rockchip 工具链目录，并通过 `RK_TOOLCHAIN_BIN` 指定
- 不要把账号密码写入仓库；工作流认证使用 `GITHUB_TOKEN`，修复器通过 runner 环境变量 `ISSUE_FIX_COMMAND` 注入。

## Runner 上线前检查

建议在启用 `issue-repair.yml` 定时任务前，先在目标 self-hosted runner 上执行:

```bash
export RK_TOOLCHAIN_BIN=/path/to/toolchain/bin
export CMAKE_BIN=/path/to/cmake
export ISSUE_FIX_COMMAND=/abs/path/to/fix_issue.sh
bash tools/issue_bot/runner_preflight.sh --repo-dir /path/to/rk_gb
```

如果要进一步确认 runner 具备完整构建能力，可追加:

```bash
bash tools/issue_bot/runner_preflight.sh \
  --repo-dir /path/to/rk_gb \
  --with-local-smoke \
  --with-build-verify
```

说明:

- `--with-local-smoke` 只验证 repair 骨架，不依赖真实 GitHub API。
- `--with-build-verify` 会执行 RK830 隔离交叉编译，适合在真实 runner 上做接线前验收。
- 可以先参考 `runner_env.example` 配置 runner 侧环境变量，再把同名值写入 GitHub repo variables。
- `.github/workflows/issue-runner-preflight.yml` 提供了 GitHub 页面上的手工预检入口；`issue-repair.yml` 也会在正式 repair 前自动执行一次基础 preflight。
- `issue-repair.yml` 和 `issue-runner-preflight.yml` 会把 `ISSUE_BOT_STATE_DIR` 上传为 artifact，并把摘要写到 GitHub step summary，便于回看日志和状态文件。
- `issue-triage.yml` 也会把 `ISSUE_BOT_STATE_DIR` 上传为 artifact，并写入 triage 摘要，便于回看本轮候选/人工分流结果。

## 环境变量

- `GITHUB_TOKEN`
  GitHub Actions 自动注入的仓库令牌。
- `GITHUB_REPOSITORY`
  仓库名，格式 `owner/repo`。
- `RK_TOOLCHAIN_BIN`
  RK830 工具链 `bin` 目录。
- `CMAKE_BIN`
  可选，指定 cmake 可执行文件。
- `ISSUE_FIX_COMMAND`
  runner 上的修复命令。命令会在独立 worktree 中执行。
- `ISSUE_BOT_STATE_DIR`
  可选，指定 issue bot 的临时状态目录。
- `ISSUE_BOT_BASE_BRANCH`
  自动修复默认基线分支。当前建议固定为 `silver`。
- `ISSUE_BOT_AUTOFIX_REPO_DIR`
  本机定时任务启动时用来发现 `origin` 的引导仓库路径。
- `ISSUE_BOT_AUTOFIX_MIRROR_DIR`
  本机定时任务使用的隔离仓库目录。建议放在 `/tmp` 或专用缓存目录。
- `ISSUE_BOT_LOCK_FILE`
  本机定时任务的互斥锁文件，避免并发巡检互相踩环境。
- `ISSUE_BOT_WRITE`
  设为 `1` 时允许 triage / repair 回写 GitHub；设为 `0` 时按 dry-run 演练。
- `ISSUE_BOT_RUNNER_READY`
  GitHub repo variable。设置为 `true` 后，`issue-runner-preflight.yml` 和 `issue-repair.yml` 才会进入 self-hosted runner 作业。
- `ISSUE_BOT_REPAIR_ENABLED`
  GitHub repo variable。仅对 `issue-repair.yml` 的定时任务生效；设置为 `true` 后，schedule 才会开始消费 `ha-candidate` 队列。

## 修复器契约

`ISSUE_FIX_COMMAND` 由 runner 管理员提供，仓库内不保存具体实现。执行时会注入以下环境变量:

- `ISSUE_NUMBER`
- `ISSUE_TITLE`
- `ISSUE_JSON_FILE`
- `ISSUE_BODY_FILE`
- `WORKTREE_DIR`
- `REPAIR_BRANCH`

修复器必须遵守以下规则:

- 只能在 `WORKTREE_DIR` 内修改代码。
- 不得直接执行 issue 原文中的 shell 命令。
- 不得读取或输出 secrets。
- 修改完成后必须返回 0，否则 repair 工作流会标记失败。

## 清理与留痕

- `repair_executor.py` 每次运行结束后都会移除临时 `git worktree` 并删除本地修复分支，避免 self-hosted runner 长期堆积脏状态。
- `ISSUE_BOT_STATE_DIR` 会保留 issue 上下文和构建中间目录，便于故障排查；如需周期清理，应由 runner 管理员在仓库外统一处理。
- `triage_issues.py` 会在 `ISSUE_BOT_STATE_DIR/triage-last-run.json` 和 `triage-last-run-summary.md` 中记录本轮 triage 总数、候选数、人工处理数和逐条结果。
- `repair_executor.py` 会在 `ISSUE_BOT_STATE_DIR/last-run.json` 和 `last-run-summary.md` 中落盘本次 repair 结果，供 workflow summary 和 artifact 使用。
- `runner_preflight.sh` 会在 `ISSUE_BOT_STATE_DIR/preflight.json` 和 `preflight-summary.md` 中落盘预检结果。
- 若 preflight 同时触发 `--with-local-smoke`，smoke repair 的结果会落在 `ISSUE_BOT_STATE_DIR/smoke-test/` 下，并同步进入 preflight workflow 的 step summary。
- 定时 repair 在没有 `ha-candidate` 候选时会正常退出并打印日志，这属于空队列空转，不应视为故障。
- 在 runner 未就绪、`ISSUE_FIX_COMMAND` 未配置或 schedule 尚未显式启用时，workflow 会在 gate job 中直接结束并给出原因，避免 self-hosted 队列长时间 pending。
- 自动修复失败后，issue 会被标记为 `ha-failed` 并移出 `ha-candidate` 队列；需要人工复核后再重新添加 `ha-candidate` 才会再次参与自动修复。
- 对于默认会被 triage 归类为 `ha-manual` 的协议类问题（如 `gb28181` / `gat1400`），可以在 `workflow_dispatch` 时填写 `issue_number` 并把 `allow_manual_issue=true`，让 repair 流程按人工指定 issue 执行。

## 本地 smoke test

当你只想验证 repair 骨架，而不依赖真实 GitHub API 或 RK830 工具链时，可以直接运行:

```bash
bash tools/issue_bot/local_smoke_test.sh
```

脚本会自动完成以下动作:

- 创建临时 bare repo 和工作仓库
- 生成一个 `ha-candidate` mock issue JSON
- 调用 `repair_executor.py --mock-issue-json --skip-build-verify`
- 使用 `example_fix.sh` 生成一次真实代码改动
- 验证临时 worktree 和本地修复分支已清理

如已设置 `ISSUE_BOT_STATE_DIR`，smoke test 会把状态文件写入该目录；未设置时默认写到临时目录。

## 手工 mock repair

如果需要在指定仓库里手工演练 repair 执行器，可以使用:

```bash
python3 tools/issue_bot/repair_executor.py \
  --repo-dir /path/to/temp/repo \
  --state-dir /tmp/rk-gb-issue-bot \
  --repair-command /abs/path/to/tools/issue_bot/example_fix.sh \
  --mock-issue-json /path/to/mock_issue.json \
  --dry-run \
  --skip-build-verify
```

`--mock-issue-json` 只用于本地演练，避免本地测试依赖 `GITHUB_REPOSITORY`、`GITHUB_TOKEN` 和真实 issue API。
该参数既可以传单个 issue 对象，也可以传 issue 列表；传列表并配合 `--pick-next` 时，可本地模拟候选队列和空队列行为。

## 一键本地回归

如果要在接入 GitHub 之前，把 triage / preflight / smoke repair 串起来跑一遍，可以执行:

```bash
bash tools/issue_bot/regression_suite.sh --repo-dir /path/to/rk_gb
```

如需把 RK830 隔离构建也纳入回归，可以追加:

```bash
bash tools/issue_bot/regression_suite.sh \
  --repo-dir /path/to/rk_gb \
  --with-build-verify
```

脚本会输出 triage 摘要、preflight 摘要、smoke repair 摘要和可选 build verify 日志目录。

## 本机定时巡检

如果你希望在当前机器上定时查看 issue、自动分析并修复，同时不污染人工开发和其他 SoC 的编译环境，建议使用仓库内置的本机巡检脚本:

```bash
bash tools/issue_bot/local_cycle.sh --dry-run
```

脚本会执行以下动作:

- 从 `ISSUE_BOT_AUTOFIX_REPO_DIR` 读取 `origin` 地址
- 在 `ISSUE_BOT_AUTOFIX_MIRROR_DIR` 下维护一个独立仓库副本
- 对独立副本执行 `fetch/reset/clean/worktree prune`
- 先跑 `triage_issues.py`，再跑 `repair_executor.py`
- 自动把修复基线固定到 `silver`，除非显式通过 `--base-branch` 或 `ISSUE_BOT_BASE_BRANCH` 覆盖

注意:

- 真正执行自动修复前，仍然必须提供 `ISSUE_FIX_COMMAND`。仓库只负责调度，不内置具体“AI 修复器”实现。
- `local_cycle.sh` 会优先读取环境里的 `GITHUB_TOKEN`；如果未设置且机器上已登录 `gh`，会自动回退到 `gh auth token`。
- 隔离仓库位于独立目录，不会复用你手工开发中的工作区。

如果需要安装为当前用户的 cron 定时任务，可以先预演:

```bash
bash tools/issue_bot/install_local_timer.sh
```

确认输出无误后再执行:

```bash
bash tools/issue_bot/install_local_timer.sh --apply
```

默认每 15 分钟跑一次，并将日志写入 `${ISSUE_BOT_STATE_DIR}/cron.log`。如需单独处理某个 issue，可手工执行:

```bash
bash tools/issue_bot/local_cycle.sh --issue-number 27 --allow-manual
```

## 本地演练

只验证 triage 逻辑:

```bash
python3 tools/issue_bot/triage_issues.py --mock-json /path/to/mock_issue.json --dry-run
```

只验证 build 骨架:

```bash
RK_TOOLCHAIN_BIN=/path/to/toolchain/bin \
CMAKE_BIN=/path/to/cmake \
bash tools/issue_bot/build_verify.sh /path/to/rk_gb
```
