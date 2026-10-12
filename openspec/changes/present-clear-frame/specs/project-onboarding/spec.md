# Spec Delta

## MODIFIED Requirements

### Requirement: Recorded presentation preference

AGENTS.md SHALL 记录画面呈现支持 MAILBOX 时优先使用 MAILBOX、否则回退 FIFO 的项目约定；ROADMAP SHALL 区分交换链的已完成阶段和后续计划。呈现实施验收后，README 和 STATUS SHALL 说明模式选择、验证方法与实际硬件结果，不将合成回退覆盖描述为实际设备模式。

#### Scenario: Plan swapchain implementation
- **WHEN** 开发者查阅后续呈现功能的计划与项目规则
- **THEN** 能找到 MAILBOX 优先及 FIFO 回退约定，并明确相关 change 需要验证模式选择与回退

#### Scenario: Read implemented presentation state
- **WHEN** present-clear-frame 完成验收后开发者查阅 README、STATUS 和 ROADMAP
- **THEN** 能按文档运行持续清屏，找到实际所选模式、FIFO 回退与同步验证的覆盖方式，并区分已完成与后续功能
