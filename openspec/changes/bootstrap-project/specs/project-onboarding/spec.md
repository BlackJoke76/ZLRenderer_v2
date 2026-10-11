# Spec Delta

## Purpose

为新开发者提供可重现的 Windows 工程入门路径，并以项目文档说明当前架构、实际完成状态与后续方向，减少对个人机器配置或聊天记录的依赖。

## ADDED Requirements

### Requirement: Reproducible Windows onboarding

README SHALL 列明受支持的 Windows 工具链、依赖获取条件，以及从源码配置、编译和运行的完整命令，包含配置类型和可执行文件位置。

#### Scenario: Fresh build directory
- **WHEN** 开发者安装文档列出的前置工具，在没有构建产物的源码目录按 README 执行命令
- **THEN** 工程配置和编译成功，文档中的运行命令打开窗口，该窗口可正常关闭

### Requirement: Project orientation documents

项目 SHALL 提供 docs/ROADMAP.md、docs/STATUS.md 和简短的 docs/ARCHITECTURE.md，分别记录后续阶段、实际完成与验证状态、当前模块职责与窗口生命周期。

#### Scenario: Read project state
- **WHEN** 开发者查阅三个文档
- **THEN** 可以区分已实现功能与未来计划，并找到当前 change 的验收结果和已知限制

### Requirement: Recorded development workflow

AGENTS.md SHALL 保留既有命名和复用规则，并记录已约定 skills、简短的规划/实现/验收/归档流程，以及每个验收通过的 change 独立提交与推送的约定；不逐项介绍 OpenSpec 命令用途。

#### Scenario: Start subsequent change
- **WHEN** 开发者依据 AGENTS.md 开始后续 change
- **THEN** 能找到对应技能与流程，并知道未验收的 change 不能标记完成，推送前必须核实远程目标、分支和 upstream

### Requirement: Recorded presentation preference

AGENTS.md SHALL 记录后续画面呈现支持 MAILBOX 时优先使用 MAILBOX、否则回退 FIFO 的项目约定；ROADMAP SHALL 在未来 swapchain 阶段引用该约定，不将其描述为本次空窗口工程已实现的功能。

#### Scenario: Plan swapchain implementation
- **WHEN** 开发者查阅后续 swapchain 阶段的计划与项目规则
- **THEN** 能找到 MAILBOX 优先及 FIFO 回退约定，并明确需要在后续 change 中验证
