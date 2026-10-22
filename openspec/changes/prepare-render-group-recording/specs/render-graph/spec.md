# Spec Delta

## MODIFIED Requirements

### Requirement: Separate logical passes from physical execution groups

编译结果 SHALL 分别表达逻辑 pass 和物理执行组。本轮 SHALL 将每个 raster pass 放入独立执行组，保留逻辑依赖并据分组生成组边界同步；资源出口不增加渲染组。执行 SHALL 消费分组结果，逻辑 pass 声明不得固定自身 command buffer 或 submit 数量。本轮不承诺自动合并算法。

#### Scenario: Preserve one to one grouping
- **WHEN** 编译两个有效逻辑 raster pass 和一个呈现出口
- **THEN** 得到两个按顺序执行的物理组，每个包含对应逻辑 pass，出口不成为第三次渲染或额外提交

#### Scenario: Execute through group boundaries
- **WHEN** 后端执行编译结果
- **THEN** 根据执行组确定渲染范围与边界同步，组内逻辑操作和诊断可互相关联，不绕过分组另走固定清屏路径

### Requirement: Validate compiler and clear execution at distinct coverage levels

工程 SHALL 分别验证 CPU 依赖推导、生产后端参数映射和真实 GPU 执行。真实 GPU 交付范围为单清屏与测试目标双清屏；采样读取等其他用途只按本轮实际 CPU/映射证据报告，不宣称采样绘制已提供或实测。Debug/Release SHALL 保持既有呈现回归、MAILBOX/FIFO 策略与同步诊断契约，并保留基础 WSI 限制。

#### Scenario: Verify compiler cases without a GPU
- **WHEN** 执行独立 CPU 测试
- **THEN** 不依赖窗口或 Vulkan 设备即可验证 RAW/WAR/WAW、所有读取者、布局变化、内容有效性、确定顺序、引用错误及一对一分组

#### Scenario: Verify two clear passes on the GPU
- **WHEN** Debug 和 Release 的测试目标在真实获取图像上依次完整清除为颜色 A、B
- **THEN** 两个完整渲染组分别录入不同 primary command buffer，按组序一次提交两个 primary，最终 25 点采样匹配 B；原始记录证明实际 barrier 和绑定与计划一致，资源平衡且无应用 validation/sync WARNING/ERROR

#### Scenario: Preserve the presentation regression baseline
- **WHEN** Debug 和 Release 验收默认/替代色、持续至少 10 秒、多次缩放、至少三次最小化恢复及可见/最小化关闭
- **THEN** 保持完整清屏、当前代绑定、暂停计数冻结、恢复推进和正常条件下 5 秒内退出 0，保存原始证据，准确列明真实硬件、合成输入、状态控制和未实测条件
## ADDED Requirements

### Requirement: Prepare frame execution before recording groups

后端 SHALL 将帧准备、执行组录制和统一提交分为明确阶段。准备 SHALL 在成功获取后校验当前代图像绑定并解析全部组所需附件与同步参数；组录制 SHALL 只消费本帧准备结果，不获取图像、重建交换链或推进提交状态。准备结果 SHALL 仅在本帧有效，不跨重建持有旧代附件。

#### Scenario: Prepare the acquired image independently from the frame slot
- **WHEN** 成功获取的图像索引不同于当前帧槽索引
- **THEN** 所有组的附件、转换与最终呈现关联同一已获取图像和当前代；命令及获取同步仍属于当前帧槽

#### Scenario: Reject preparation before recording
- **WHEN** 任一绑定缺失、过期、不兼容，或计划要求未支持的录制能力
- **THEN** 准备失败，不开始任何组录制或提交，不将该帧报告为成功

### Requirement: Record complete physical groups into independent primaries

后端 SHALL 为每个完整物理执行组录制一个独立 primary，并按编译组序串行调用录制。每个 primary SHALL 包含该组全部入口转换、完整渲染范围和出口转换；渲染范围不得跨 primary 拆分。最终呈现转换 SHALL 保持在计划指定组的出口，不创建额外渲染组或转换专用 primary。

