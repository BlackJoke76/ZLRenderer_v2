# Design

## Context

参见 proposal.md 的动机和范围。当前只有 AGENTS.md、本地 skills、OpenSpec 配置和无提交的 Git 仓库；无源代码、构建配置或可复用的窗口封装。当前分支为 master，未配置 remote。设计因引入 GLFW 和构建流程而必要。

本机 OpenSpec 的 schema 读取在 Windows 沙箱内因 Node realpathSync.native 返回 EPERM 而失败；允许的沙箱外调用已验证可正常加载。无需修改全局安装或复制 schema，后续 CLI 若复现相同限制，应使用获准执行方式。

## Goals / Non-Goals

**Goals:** 最少的源文件和明确的单线程生命周期；可重现依赖；从全新构建目录验证文档；每个 change 的验收证据与 Git 同步状态可追踪。

**Non-Goals:** 不提前创建 Application/Renderer 抽象，不引入 Vulkan/OpenGL 渲染、线程调度、多平台构建矩阵或通用资源管理框架。

## Decisions

1. 使用 CMake、C++20 和 MSVC x64，以 Visual Studio 2022 的 C++ 桌面工具链作为首个文档支持路径。CMake 最低版本取 3.24；目标级开启 C++20、关闭编译器扩展。相较手写 VS 工程，CMake 易于复用和验证；暂不维护多个 generator 的文档路径。
2. 用 CMake FetchContent 获取 GLFW，固定已验证 release 对应的完整提交 SHA；实施时核实版本和来源，关闭其示例、测试与文档构建。相较机器上的隐式安装或新增包管理器，本方式减少配置项；首次配置明确需要网络和 Git。
3. 初始结构为根 CMakeLists.txt、src/main.cpp、.gitignore 和文档。只有一个 executable target。使用 GLFW_NO_API 创建窗口，不生成渲染上下文；建议初始大小 1280×720，可调整大小，标题为 ZLRenderer。
4. 生命周期顺序为错误回调 → 初始化 GLFW → 创建窗口 → 检查关闭标志并处理事件 → 销毁窗口 → 终止 GLFW。空窗口阶段采用 glfwWaitEvents 等待事件，避免无工作的忙循环；后续需要连续渲染时在新 change 中改为帧循环。初始化失败直接返回非零；创建失败先终止 GLFW。所有函数和变量遵守 lowerCamelCase。
5. README 提供 PowerShell 下从干净目录执行的配置、Debug 构建和运行命令，并说明 Release 的配置参数与输出路径。ARCHITECTURE 只描述当前入口与生命周期；ROADMAP 将后续 renderer 阶段标为计划；STATUS 记录实际命令、窗口操作、退出码、Git 同步状态和限制。
6. AGENTS.md 只记录项目已约定的 skills 和实际开发流程，不逐项介绍 OpenSpec propose、apply、update、explore、sync、archive 的用途。C++ 工作使用 cpp-pro 和 karpathy-guidelines；Vulkan 工作引入 vulkan-renderer-optimization，游戏功能按需使用 game-developer。不约定自动使用其他不相关技能，也不授权自动委派 agent。规划完成后等待新的实现请求；验收后更新交接，归档使用对应技能，已授权的每个 change 独立提交并推送。
7. GitHub 创建是用户已请求的实施工作；执行前明确 owner、仓库名、可见性并检查 gh 认证和同名仓库。不猜测目标、不覆盖已有仓库。核实当前分支、origin 和 upstream 后再推送；不自动切换分支或强推。提交前逐项审查，仅暂存 change 相关文件，保留其他改动。
8. 将画面呈现偏好记录到 AGENTS.md，并在 ROADMAP 的后续 Vulkan swapchain 阶段引用：查询设备与 surface 支持的 present modes，支持 MAILBOX 时优先选择 MAILBOX，否则回退 FIFO；后续 change 必须验证选择与回退。此项只记录未来实现约定；本 change 的 GLFW_NO_API 空窗口没有 swapchain，不实现呈现模式。

## Risks / Trade-offs

- [首次获取依赖需要网络] → README 明确要求，固定 SHA；失败时记录下载错误，不宣称构建完成。
- [本机工具链可能未安装] → apply 起始检查并报告具体缺项，不隐式安装大型工具链。
- [窗口行为不能只由编译证明] → 在有桌面的 Windows 会话实际打开、移动、调整大小、点击关闭，记录退出码；无法操作 UI 时记录未验收。
- [失败分支难以自然触发] → 用临时故障注入验证初始化与创建失败后的非零退出和清理，恢复后再做最终构建；不为此新增产品开关。
- [GitHub 目标尚未确定] → 可以先完成本地实现与验收，但创建和推送必须等待目标信息；同步失败时保留本地成果并报告真实状态。

## Migration Plan

无现有运行行为需要迁移。新增工程后从全新 build 目录按 README 验收；更新 STATUS，完成相关变更提交及推送。后续若撤销，用独立 revert 保留历史，不重写远程历史。

## Open Questions

- 已确认 GitHub 目标为 BlackJoke76/ZLRenderer_v2，仓库已存在、可见性 PUBLIC，原 main 包含 MIT LICENSE。用户已选择保留远程 LICENSE 和历史、接到 main 并使用 HTTPS；复用目标仓库，不重复创建。
