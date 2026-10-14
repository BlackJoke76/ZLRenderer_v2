# Spec Delta

## MODIFIED Requirements

### Requirement: Recover from window and swapchain changes

程序 SHALL 在 framebuffer 尺寸改变、获取或呈现返回 OUT_OF_DATE 或 SUBOPTIMAL 时更新交换链相关资源并恢复配置颜色。重建准备完成后 SHALL 复查窗口状态，以最新非零 framebuffer 像素尺寸及当前 surface 限制选择配置；窗口已关闭、最小化或所选 extent 为零时 SHALL 不开始新一代呈现。framebuffer 为零或窗口最小化时 SHALL 暂停呈现并等待窗口事件，期间仍处理关闭，保留尚未完成的重建请求；有图像的 SUBOPTIMAL 获取 SHALL 正确结束其获取同步生命周期。

#### Scenario: Resize or out of date
- **WHEN** 用户多次调整窗口大小，或获取/呈现报告 OUT_OF_DATE
- **THEN** 使用最新非零 framebuffer 尺寸重建，恢复完整清屏，无死锁或旧尺寸资源的后续使用

#### Scenario: Suboptimal acquisition or presentation
- **WHEN** 获取或呈现报告 SUBOPTIMAL
- **THEN** 不将已获取图像及其同步状态当作获取失败丢弃，并在完成必要处理后更新交换链

#### Scenario: Minimize and restore
- **WHEN** framebuffer 变为零后恢复为非零
- **THEN** 零尺寸期间不提交无效呈现或忙循环，恢复后重新呈现配置颜色

#### Scenario: Close while minimized
- **WHEN** 用户在 framebuffer 为零期间请求关闭
- **THEN** 等待事件的流程响应关闭并正常退出，不等待永远不会发生的非零尺寸

#### Scenario: Window changes during reconstruction preparation
- **WHEN** 正在为重建等待旧工作完成，窗口又改变尺寸、最小化或收到关闭事件
- **THEN** 开始资源重建前处理已到达的事件并复查状态；新尺寸替代旧快照，最小化延迟重建，关闭停止帧推进并正常退出

#### Scenario: Iconified window retains a nonzero framebuffer
- **WHEN** 窗口最小化但窗口系统仍报告非零 framebuffer
- **THEN** 仍进入暂停状态，不因保留尺寸而继续获取、提交或呈现

#### Scenario: Pending reconstruction survives repeated pause and resume
- **WHEN** 重建请求尚未完成时窗口暂停，并重复恢复到相同或不同的非零尺寸
- **THEN** 请求不丢失，恢复后完成必要重建并持续清屏；没有重建请求且配置仍有效时不强制重建同尺寸交换链

#### Scenario: Presentation starts in a paused state
- **WHEN** 呈现初始化时窗口最小化或 framebuffer 为零
- **THEN** 延迟创建交换链但继续处理窗口事件，恢复后开始持续清屏，恢复前请求关闭也能正常退出

#### Scenario: Surface selects a zero extent
- **WHEN** framebuffer 非零但当前 surface 限制使所选 extent 的任一维度为零
- **THEN** 不创建零 extent 交换链，不向该状态提交帧，以处理事件的有界等待延迟重试，恢复有效配置后继续呈现

#### Scenario: Stable size after a resize burst
- **WHEN** 一组快速尺寸变化结束后窗口尺寸与 surface 配置保持稳定，且没有新的重建状态
- **THEN** 最终使用最后可呈现尺寸，随后继续多帧呈现而不持续重复重建

### Requirement: Handle aborted frames and rendering failures

后端 SHALL 区分可重建状态与不可恢复错误，在跳过的帧中保持帧槽可再次使用。录制、提交、呈现、等待或重建资源创建失败时 SHALL 报告阶段及结果、停止正常帧推进，以非零状态结束并清理已创建资源，不等待未成功提交的工作；未完成的一代资源 SHALL 不用于帧呈现。

#### Scenario: Acquisition requires reconstruction
- **WHEN** 获取返回 OUT_OF_DATE 且本帧未提交
- **THEN** 重建后下一帧可以继续，无因错误重置完成状态而永久等待

#### Scenario: Recording or submission fails
- **WHEN** 图像已获取而录制失败，或实际提交失败
- **THEN** 报告错误并停止正常循环，不继续复用未完成获取对象，不把该帧记为成功提交或等待其永远不会到来的完成信号

#### Scenario: Unrecoverable presentation or device failure
- **WHEN** 呈现、GPU 等待报告设备丢失、surface 丢失或其他不可恢复错误
- **THEN** 报告实际阶段及结果，以非零状态结束，不反复重建或无限重试

#### Scenario: Reconstruction fails after partial resource creation
- **WHEN** 重建已创建部分交换链相关对象，后续图像视图、framebuffer、render pass 或呈现同步对象的创建失败
- **THEN** 不使用半成品资源提交或呈现，也不回退到已退役旧交换链继续 acquire，明确报告阶段并非零退出，新旧两代已创建对象只清理一次且无无效句柄销毁

