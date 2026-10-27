# Spec Delta

## MODIFIED Requirements

### Requirement: Record complete physical groups into independent primaries

后端 SHALL 为每个完整物理执行组录制一个独立 primary，支持显式零、单、多 worker 配置且共用相同组录制逻辑。零 worker SHALL 在协调线程按组序执行；正数配置 SHALL 在固定 worker 上执行，允许不同组的 CPU 录制重叠。每个 primary SHALL 包含该组全部入口转换、完整渲染范围和出口转换；渲染范围不得跨 primary 拆分。最终呈现转换 SHALL 保持在计划指定组的出口，不创建额外渲染组或转换专用 primary。CPU 录制顺序 SHALL 不改变 GPU 计划顺序与同步需求。

#### Scenario: Record two complete clear groups
- **WHEN** 录制同图像的双清屏计划
- **THEN** 产生两个不同的 primary，每个独立 begin/end；第二组前的颜色写入依赖保持颜色附件布局，最终呈现转换仅在第二组渲染结束后出现一次

#### Scenario: Keep a single group complete
- **WHEN** 录制单组清屏计划
- **THEN** 唯一 primary 包含获取后的入口转换、完整清屏渲染范围和最终呈现转换，保持原有颜色及 load/store 语义

#### Scenario: Select inline single and multiple workers
- **WHEN** 分别配置零、一个和多个 worker 执行同一有效计划
- **THEN** 零 worker 不创建后台线程；正数配置仅由稳定索引的 worker 录制，协调线程等待时不帮助执行；三种配置均完整录制每组恰好一次并保持相同计划语义，实际启用数量可检查

### Requirement: Reuse group primaries with frame slot lifetime

后端 SHALL 在固定计划和 worker 配置下按帧槽及录制 worker 独占命令池并预分配可复用 primary，零 worker 使用本槽的内联录制资源。稳定帧 SHALL 不创建或释放命令及同步对象。整个槽内集合 SHALL 由该槽同一次提交的完成状态保护，只有旧提交和相关 CPU 任务全部完成后才能复位和重录；同一命令池不得被多个线程同时使用。重建 SHALL 在所有 CPU 使用者和旧 GPU 工作完成后解除全部槽内旧命令引用，再清理旧附件，并继续复用命令及同步对象集合。

#### Scenario: Revisit a frame slot
- **WHEN** 多帧后重新使用同一帧槽
- **THEN** 等待覆盖该槽全部组的提交和相关 CPU 任务完成后复位其所有 worker 命令池，复用已分配 primary；其他未完成槽不被复位，不以组上次被分配的 worker 推断资源归属

#### Scenario: Rebuild with several primaries per slot
- **WHEN** 多 worker 双清屏执行多次交换链重建
- **THEN** 所有 CPU 使用者结束、旧 GPU 工作完成且全部槽的全部 worker 池旧引用解除后才销毁旧附件；新帧准备引用完整新代，池、primary 和同步对象集合的身份与数量稳定

#### Scenario: Skip without command or fence mutation
- **WHEN** fence 或 acquire 超时、acquire NOT_READY/OUT_OF_DATE，或窗口暂停、零 extent、关闭
- **THEN** 不开始本帧组录制或提交、不重置其提交 fence、不推进帧槽与计数，并保留现有恢复规则；有效 SUBOPTIMAL 获取仍走完整录制和统一提交路径

#### Scenario: Use distinct pools concurrently
- **WHEN** 同一帧槽的多个 worker 同时录制不同组，或同一 worker 随后处理更多组
- **THEN** 并发 worker 的池互不相同，后续组使用本 worker 的不同 primary，不覆盖本帧先前组；池的复位、分配和销毁不与录制重叠

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

#### Scenario: Compare serial and parallel rendering
- **WHEN** 零、单、多 worker 在两种构建配置下执行相同的单清屏及双清屏输入
- **THEN** 归一化后的实际组顺序、barrier、附件和提交语义一致，画面采样一致；线程、池和 command 身份等调度差异不被误判为渲染差异

## ADDED Requirements

### Requirement: Complete the recording batch before deciding frame submission

后端 SHALL 在全部已接受录制任务终结并使结果可见后，检查每组完成状态及完整有序 primary 集合，再决定是否提交。任务结束次序 SHALL 不决定提交顺序；任何准备、调度、录制或结果校验失败 SHALL 阻止本帧所有组提交、呈现及提交状态推进。失败 SHALL 先收敛已接受任务再清理借用对象，报告具体阶段及可确定的失败组；不因首个异常提前释放资源或重置提交 fence。

#### Scenario: Finish in reverse order and submit in plan order
- **WHEN** 控制第二组先完成而第一组后完成，全部录制成功
- **THEN** 实际提交数组仍为第一组、第二组，一次提交等待获取并发出呈现信号；组边界同步及最终转换不变，只有提交成功后推进状态

#### Scenario: Wait for a sibling after recording fails
- **WHEN** 一组抛出标准或非标准异常，而另一组仍在录制
- **THEN** 等待另一组终结才报告整帧失败和清理；本帧无队列提交、present 请求、fence reset 或槽/计数推进，旧提交的清理规则仍成立

#### Scenario: Reject incomplete recording results
- **WHEN** 汇总后存在缺组、重复组、未完成 primary、空句柄或不匹配的本帧身份
- **THEN** 在 reset fence 前明确拒绝整个帧，不能提交有效子集或把未完成组算作成功

#### Scenario: Reject scheduling before acceptance
- **WHEN** 任务构造或批次接受失败
- **THEN** 无部分批次录制或部分提交，沿现有失败退出路径清理；已获取信号不被当作已消费或用于下一正常帧

#### Scenario: Fail submission after full recording
- **WHEN** 全批成功且 reset fence 后的队列提交失败
- **THEN** 不发布成功帧或推进状态，不等待该次未提交工作的 fence，结束正常循环并安全清理此前已提交工作及录制线程

### Requirement: Verify actual recording overlap with bounded evidence

工程 SHALL 在真实组录制路径验证至少两个不同 worker 的录制区间重叠，并分别保留任务执行、实际 Vulkan 输入和 GPU 结果证据。诊断 SHALL 关联帧槽、代、计划、组、worker、线程、命令池及 primary，区分任务完成与成功提交；测试记录 SHALL 有界，正常稳定帧不增加逐帧全量日志。仅有多个 worker 配置、任务提交或最终颜色 SHALL 不构成并行录制证明。

#### Scenario: Overlap inside actual recording
- **WHEN** 至少两个 worker 对两个完整组执行测试专用的有超时录制门控
- **THEN** 两个不同线程与 worker 均在真实 primary begin 成功后、end 之前同时在途，记录各自独占的池与 primary；释放后各组完成并按计划统一提交，超时能释放门控并明确失败

#### Scenario: Check evidence without relying on log order
- **WHEN** 任务乱序完成且有界记录由协调线程按组序输出
- **THEN** 独立的在途计数/门控及线程身份仍能证明重叠，排序后的文本不冒充实际时间顺序；损坏的重复/缺失身份、错序提交或伪重叠证据被检查器拒绝

#### Scenario: Report coverage and performance limits
- **WHEN** 汇总验收结果
- **THEN** 分别报告真实 CPU 录制重叠、真实 GPU 像素/同步、故障控制和合成策略覆盖；门控延长区间只证明并发能力，不宣称 Vulkan 调用同时执行、GPU 并行或净性能提升
