# Proposal

## Why

当前项目只有开发规则和 OpenSpec 配置，缺少可运行的工程。需要建立可从 README 重现的 Windows 窗口程序，作为后续 renderer 开发的起点，并让每个验收通过的 change 有明确的 GitHub 记录。

## What Changes

- 建立 Windows CMake、C++20、GLFW 工程，支持配置、编译与运行。
- 实现窗口创建、事件主循环、关闭请求和资源释放；初始化失败时输出错误并返回非零退出码。
- 补充 README、docs/ROADMAP.md、docs/STATUS.md、简短的 docs/ARCHITECTURE.md。
- 在 AGENTS.md 记录已约定的 skills、propose/apply/验收/归档流程，以及每个 change 验收后独立提交并推送的规则。
- AGENTS.md 不逐项介绍 OpenSpec 命令用途；记录后续画面呈现支持 MAILBOX 时优先使用、否则回退 FIFO 的约定，并在 ROADMAP 引用，具体 swapchain 实现留给后续 change。
- 实施阶段确认 GitHub 仓库目标，创建仓库并配置 remote/upstream；验收后提交与推送本 change。
- 本 change 不包含 Vulkan、渲染管线、场景系统或任务系统。

## Capabilities

### New Capabilities

- `desktop-window`: Windows 桌面程序的启动、事件处理、正常关闭和启动失败行为。
- `project-onboarding`: 新开发者通过文档从头配置、编译和运行，以及查阅架构、路线和实际进度。

### Modified Capabilities

无；当前没有主规格。

## Impact

新增 CMake 配置、程序入口、依赖配置、忽略规则和项目文档；更新 AGENTS.md。引入 GLFW 构建依赖，需要 Windows C++ 工具链和首次依赖获取的网络连接。GitHub 仓库创建需要已认证账号及明确的仓库名称、owner 和可见性；不得猜测这些设置。