#### Scenario: Replacement creation fails after retiring the old swapchain
- **WHEN** 传入有效旧交换链的新交换链创建调用失败
- **THEN** 将旧交换链视为已退役，不再从它获取图像，不再次将它作为未退役旧交换链用于创建，报告实际错误并清理旧资源后非零退出

### Requirement: Validate presentation lifecycle

工程 SHALL 验证 Debug 和 Release 中的持续清屏、帧复用、重建与退出，不产生应用引起的 validation WARNING 或 ERROR；同步 validation 可用时 SHALL 启用并记录结果。验收 SHALL 核对逐次 framebuffer 与所选 extent、逐代资源、暂停期间无新提交及重复恢复后的帧推进；正常关闭 SHALL 在正常设备工作条件下于请求后 5 秒内退出 0。测试 SHALL 保留原始诊断，明确区分真实硬件、合成输入及注入结果，不将未支持模式或分族的模拟覆盖报告为硬件实测。

#### Scenario: Validated runtime sequence
- **WHEN** 验收持续清屏、多次缩放、最小化恢复和关闭
- **THEN** 保存多帧、所选模式、恢复及销毁证据，正常退出码为零，未出现应用 validation 或同步错误

#### Scenario: Policy and exceptional path coverage
- **WHEN** 验证 MAILBOX/FIFO、分族、OUT_OF_DATE、SUBOPTIMAL 和失败路径
- **THEN** 选择策略与运行控制均有对应验证，报告准确列明真实与合成覆盖及环境限制

#### Scenario: Repeated window transitions have measurable evidence
- **WHEN** Debug 和 Release 各运行多次缩放及至少三次最小化/恢复，并分别从可见和最小化状态关闭
- **THEN** 每个稳定尺寸有像素尺寸/extent 和完整清屏证据，暂停后无新提交，恢复后重新推进帧，稳定期间无重复重建，关闭均在正常工作条件下 5 秒内退出 0

#### Scenario: Boundary coverage supplements actual windows
- **WHEN** 真实桌面不能可靠产生零 framebuffer、准备期间关闭或跨代格式/图像数量变化
- **THEN** 以合成配置、测试目标状态控制或代码核查补充对应证据，明确覆盖方式，不伪造真实 Vulkan 对象或将控制测试称为硬件实测

## ADDED Requirements

### Requirement: Rebuild swapchain resources as a complete generation

每次重建 SHALL 重新查询当前 surface 配置，以当前未退役交换链作为 `oldSwapchain` 创建新交换链，保持 MAILBOX 优先、FIFO 回退策略；初次创建 SHALL 使用空旧句柄。后端 SHALL 按实际返回图像数完成新一代资源后才用于呈现，保留旧句柄至新交换链创建调用之后，并在旧 GPU 工作完成及旧命令引用解除后清理旧资源；固定帧资源 SHALL 保持复用。创建调用后的旧链 SHALL 视为已退役，失败时不得继续 acquire。逐代诊断及基础 WSI 呈现完成限制 SHALL 如实记录。

#### Scenario: Initial creation has no predecessor
- **WHEN** 首次创建交换链且不存在旧交换链
- **THEN** 使用空 oldSwapchain，并在完整资源集合就绪后开始呈现

#### Scenario: Replacement refers to the live predecessor
- **WHEN** 当前交换链需要重建且新配置有效
- **THEN** 传入该旧链的有效未退役句柄作为 oldSwapchain，创建调用前不销毁旧链；完整新一代就绪后切换，旧资源清理一次，后续不从旧链 acquire

#### Scenario: Every image has current attachments
- **WHEN** 重建成功且交换链返回实际图像集合
- **THEN** 每个图像对应当前代的视图、匹配格式与 extent 的 framebuffer 及图像级呈现同步对象，后续帧只引用这代资源

#### Scenario: Configuration changes between generations
- **WHEN** 重新查询后格式、extent、图像数量或支持的呈现模式变化
- **THEN** 新资源集合匹配新配置和实际图像数量，render pass 与附件格式兼容，并重新遵循 MAILBOX 优先/FIFO 回退策略

#### Scenario: Frame resources survive reconstruction
- **WHEN** 多次重建后继续呈现
- **THEN** 固定帧槽的命令及提交同步对象继续复用，旧提交完成后才复位命令引用，无跨代旧 framebuffer 引用或逐次增加的帧资源

#### Scenario: Generation cleanup and completion evidence
- **WHEN** 多代交换链已被替换并完成退出清理
- **THEN** 逐代已创建的自有对象各释放一次、借用图像不被独立销毁，记录实际 GPU 等待及基础 WSI 限制，不以提交 fence 或 WaitIdle 替代未提供的呈现完成保证
