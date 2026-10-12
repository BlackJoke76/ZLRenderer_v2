# Spec Delta

## MODIFIED Requirements

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
