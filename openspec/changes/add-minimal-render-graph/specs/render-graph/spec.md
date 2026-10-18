# Render Graph Spec Delta

## Purpose

为有序渲染工作提供明确的资源使用声明和可核验的编译结果，根据同一资源的前后读写自动规划依赖与状态转换，并将逻辑 pass、物理执行组和后端执行分开，使清屏接入可以验证多 pass 基础且保持现有呈现生命周期正确。

## ADDED Requirements

### Requirement: Declare borrowed external color images

渲染图 SHALL 支持声明多个外部颜色图像，以图内有效引用标识资源、外部所有权、入口用途、内容有效性和是否可呈现。逻辑声明 SHALL 不保存真实交换链附件引用；实际交换链图像只在成功获取后绑定，图不创建或销毁借用图像。无效、非本图引用和不支持的物理别名 SHALL 在使用对应计划提交前拒绝。

#### Scenario: Declare multiple external resources
- **WHEN** 为有序 pass 声明多个不同的外部颜色图像
- **THEN** 保留各自身份、入口条件与所有权，不因资源数大于一而拒绝声明，也不替它们分配 GPU 图像

#### Scenario: Reject an invalid resource reference
- **WHEN** 使用默认无效、未导入或属于其他图的资源引用
- **THEN** 明确报告相关引用错误，不能使用该声明提交工作

#### Scenario: Bind the acquired image
- **WHEN** 成功获取当前交换链代图像，且图像索引与帧槽索引不同
- **THEN** 逻辑目标绑定到该已获取图像和当前代附件，帧槽及图像同步对象按各自归属选择，图不独立销毁借用图像

#### Scenario: Reject hidden physical aliasing
- **WHEN** 两个不同逻辑资源在同一执行计划中被绑定到同一实际图像
- **THEN** 以本轮不支持物理别名为由拒绝绑定，不把它们当作无依赖的独立图像提交

### Requirement: Describe ordered raster work through resource uses

渲染图 SHALL 支持多个有序逻辑 raster pass，每个 pass 当前包含一个颜色附件及可声明的 fragment 采样输入。颜色附件 SHALL 声明清屏或保留内容的 load 意图、清屏值及 store 意图；清屏属于附件行为。用途模型 SHALL 区分颜色附件写入、颜色附件读写及 fragment 采样读取；呈现 SHALL 作为可呈现资源的终端用途，不额外形成 GPU 绘制工作。

#### Scenario: Clear the same target twice
- **WHEN** 两个连续逻辑 pass 对同一颜色目标分别声明完整清屏
- **THEN** 保留两次清屏及顺序，不因多次写入而拒绝、不覆盖前一个 pass 的声明，呈现出口位于最后一次使用之后

#### Scenario: Preserve defined attachment contents
- **WHEN** pass 声明保留颜色附件原内容
- **THEN** 声明表达对原内容的读取及附件写入用途，不能被编译器默认为再次清屏或丢弃内容

#### Scenario: Reject unsupported use combinations
- **WHEN** 声明同一 pass 对同一图像同时采样和颜色附件写入，或要求本轮范围外的 MRT、深度、MSAA、子资源拆分或多队列
- **THEN** 明确拒绝不支持的组合，不以宽泛默认状态掩盖限制

### Requirement: Compile deterministic ordered plans with valid contents

渲染图 SHALL 按声明顺序编译，不自动重排、裁剪或合并 pass；相同声明 SHALL 得到相同顺序、资源关联与同步需求。编译 SHALL 校验内容可用性，拒绝读取无有效来源的内容。用于呈现的计划 SHALL 具有有效的可呈现资源出口，不接受空工作、缺失出口或冲突出口。

#### Scenario: Compile a supported ordered graph
- **WHEN** 编译相同的有效多 pass 声明
- **THEN** 得到相同执行顺序和资源使用关系，所有有效声明的 pass 均保留

#### Scenario: Reject a read of undefined contents
- **WHEN** 采样或保留附件内容时，该资源没有有效入口内容，也没有之前建立内容的写入，或其内容已按声明丢弃
- **THEN** 在提交前明确拒绝读取，并定位资源和消费操作

#### Scenario: Reject an invalid presentation exit
- **WHEN** 可呈现计划没有工作、缺少出口、重复声明同一出口，或将不可呈现图像作为呈现出口
- **THEN** 明确失败，不生成可提交的部分呈现计划

