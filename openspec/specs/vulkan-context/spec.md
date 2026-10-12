# vulkan-context Specification

## Purpose

定义 Windows 桌面窗口关联的 Vulkan 初始化和退出契约，使开发者能够确认图形与呈现能力、观察 validation 诊断，并验证完整与部分初始化状态下的对象生命周期。

## Requirements

### Requirement: Initialize Vulkan for the existing window

程序 SHALL 在现有窗口建立可用的 Vulkan instance、关联该窗口的 surface 和逻辑设备。只有全部初始化成功后才能报告成功并进入窗口主循环，窗口移动、调整大小与关闭行为继续符合 desktop-window 规格。

#### Scenario: Successful initialization
- **WHEN** 在满足 README 前置条件的 Windows 环境启动程序
- **THEN** 初始化成功信息明确可见，原窗口继续响应事件，并可正常关闭

### Requirement: Select a suitable physical device

程序 SHALL 只从具有非空图形队列族、支持当前 surface 呈现的非空队列族、`VK_KHR_swapchain` 以及兼容清屏呈现的 surface 格式、模式和图像使用能力的设备中选择物理设备，输出所选设备名称及两种队列族索引。图形和呈现能力允许位于同一或不同队列族；无合适设备时明确失败。

#### Scenario: Shared queue family
- **WHEN** 合适设备的一组队列族同时支持图形和当前 surface 呈现
- **THEN** 该设备可被选择，两个角色可使用同一队列族

#### Scenario: Separate queue families
- **WHEN** 合适设备的图形和当前 surface 呈现能力分属两个队列族
- **THEN** 该设备仍可被选择，并分别记录两个队列族索引

#### Scenario: No suitable device
- **WHEN** 没有设备同时满足图形、当前 surface 呈现及清屏交换链支持要求
- **THEN** 程序报告设备选择失败，不报告初始化成功，不进入主循环，并以非零退出码结束

#### Scenario: Queue support without swapchain support
- **WHEN** 某候选设备具备图形和呈现队列，但缺少交换链扩展或当前 surface 的清屏呈现条件
- **THEN** 不选择该候选，并继续检查后续设备；全部候选不适用时诊断指出交换链支持不足

### Requirement: Create and retrieve the selected queues

程序 SHALL 为所选物理设备创建启用 `VK_KHR_swapchain` 的逻辑设备，并获取有效的图形与呈现队列；同一队列族 SHALL 只请求一次，获取的队列 SHALL 属于逻辑设备创建时请求的队列族。

#### Scenario: One family serves both roles
- **WHEN** 图形与呈现角色使用同一队列族
- **THEN** 逻辑设备创建不重复请求该族，两个角色获得有效队列

#### Scenario: Two families serve the roles
- **WHEN** 图形与呈现角色使用不同队列族
- **THEN** 逻辑设备创建包含两个选中的族，两种角色分别获得有效队列

#### Scenario: Swapchain device extension enabled
- **WHEN** 逻辑设备成功创建并开始初始化呈现资源
- **THEN** 交换链设备扩展已启用，后端能够合法调用交换链 API

### Requirement: Enable validation and observable diagnostics

程序 SHALL 在 Debug 和 Release 均启用 Khronos validation，并输出包含严重级别与消息内容的诊断，覆盖 instance 创建、后续初始化和清理。所需 layer 或诊断扩展缺失时 SHALL 明确失败，不静默关闭 validation。

#### Scenario: Normal validated lifecycle
- **WHEN** validation 可用且程序正常启动和关闭
- **THEN** 输出确认 validation 已启用，初始化和资源清理不产生由应用错误引起的 validation WARNING 或 ERROR

#### Scenario: Validation dependency missing
- **WHEN** validation layer 或所需诊断扩展不可用
- **THEN** 程序指出具体缺项，以非零退出码结束，不进入主循环

#### Scenario: Diagnostic message is delivered
- **WHEN** validation 或受控验证产生需报告的诊断
- **THEN** 输出包含该消息的严重级别和内容，回调不让异常跨越 Vulkan 调用边界

### Requirement: Destroy objects before their dependencies

程序 SHALL 在正常关闭时完成已有 GPU 提交，并先释放交换链、帧资源等 device 子对象，再按逻辑设备 → surface → debug messenger → instance → 窗口 → GLFW 的顺序释放对象并以 0 退出；每个显式拥有对象只销毁一次，physical device、queue 和交换链图像不作为独立拥有对象销毁。等待失败 SHALL 报告并导致非零退出，基础 WSI 呈现清理的已知限制 SHALL 在文档中明示。

#### Scenario: Orderly shutdown
- **WHEN** 初始化成功后用户通过系统关闭按钮退出
- **THEN** 清理记录体现上述顺序，窗口关联的 Vulkan 对象在窗口销毁前释放，无对象生命周期 validation 错误或残留应用进程

#### Scenario: Close with submitted frames
- **WHEN** 用户在已有 GPU 帧提交时请求关闭
- **THEN** 提交完成后才释放相关命令、同步和图像视图等资源，device 子对象先于 device 清理

#### Scenario: Failure during GPU completion wait
- **WHEN** 退出时的 GPU 等待返回不可恢复错误
- **THEN** 保存错误诊断并以非零状态结束，继续清理已拥有资源，不伪报为正常关闭

#### Scenario: Partial presentation initialization
- **WHEN** device 创建后交换链或某个帧资源的初始化失败
- **THEN** 已创建的呈现子对象先于 device 清理，随后按原依赖顺序释放其余对象，未创建对象不销毁

### Requirement: Roll back partial initialization

程序 SHALL 在任意初始化阶段失败时报告阶段及错误原因，只销毁已成功创建的对象，保持依赖顺序并以非零退出码结束。失败路径 SHALL 与正常路径复用清理职责，不进入窗口主循环。

#### Scenario: Failure after instance creation
- **WHEN** instance 已创建而后续初始化失败
- **THEN** instance 及已经成功创建的子对象按依赖顺序释放，未创建对象不被销毁

#### Scenario: Failure after surface creation
- **WHEN** surface 已创建而设备选择或逻辑设备创建失败
- **THEN** surface、诊断对象和 instance 被释放，并继续释放窗口和 GLFW

#### Scenario: Failure after device creation
- **WHEN** 逻辑设备已创建而完成初始化前发生失败
- **THEN** 逻辑设备先释放，随后按同一依赖顺序清理其他已创建对象，无重复销毁或泄漏
