# Tasks

## 1. Windows 构建工程

- [x] 1.1 检查 CMake、Git、MSVC x64 工具链版本及工作区状态，记录可用版本或具体阻塞项；验证工具命令能执行。
- [x] 1.2 核实 GLFW 官方 release 与完整提交 SHA，建立 CMake C++20 executable target、固定 FetchContent 依赖和 .gitignore；验证配置成功、依赖版本明确且构建产物不出现在待提交文件中。
- [x] 1.3 写 README 的前置条件、PowerShell 配置/Debug 编译/运行命令与 Release 参数；以一次实际配置和编译核对路径及命令。

## 2. 窗口生命周期

- [x] 2.1 实现 src/main.cpp 的错误回调、初始化、无图形 API 窗口、事件等待循环和清理；验证编译成功且窗口能打开、移动、调整大小并由关闭按钮正常退出，退出码为 0。
- [x] 2.2 验证初始化与窗口创建失败分支：通过临时故障注入检查错误输出、非零退出码和已初始化资源的清理，撤销注入后重新编译，确认无故障注入代码遗留。
- [x] 2.3 补充简短 docs/ARCHITECTURE.md 的文件职责和初始化→主循环→关闭时序，以及 README 的窗口预期行为；逐项对照实际实现。

## 3. 计划与开发流程

- [x] 3.1 编写 docs/ROADMAP.md，区分本 change 与后续渲染阶段，在后续 Vulkan swapchain 阶段引用 MAILBOX 优先、FIFO 回退约定；验证未来功能未被写为已完成。
- [x] 3.2 更新 AGENTS.md，保留现有规则，记录已约定 skills、简短开发流程、已授权提交推送规则和 MAILBOX 优先、FIFO 回退约定，不逐项介绍 OpenSpec 命令用途；检查规划/实现边界、远程核实和后续呈现模式验证要求明确。
- [x] 3.3 建立 docs/STATUS.md，记录当前 change、实际工具版本、验证命令与结果、限制及同步状态；检查每项状态有证据且待完成项明确。

## 4. 集成验收

- [x] 4.1 从全新构建目录逐字执行 README 的配置、编译和运行命令，实际完成窗口交互及关闭并记录退出码；将验收证据更新到 STATUS。
- [x] 4.2 对照两份 capability 规格和任务检查完成情况，运行 openspec validate bootstrap-project --strict，审查 diff 与命名/复用规则；仅在全部验收通过后标记实现完成。

## 5. GitHub 仓库与变更交付

- [x] 5.1 确认用户指定的 BlackJoke76/ZLRenderer_v2 仓库与 GitHub 认证；目标已存在且为 PUBLIC，复用而不重复创建，保留 LICENSE 和历史；核实仓库 URL、main 分支和 HTTPS remote（用户已授权）。
- [x] 5.2 核实当前分支、remote/upstream，更新交接记录并只暂存本 change 相关文件；创建含 bootstrap-project 名称的独立提交，以 staged diff 和提交 SHA 验证范围。
- [x] 5.3 推送到已确认目标并设置或核实 upstream；比较本地与远程 SHA，报告目标分支、验证结果、推送状态和 GitHub 提交链接，失败时记录实际状态。

## Workflow follow-up

- 若用户请求归档，使用 openspec-archive-change 完成归档及主规格同步，验证归档结果，并按项目规则提交和推送其记录。
- 未验收、未创建仓库或未同步的工作必须如实保留为未完成，不将规划完成等同于实现完成。