### Requirement: Infer single queue synchronization from declared uses

在单 graphics queue、整张单 mip/单 layer/单采样颜色图像范围内，渲染图 SHALL 根据用途自动推导 RAW、WAR、WAW 及布局变化所需的同步，而不要求业务 pass 填写原生阶段、访问掩码和布局。分析 SHALL 覆盖最近写入者及其后所有未结束读取者，区分执行依赖与内存可见性；丢弃内容 SHALL 不解除此前使用的同步义务。

#### Scenario: Make a color write available to sampling
- **WHEN** 前一 pass 写入颜色附件，后一 pass 声明 fragment 采样读取该图像
- **THEN** 同步计划关联该生产者和消费者，包含颜色写入到 fragment 读取的可见性及所需布局变化

#### Scenario: Wait for reads before overwriting
- **WHEN** 某资源先被读取，后被另一 pass 覆盖
- **THEN** 后续写入等待先前读取结束，单纯 WAR 不被机械地当作 RAW 的可见性操作，布局变化所需同步仍被保留

#### Scenario: Synchronize consecutive writes
- **WHEN** 两个 pass 连续写入同一颜色目标，即使后一次完整清屏丢弃原内容
- **THEN** 生成写入间的有效依赖，不能因为布局相同或不保留原内容而省略同步

#### Scenario: Cover every outstanding reader
- **WHEN** 一次写入后出现多个读取 pass，之后再覆盖该资源
- **THEN** 覆盖操作的依赖覆盖所有相关读取者及读取阶段，不仅依赖最近一个读取 pass

#### Scenario: Avoid a dependency caused only by compatible reads
- **WHEN** 连续读取的用途和布局兼容，且没有未处理的生产者可见性或转换要求
- **THEN** 不仅因存在两个读取者就生成额外读读同步，后续写入仍能找到全部相关读取者

### Requirement: Separate logical passes from physical execution groups

编译结果 SHALL 分别表达逻辑 pass 和物理执行组。本轮 SHALL 将每个 raster pass 放入独立执行组，保留逻辑依赖并据分组生成组边界同步；资源出口不增加渲染组。执行 SHALL 消费分组结果，逻辑 pass 声明不得固定自身 command buffer 或 submit 数量。本轮不承诺自动合并算法。

#### Scenario: Preserve one to one grouping
- **WHEN** 编译两个有效逻辑 raster pass 和一个呈现出口
- **THEN** 得到两个按顺序执行的物理组，每个包含对应逻辑 pass，出口不成为第三次渲染或额外提交

#### Scenario: Execute through group boundaries
- **WHEN** 后端执行编译结果
- **THEN** 根据执行组确定渲染范围与边界同步，组内逻辑操作和诊断可互相关联，不绕过分组另走固定清屏路径

### Requirement: Apply graph synchronization at explicit backend boundaries

后端 SHALL 将图的同步需求统一落实为物理执行组边界的显式图像同步与布局转换；渲染范围内的附件布局 SHALL 与边界计划一致，不重复执行同一次转换。获取等待 SHALL 覆盖目标的首次使用，呈现请求 SHALL 等待最后写入及最终转换。执行与诊断 SHALL 消费同一计划、映射和实际绑定。

#### Scenario: Acquire render and present one image
- **WHEN** 成功获取图像并执行单次清屏计划
- **THEN** 等待获取后才进入颜色附件使用，完整清屏后进行可呈现状态转换，再以正确图像的同步对象关联呈现请求

#### Scenario: Keep the image writable between clear groups
- **WHEN** 对同一实际图像执行两个清屏组
- **THEN** 组间处理颜色写入依赖且保持颜色附件布局，第二组结束后才转换为可呈现状态；不插入中途呈现转换或重复隐式转换

#### Scenario: Reject an unusable backend binding
- **WHEN** 实际绑定缺失、与声明不兼容或所需执行内容未被后端支持
- **THEN** 在提交前明确报告原因并停止该执行，不静默忽略资源用途或将未执行工作报告为成功

### Requirement: Preserve frame and swapchain lifecycle during graph execution

