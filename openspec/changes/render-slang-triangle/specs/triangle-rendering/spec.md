# Spec Delta

## Purpose

定义 Slang 着色器驱动的最小三角形绘制契约，让示例应用可验证从构建产物到持续画面的完整路径，并在既有窗口、帧同步和交换链重建行为之上提供可重复验证的彩色绘制结果。

## ADDED Requirements

### Requirement: Build and deploy Slang shaders

工程 SHALL 从受版本控制的 Slang 源码构建顶点和片元 SPIR-V 产物，明确编译工具、入口及兼容当前 Vulkan 基线的目标，自动部署到对应配置的可执行文件资源目录。缺少工具或编译失败 SHALL 明确使配置或构建失败，不使用过期产物掩盖错误。

#### Scenario: Clean and incremental builds
- **WHEN** 全新构建 Debug/Release，或修改着色器后增量构建
- **THEN** 对应入口产物由源码生成并部署，修改触发重编译，无修改时不重复编译

#### Scenario: Tool or source failure
- **WHEN** 显式指定的编译器无效，或着色器包含编译错误
- **THEN** 配置或构建非零失败，报告路径或编译诊断，不报告应用已成功更新

#### Scenario: Launch outside the build directory
- **WHEN** 从任意工作目录启动已部署应用
- **THEN** 从可执行文件对应资源目录加载正确产物，无需 Slang 运行时编译器或源码目录

### Requirement: Select triangle content through the rendering boundary

渲染配置 SHALL 支持显式选择三角形或仅清屏内容。示例应用 SHALL 选择三角形，未指定内容的现有调用 SHALL 保持仅清屏行为及原清屏颜色配置；应用无需直接操作 Vulkan 绘制对象。

#### Scenario: Demonstration application
- **WHEN** 正常运行示例应用
- **THEN** 背景清屏后显示一个静态彩色三角形

#### Scenario: Existing clear only caller
- **WHEN** 调用方不指定内容或显式选择仅清屏
- **THEN** 持续显示完整配置颜色，原清屏验收继续成立，且不要求加载三角形资源

### Requirement: Display an interpolated color triangle

三角形模式 SHALL 持续显示一个位于画面中央、具有红绿蓝顶点插值颜色的不透明三角形，外部保持配置背景；每个可呈现帧完整重绘，不依赖输入事件。几何位置以归一化坐标固定，非方形窗口允许按窗口比例伸缩。

#### Scenario: Sustained triangle output
- **WHEN** 窗口稳定显示至少10秒
- **THEN** 多帧推进，三角形内部多个采样点符合插值颜色，外部及边缘符合背景，不以单点非背景判定成功

#### Scenario: Changed background
- **WHEN** 指定另一有效清屏颜色并选择三角形
- **THEN** 三角形颜色保持，外部区域使用新背景

### Requirement: Preserve drawing across presentation transitions

三角形模式 SHALL 遵循既有帧呈现的尺寸、暂停、oldSwapchain 替换和模式选择契约，在新一代配置就绪后使用兼容的绘制资源恢复画面；稳定期间 SHALL 不逐帧创建管线或增加设备空闲等待。

#### Scenario: Resize and repeated restore
- **WHEN** 快速缩放后稳定，并执行至少三轮最小化及恢复
- **THEN** 每个稳定尺寸显示完整三角形和背景，暂停计数冻结，恢复帧推进，稳定期不重复重建或创建管线

#### Scenario: Generation compatibility
- **WHEN** 交换链 extent、格式或实际图像数量改变
- **THEN** 当前绘制资源与新附件兼容，旧命令引用不被继续使用；无法硬件复现的配置变化明确以合成或代码核查补充

### Requirement: Fail and release drawing resources correctly

着色器加载、模块、布局或绘制管线创建失败 SHALL 报告阶段及原因，停止正常帧推进并非零退出；已创建绘制资源 SHALL 在安全生命周期内释放且仅释放一次，不呈现半成品资源或回退到已退役旧链。

#### Scenario: Invalid deployed shader
- **WHEN** 三角形所需产物缺失、为空或格式无效
- **THEN** 明确报告文件及加载原因，非零退出并清理已有资源

#### Scenario: Partial pipeline creation failure
- **WHEN** 初建或替换期间在部分绘制对象已创建后失败
- **THEN** 不使用未完成资源提交，已创建对象清理一次，无无效句柄销毁或永久等待

### Requirement: Validate triangle rendering and shutdown

验收 SHALL 覆盖 Debug/Release 三角形画面、重复窗口变化及可见和最小化关闭，正常设备条件下关闭请求后5秒内退出0，无应用引起的 validation WARNING/ERROR，同步 validation 可用时启用。证据 SHALL 区分硬件、合成及注入，保留现有环境诊断归因和基础WSI限制。

#### Scenario: Complete acceptance sequence
- **WHEN** 两配置运行三角形窗口流程和既有呈现回归
- **THEN** 保留逐尺寸截图、像素采样、帧/代数与资源释放证据，并记录实际模式和退出结果

#### Scenario: Supported mode policy
- **WHEN** 验证呈现模式选择
- **THEN** MAILBOX 优先、FIFO 回退均有测试，并明确本机实际模式而不将合成覆盖称为硬件实测