#### Scenario: Record two complete clear groups
- **WHEN** 录制同图像的双清屏计划
- **THEN** 产生两个不同的 primary，每个独立 begin/end；第二组前的颜色写入依赖保持颜色附件布局，最终呈现转换仅在第二组渲染结束后出现一次

#### Scenario: Keep a single group complete
- **WHEN** 录制单组清屏计划
- **THEN** 唯一 primary 包含获取后的入口转换、完整清屏渲染范围和最终呈现转换，保持原有颜色及 load/store 语义

### Requirement: Submit all recorded groups as one ordered frame

后端 SHALL 仅在所有组成功完成录制后，将全部 primary 按计划组序放入一个提交描述，并通过一次队列提交关联一个帧槽 fence。获取等待 SHALL 覆盖目标首次使用，完成信号 SHALL 属于已获取图像并覆盖最后写入和出口转换。后端 SHALL 只在提交成功后推进帧槽及提交计数、发布成功执行诊断，不提交部分组。

#### Scenario: Submit an ordered primary array
- **WHEN** 双清屏的两个 primary 均录制成功
- **THEN** 实际提交数组按第一组、第二组排列且数量为二；一次提交只等待一次获取信号并发出一次图像呈现信号，随后请求呈现第二次清屏内容

#### Scenario: Abort after an earlier group has recorded
- **WHEN** 前组已完成而后组 begin、组内录制或 end 失败
- **THEN** 不提交任何本帧 primary、不请求本帧呈现、不重置本帧提交 fence，不推进帧槽及提交计数；报告失败组和阶段，停止循环并非零清理

#### Scenario: Submission fails after resetting the fence
- **WHEN** 所有组已完成但统一提交失败
- **THEN** 不推进提交状态或发布成功执行记录，停止正常循环且不等待该次未成功提交工作的 fence，不复用尚未消费的本帧获取信号

### Requirement: Reuse group primaries with frame slot lifetime

后端 SHALL 在固定计划下为每个帧槽预分配组 primary 集合，稳定帧不创建或释放命令及同步对象。整个集合 SHALL 由该槽同一次提交的完成状态保护，只有确认旧提交完成后才能复位和重录。重建 SHALL 解除全部槽内旧命令引用后清理旧附件，继续复用帧槽对象及 primary 集合。

#### Scenario: Revisit a frame slot
- **WHEN** 多帧后重新使用同一帧槽
- **THEN** 等待覆盖该槽全部组的提交完成后复位其命令池，复用同一组 primary 集合；其他未完成槽不被复位

#### Scenario: Rebuild with several primaries per slot
- **WHEN** 双清屏执行多次交换链重建
- **THEN** 所有槽旧 GPU 工作完成且旧命令引用解除后才销毁旧附件，新帧准备引用完整新代，组 primary 与同步对象身份和数量保持稳定

#### Scenario: Skip without command or fence mutation
- **WHEN** fence 或 acquire 超时、acquire NOT_READY/OUT_OF_DATE，或窗口暂停、零 extent、关闭
- **THEN** 不开始本帧组录制或提交、不重置其提交 fence、不推进帧槽与计数，并保留现有恢复规则；有效 SUBOPTIMAL 获取仍走完整录制和统一提交路径

### Requirement: Expose actual group recording and submission identity

诊断 SHALL 从实际录制与提交输入关联组索引、primary 身份、组内 barrier/渲染范围及提交数组位置，并保留代号、计划版本、帧槽和已获取图像关联。成功记录 SHALL 仅在统一提交成功后发布；失败记录 SHALL 定位准备、具体组录制或提交阶段。正常完整计划仍按每代每计划首次成功提交输出，测试逐帧采集保持有界。

#### Scenario: Verify actual ordered submission inputs
- **WHEN** 测试检查双清屏实际执行记录
- **THEN** 可验证两个组使用不同 primary，barrier 与渲染位于对应 primary 内，实际提交数量和顺序与组一致，且唯一呈现转换位于最终组；固定的 submits=1 文本不能替代调用输入证据

#### Scenario: Diagnose partial recording without claiming submission
- **WHEN** 第二组失败而第一组已完成录制
- **THEN** 可识别失败组与阶段，已录制部分不被标成成功提交，完整计划的首次成功报告状态不被失败帧消耗