逻辑图与计划 SHALL 独立于交换链实际对象的所有权；本帧交换链绑定仅引用成功获取的已发布当前代，不跨帧保存旧附件引用。图接入 SHALL 保持现有跳帧、暂停、SUBOPTIMAL 消费、提交后推进及错误清理规则；稳定帧不新增命令/同步对象创建销毁或整设备空闲等待。

#### Scenario: Replace the swapchain generation
- **WHEN** 缩放或过期导致完整新代发布
- **THEN** 后续绑定和后端对象匹配新代格式、extent 与图像集合，逻辑计划不持有旧附件且帧槽继续复用

#### Scenario: Skip a frame or pause presentation
- **WHEN** 等待或获取超时、获取 NOT_READY/OUT_OF_DATE，或者窗口关闭、最小化或 extent 为零
- **THEN** 不执行该帧图工作、不输出成功提交、不推进该帧提交计数，保留既有恢复与关闭处理

#### Scenario: Consume a suboptimal acquisition
- **WHEN** SUBOPTIMAL 获取返回有效图像与同步信号
- **THEN** 使用该图像执行计划并消费获取同步，随后按既有规则重建

#### Scenario: Fail before successful submission
- **WHEN** 编译、映射、绑定、录制或提交失败
- **THEN** 报告实际阶段，停止正常推进，以非零状态清理；不推进未提交帧的帧槽/计数，不等待未成功提交工作的 fence

### Requirement: Expose dependencies groups synchronization and binding

正常诊断 SHALL 输出逻辑顺序、物理组、资源名与用途、依赖原因、状态变化、后端同步映射及所关联提交的代号/图像索引/帧槽。输出 SHALL 区分计划、成功提交和呈现请求结果，不声称呈现引擎已完成。完整计划在每个已执行代及计划版本首次成功提交时输出一次，稳定帧不重复完整输出；测试逐帧记录保持有限。

#### Scenario: Inspect a submitted clear plan
- **WHEN** 某代及计划版本首次成功提交
- **THEN** 可从诊断还原资源、逻辑 pass、执行组、外部获取到清屏到呈现的依赖和实际绑定，原生参数与实际调用输入一致

#### Scenario: Inspect the dependency between two clears
- **WHEN** 测试目标执行同图像双清屏
- **THEN** 诊断关联两个逻辑 pass、两个物理组、真实组间写后写同步和唯一最终呈现转换，不能用固定摘要替代调用证据

#### Scenario: Keep diagnostics bounded and truthful
- **WHEN** 同一代及计划稳定执行，或编译/录制/提交失败
- **THEN** 稳定帧不重复完整计划，失败帧不冒充成功提交；新代或新计划首次成功提交重新输出，代表帧的呈现结果来自实际请求

### Requirement: Validate compiler and clear execution at distinct coverage levels

工程 SHALL 分别验证 CPU 依赖推导、生产后端参数映射和真实 GPU 执行。真实 GPU 交付范围为单清屏与测试目标双清屏；采样读取等其他用途只按本轮实际 CPU/映射证据报告，不宣称采样绘制已提供或实测。Debug/Release SHALL 保持既有呈现回归、MAILBOX/FIFO 策略与同步诊断契约，并保留基础 WSI 限制。

#### Scenario: Verify compiler cases without a GPU
- **WHEN** 执行独立 CPU 测试
- **THEN** 不依赖窗口或 Vulkan 设备即可验证 RAW/WAR/WAW、所有读取者、布局变化、内容有效性、确定顺序、引用错误及一对一分组

#### Scenario: Verify two clear passes on the GPU
- **WHEN** Debug 和 Release 的测试目标在真实获取图像上依次完整清除为颜色 A、B
- **THEN** 同一 command buffer 内执行两个渲染组并只提交一次，最终 25 点采样匹配 B；原始记录证明实际 barrier 和绑定与计划一致，资源平衡且无应用 validation/sync WARNING/ERROR

#### Scenario: Preserve the presentation regression baseline
- **WHEN** Debug 和 Release 验收默认/替代色、持续至少 10 秒、多次缩放、至少三次最小化恢复及可见/最小化关闭
- **THEN** 保持完整清屏、当前代绑定、暂停计数冻结、恢复推进和正常条件下 5 秒内退出 0，保存原始证据，准确列明真实硬件、合成输入、状态控制和未实测条件
