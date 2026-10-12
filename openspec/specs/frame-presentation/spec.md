# frame-presentation Specification

## Purpose

定义桌面窗口的持续帧呈现契约，使应用通过最小渲染接口显示配置的清屏颜色，并能验证交换链选择、帧资源复用、窗口变化和退出行为，为后续绘制功能提供稳定的基础。

## Requirements

### Requirement: Expose a minimal rendering interface

应用 SHALL 通过最小渲染接口初始化窗口呈现、请求一帧指定颜色的清屏并结束呈现生命周期。接口 SHALL 使用渲染配置表达颜色，不要求应用直接获取或操作 Vulkan 对象；后端 SHALL 独占其资源，窗口在呈现生命周期内保持有效。

#### Scenario: Render through the application boundary
- **WHEN** 应用初始化渲染接口并请求清屏帧
- **THEN** 后端完成获取、录制、提交和呈现，应用无需操作交换链图像、命令或同步对象

### Requirement: Choose a supported swapchain configuration

程序 SHALL 依据当前设备和 surface 支持的能力、格式、呈现模式、图像数量及实际 framebuffer 尺寸创建交换链。支持 MAILBOX 时 SHALL 优先使用 MAILBOX，否则 SHALL 使用 FIFO；配置与所选模式 SHALL 可由诊断记录核验，无可用配置时明确失败。

#### Scenario: Mailbox is supported
- **WHEN** 当前 surface 的可用呈现模式包含 MAILBOX
- **THEN** 选择 MAILBOX 并记录该选择

#### Scenario: Fifo fallback
- **WHEN** 当前 surface 不支持 MAILBOX 但支持 FIFO
- **THEN** 选择 FIFO 并记录回退原因，仍能持续呈现

#### Scenario: Respect surface bounds
- **WHEN** surface 限定尺寸或最少、最多图像数量
- **THEN** 创建参数满足这些限制，未限定最大数量时不将其当作零张图像

#### Scenario: Configuration is unavailable
- **WHEN** surface 没有兼容清屏的格式、使用能力或可用呈现模式
- **THEN** 报告具体原因，以非零状态结束而不进入无效帧循环

### Requirement: Continuously present the configured clear color

程序 SHALL 在窗口可呈现期间持续生成完整覆盖 framebuffer 的清屏帧，无需窗口输入事件触发下一帧。清屏配置 SHALL 使用有限且范围为 [0,1] 的线性 RGBA 分量，默认值为 (0.05, 0.10, 0.20, 1.0)；呈现颜色遵循实际 surface 格式的颜色编码，窗口移动与关闭仍保持响应。

#### Scenario: Sustained visible output
- **WHEN** 窗口保持可见且无输入事件至少 10 秒
- **THEN** 客户区持续显示配置颜色，成功提交及呈现记录证明发生多帧推进，无黑帧或旧窗口内容

#### Scenario: Alternative configured color
- **WHEN** 开发者通过初始化配置指定另一组有效 RGBA 并运行
- **THEN** 客户区显示对应颜色，无需修改后端的帧同步或录制流程

#### Scenario: Invalid color configuration
- **WHEN** 配置包含非有限值或范围外分量
- **THEN** 初始化明确拒绝该配置，不向 GPU 提交无效颜色帧

### Requirement: Synchronize acquisition rendering and presentation

后端 SHALL 只操作成功获取的交换链图像，在获取完成后执行清屏写入，并在写入完成且图像处于可呈现布局后显示。此契约 SHALL 同时适用于图形和呈现使用同族或分族队列的设备，不将成功提交等同于呈现引擎已完成使用同步对象。

#### Scenario: Successful frame
- **WHEN** 图像获取、命令录制、提交和呈现成功
- **THEN** 显示的是本次获取图像的完整清屏内容，获取与写入、写入与显示之间有有效同步

#### Scenario: Separate queue families
- **WHEN** 图形和当前 surface 呈现使用不同队列族
- **THEN** 交换链图像可由两个角色合法访问，画面和同步契约保持成立

### Requirement: Reuse completed frame and image resources

后端 SHALL 使用固定数量的帧资源，在其 GPU 提交完成后才重新录制或复用；图像相关资源 SHALL 按实际交换链图像归属管理，呈现等待对象只在其先前使用完成后复用。稳定运行期间 SHALL 不逐帧创建或销毁命令及同步对象，也不逐帧等待整个设备或队列空闲。

#### Scenario: Multiple frame slot cycles
- **WHEN** 连续帧跨越所有帧槽并多次回到同一帧槽
- **THEN** 旧提交完成后才复用该槽，无 pending 命令复位、同步对象重复使用或资源数量持续增长

#### Scenario: Image count differs from frame slot count
- **WHEN** 交换链图像数量与帧槽数量不同，或获取图像顺序与帧槽顺序不同
- **THEN** 资源按各自归属正确选取，呈现等待对象不因另一个帧槽完成而被提前复用

### Requirement: Recover from window and swapchain changes

程序 SHALL 在 framebuffer 尺寸改变、获取或呈现返回 OUT_OF_DATE 或 SUBOPTIMAL 时更新交换链相关资源并恢复配置颜色。framebuffer 为零时 SHALL 暂停呈现并等待窗口事件，期间仍处理关闭；有图像的 SUBOPTIMAL 获取 SHALL 正确结束其获取同步生命周期。

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

### Requirement: Handle aborted frames and rendering failures

后端 SHALL 区分可重建状态与不可恢复错误，在跳过的帧中保持帧槽可再次使用。录制、提交、呈现或等待失败时 SHALL 报告阶段及结果、停止正常帧推进，以非零状态结束并清理已创建资源，不等待未成功提交的工作。

#### Scenario: Acquisition requires reconstruction
- **WHEN** 获取返回 OUT_OF_DATE 且本帧未提交
- **THEN** 重建后下一帧可以继续，无因错误重置完成状态而永久等待

#### Scenario: Recording or submission fails
- **WHEN** 图像已获取而录制失败，或实际提交失败
- **THEN** 报告错误并停止正常循环，不继续复用未完成获取对象，不把该帧记为成功提交或等待其永远不会到来的完成信号

#### Scenario: Unrecoverable presentation or device failure
- **WHEN** 呈现、GPU 等待报告设备丢失、surface 丢失或其他不可恢复错误
- **THEN** 报告实际阶段及结果，以非零状态结束，不反复重建或无限重试

### Requirement: Validate presentation lifecycle

工程 SHALL 验证 Debug 和 Release 中的持续清屏、帧复用、重建与退出，不产生应用引起的 validation WARNING 或 ERROR；同步 validation 可用时 SHALL 启用并记录结果。测试 SHALL 保留原始诊断，明确区分真实硬件、合成输入及注入结果，不将未支持模式或分族的模拟覆盖报告为硬件实测。

#### Scenario: Validated runtime sequence
- **WHEN** 验收持续清屏、多次缩放、最小化恢复和关闭
- **THEN** 保存多帧、所选模式、恢复及销毁证据，正常退出码为零，未出现应用 validation 或同步错误

#### Scenario: Policy and exceptional path coverage
- **WHEN** 验证 MAILBOX/FIFO、分族、OUT_OF_DATE、SUBOPTIMAL 和失败路径
- **THEN** 选择策略与运行控制均有对应验证，报告准确列明真实与合成覆盖及环境限制
