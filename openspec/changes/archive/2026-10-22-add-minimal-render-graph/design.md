# Design

## Context

动机与交付范围见 `proposal.md`。本次修订落实架构讨论：以清屏验证一个小型单队列图编译器；资源使用分析、执行分组和同步具有明确边界，不将图永久限定为单次清屏。

实施前的源码基线（本变更的出发点；实施结果见 verification.md）：

- `Rhi::renderFrame()` 转发到 Presenter；RhiConfig 提供线性清屏色，公开接口没有 Vulkan 句柄。
- `createRenderPass()` 固定 CLEAR/STORE，initialLayout=UNDEFINED、subpass=COLOR_ATTACHMENT_OPTIMAL、finalLayout=PRESENT_SRC_KHR；入向 EXTERNAL 依赖与 submit acquire wait stage 都为 COLOR_ATTACHMENT_OUTPUT。
- `recordClearFrame()` 使用已等待 fence 的帧槽，begin/end render pass 覆盖 extent，没有 shader、pipeline 或 draw。
- Presenter 独占两个固定帧槽和各代 SwapchainResources。实际图像借用，framebuffer/view/present semaphore 按图像归属；submit 成功才推进计数和 frameIndex。
- 重建使用 oldSwapchain，候选附件完整后发布；旧链退役后失败不能回用。分族使用 CONCURRENT sharing。50 项既有 CTest、测试 hooks、呈现核查与窗口脚本可复用，既有硬件结果只作为历史基线。

本轮仍使用 Vulkan 1.0、传统 render pass 与一次 graphics submit；显式边界转换替换当前 render pass 的隐式初始/最终布局转换。这是本次修订选择的同步落地方式。

## Goals / Non-Goals

**Goals:**

- 从多个逻辑 pass 的资源用途推导单队列依赖、内容有效性和同步，而不是维护固定清屏同步摘要。
- 图核心不依赖 Vulkan/GLFW；声明、不可变编译计划和后端执行职责清楚，诊断读取同一计划与实际 API 参数。
- 清屏是附件 load 行为；逻辑 pass、物理执行组、command buffer 与 submit 没有永久一对一关系。
- 生产单清屏；测试以同图像双清屏真实验证 WAW 和执行分组，CPU/参数测试覆盖更多使用关系。
- 稳定帧复用计划、命令与后端对象，不增加整设备等待。

**Non-Goals:**

- 本次不实现 pass 自动合并、自动重排、通用 DAG 调度、裁剪、别名、资源版本系统、临时 GPU 图像或跨帧通用状态表。
- 只分析整张单 mip/单 layer/单采样颜色图像，单 graphics queue；每个 raster pass 当前一个颜色附件，可声明 fragment 采样输入。不扩展 MRT/深度/MSAA、反馈采样、多队列或子资源。
- 不实现实际采样绘制、shader/三角形/后处理，不新增空 Renderer/RenderScene 类、虚拟多后端框架或通用命令回调系统。
- 不迁移 WSI 所有权，不扩展当前呈现引擎完成保证。

## Decisions

### 1. 图声明用途，清屏属于附件

图核心预计放在 `src/renderGraph.h/.cpp`。核心数据仅需：

| 数据 | 内容与职责 |
|---|---|
| 外部图像声明 | 图内 typed handle、名称、外部角色、入口用途/内容有效性；区分可呈现目标与普通外部颜色图像 |
| 逻辑 raster pass | 名称、单颜色附件、load/store 意图及 clear value、fragment 采样输入 |
| 资源用途 | `colorAttachmentWrite`、`colorAttachmentReadWrite`、`fragmentSampledRead`；`present` 是外部终端用途 |
| 编译计划 | 声明顺序、资源依赖及原因、物理组、边界同步与出口；没有真实图像指针 |

建议用 `GraphImageHandle` 区分逻辑图像与实际 RHI/Vulkan 资源，变量按角色命名为 `colorTargetHandle`、`sampledImageHandle` 等。默认无效、越界和非本图 handle 必须被拒绝，不需要全局资源注册表。

附件 load 当前支持 Clear 与 Load，store 支持 Store 与 Discard。Clear 表达完整覆盖本轮整图像范围；Load 需要已有有效内容，按颜色附件读写处理。声明 Discard store 后内容不能再被采样、Load 或导出显示。没有 load DONT_CARE 加任意绘制的覆盖推断：本轮不存在能证明完整写入的 draw 内容。

一个资源在同一 pass 内既被采样又作为颜色附件写入属于不支持的反馈，明确拒绝。编译时合并同 pass 的一致重复用途、拒绝冲突用途，不产生依赖自身的边。不同逻辑图像不能暗中绑定同一实际图像；别名本轮不支持，绑定阶段检查并拒绝。

清屏值复用现有 ClearColor 和有效颜色规则，生产配置仍在原初始化边界检查；如为测试图复用检查而提取公共函数，保持既有行为和唯一默认值。

选择原因：附件与用途是后续绘制仍然需要的信息。替代的 `addClearPass` 特殊操作枚举会让每种新绘制工作都要求修改图编译器。

### 2. 按声明顺序分析使用，维护完整访问关系

编译阶段为普通函数与数据转换，不为每阶段引入接口类：

```text
Pass/resource declarations
            |
            v
Validate and analyze ordered uses
            |
            v
Build physical groups (one raster pass per group)
            |
            v
Lower dependencies into boundary synchronization
            |
            v
Immutable compiled plan
```

当前没有排序或调度算法，执行顺序就是声明顺序。多个资源、多个写入是有效输入，不再沿用“恰好一个目标和一个 clear”的拒绝规则。可呈现计划必须包含工作和唯一有效呈现出口；出口放在全部逻辑使用之后。未引用资源可以保留，不为此增加裁剪算法。

每个资源在本次编译内维护最近写入者、此次写入后的读取者集合、当前用途/布局需求及累计读取范围、内容有效性。处理规则：

1. 读取：验证内容有效，关联最近写入者的 RAW 依赖，并加入读取者集合。
2. 写入：关联最近写入者的 WAW 以及全部相关读取者的 WAR；之后更新写入者并清空旧读取集合。读写用途先检查原内容及已有依赖，再登记本次写入，避免自身依赖。
3. 用途要求布局改变：转换覆盖相关先前访问，不能只看最近一次访问。多个读取者必须全部纳入后续覆盖/转换的源范围。
4. Clear+Store 建立有效完整内容；Load 保留有效性；Discard store 使内容失效。入口有效性由外部声明提供，编译器不能猜测。
5. 同用途、同布局的连续读取不凭空产生读读 hazard。逻辑生产者关系仍保留；同步降低阶段可以复用已满足同一读取范围的可见性，不重复制造只因连续读取而出现的 barrier。

依赖记录保留生产者、消费者、资源和 RAW/WAR/WAW/布局变化等原因。一个边界可合并多个原因的源范围；先追求正确和可解释，不追求全局最少 barrier。WAR 本身是执行依赖；若还需图像布局转换，必须同时满足转换的内存要求。布局相同不代表 WAW 可以省略；丢弃内容也不解除已在执行的读写依赖。

选择原因：只记录 lastUse 会漏掉多个读者；直接引入任意 DAG 调度又超出当前实际需要。按序分析保留后续调度可使用的依赖信息，但本轮不消费它来重排。

### 3. 物理执行组是数据边界，本轮一对一

`PhysicalPassGroup`（建议名称）包含有序逻辑 pass 引用、附件范围与 load/store、组前/组后同步；同步记录使用逻辑资源 handle，执行时才解析实际图像。当前每个 raster pass 独占一组，呈现出口不成为组，执行器遍历组后执行组内工作。

组不拥有 command buffer、submit 或 semaphore。现有帧槽提供一个 primary command buffer，帧内所有组录制进去，只在帧尾 submit 一次。阶段函数和简单数据足够，不建立可插拔分组策略或插件系统。

本轮不尝试合并。未来合并相邻组时，必须验证附件/采样等兼容性、内容语义及能否在同一渲染范围表达依赖，重新降低组内部与外部同步；不能直接删掉已有 barrier。后续 pass 的 Clear 不得丢失，需要明确组内清除实现或拒绝合并。先保留逻辑依赖再生成组同步，避免把旧边界写死。

参考 Granite 的 [PhysicalPass 与分组/同步构建](https://github.com/Themaister/Granite/blob/master/renderer/render_graph.cpp#L2750)；只采用职责区分，不搬入其调度、资源别名或 subpass 合并实现。

### 4. 显式边界 barrier 统一负责转换

后端的小型生产映射函数将图用途转成 Vulkan 1.0 参数；CPU 图不包含 Vk 类型：

| 用途 | layout | stage | access |
|---|---|---|---|
| colorAttachmentWrite | COLOR_ATTACHMENT_OPTIMAL | COLOR_ATTACHMENT_OUTPUT | COLOR_ATTACHMENT_WRITE |
| colorAttachmentReadWrite | COLOR_ATTACHMENT_OPTIMAL | COLOR_ATTACHMENT_OUTPUT | COLOR_ATTACHMENT_READ 与 WRITE |
| fragmentSampledRead | SHADER_READ_ONLY_OPTIMAL | FRAGMENT_SHADER | SHADER_READ |
| present 出口 | PRESENT_SRC_KHR | 由外部呈现协议处理；不是普通 shader read | 不伪造呈现引擎的 GPU 读取 access |

范围只包含 COLOR aspect、mip 0 和 layer 0，各计数 1。按计划在渲染范围之外调用 `vkCmdPipelineBarrier`，兼容同步点可以批量提交 image barriers。依赖包含源使用和目标使用，后端根据 hazard 区分内存与执行要求；无关资源不使用 ALL_COMMANDS/GENERAL 默认兜底。

传统 render pass 的 initialLayout、subpass layout、finalLayout 均为 COLOR_ATTACHMENT_OPTIMAL；load/store 来自该组。初建与重建共用创建函数，核对并移除/调整原本为自动初始转换设置的显式 EXTERNAL 依赖。图拥有的资源转换不再由 render pass 初始/最终布局重复承担；这不声称 Vulkan 不存在规范定义的隐式依赖。

交换链清屏链的具体落地：

| 边界 | 实际处理 |
|---|---|
| 外部 acquire → 首次清屏 | 丢弃旧内容时 UNDEFINED→COLOR_ATTACHMENT_OPTIMAL；barrier src/dst stage 均 COLOR_ATTACHMENT_OUTPUT，srcAccess=0、dstAccess=COLOR_ATTACHMENT_WRITE；submit 在相同阶段等待 acquire semaphore |
| clear A → clear B | COLOR_ATTACHMENT_OPTIMAL→同布局；src/dst stage 均 COLOR_ATTACHMENT_OUTPUT，src/dst access 均 COLOR_ATTACHMENT_WRITE，处理实际 WAW；不能把第二次 Clear 再当成无前序访问的 UNDEFINED 起点 |
| 最后写入 → present | COLOR_ATTACHMENT_OPTIMAL→PRESENT_SRC_KHR；src stage/access 为颜色输出/写入，legacy dst stage=BOTTOM_OF_PIPE、dstAccess=0；原 vkQueueSubmit signal 图像级 present semaphore，present 等待它 |

图内 discard 意图与外部同步分开。初次或再次 acquire 都等待真实获取信号；不维护“所有帧一律尚未使用”的伪历史。单 graphics queue 内不做 ownership transfer；graphics/present 分族继续沿用 CONCURRENT sharing，barrier queue family index 为 IGNORED。

采样用途的计划与生产 barrier 参数映射本轮完成并测试，但不实现采样 draw/descriptor/pipeline。实际执行要求所需物理绑定和支持的命令内容齐备，缺失时提交前失败，不静默跳过。不能用正确生成一个 SHADER_READ barrier 宣称采样绘制已实测。

依据：[Khronos 同步示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html) 及 [legacy API 示例](https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples-(Legacy-synchronization-APIs))。替代的 render pass 隐式转换可减少显式命令，但对本轮多组计划会增加“转换究竟在哪里”的阅读成本；本次统一显式落地，未来如吸收为 subpass 依赖也必须保持每个转换只有一个执行位置。

### 5. 图与真实资源各有所有者

现有 VulkanBackend 先持有图声明和不可变编译计划；逻辑拓扑或配置改变才重新编译。Presenter 消费计划并继续负责 acquire、帧槽、submit/present、窗口暂停、重建及清理。计划不作为 SwapchainResources 的成员；后者只拥有本代 swapchain、render pass、framebuffer/view 和图像级同步等后端对象。

逻辑用途映射可按计划版本复用；依赖实际格式的 render pass/framebuffer 随代建立。稳定帧只建立局部绑定，包含当前代、imageIndex、frameIndex、实际图像/附件与 extent；不逐帧重建 GPU 对象、图字符串或完整编译计划。

成功 acquire 后才能绑定交换链目标。绑定核查当前已发布代、合法索引、兼容描述及真实图像唯一性；编译计划里没有真实附件指针，因此新代发布后可以复用同一逻辑计划并绑定新对象，不遗留旧 framebuffer。普通外部资源若未来提供真实入口，其状态、有效内容和外部完成条件也必须由调用方明确提供；本轮运行路径没有新增此类 GPU 资源。

选择原因：图描述帧内使用，交换链代描述实际对象的可用性和生命周期。图成为交换链的附属物会妨碍未来离屏资源；提前增加空 Renderer 层同样没有帮助。

### 6. 清屏执行内容最小，失败仍受既有规则约束

当前 pass 的实际工作是附件 Clear/Load/Store，不含 draw payload。执行器按组选择已绑定附件、组的 clear value 和 extent，begin/end render pass；这足以执行生产单清屏和测试双清屏。未来 draw 内容可接入组内录制，不要求业务自行 begin/end、做转换或 submit；本轮不预造完整命令接口。

所有 barrier、组录制和出口都必须消费编译计划，不保留绕过图的正常清屏函数。颜色和操作顺序不得由另一套执行器硬编码。

- 暂停/关闭、fence timeout、acquire TIMEOUT/NOT_READY/OUT_OF_DATE 在绑定前返回；没有图执行成功记录。
- SUBOPTIMAL 的有效获取仍消费图像和 acquire 信号，完成本次提交/呈现后再重建。
- 编译/映射可提前校验；获取后绑定/录制失败终止正常循环，不复用尚未消费的 acquire。
- fence 只在真正 submit 前 reset。所有组录制后一次 submit；成功后才推进 frameIndex、提交计数和成功日志，失败不等待未提交 fence。
- 没有跨帧状态表需要提前提交：本次编译状态只描述图内假设。未来若增加持久资源状态，必须另行定义 submit 成功后的状态提交规则。
- oldSwapchain 退役、候选失败清理、帧槽复用及基础 WSI WaitIdle 限制保持既有规则。

### 7. 诊断来自计划和实际调用输入

默认 stdout 在每个已执行代和计划版本首次 submit 成功后输出一次完整内容：

- 逻辑 pass 顺序、每组包含哪些 pass、资源名和用途/load/store；
- 生产者/消费者及 RAW/WAR/WAW/转换原因、前后状态；
- 实际 stage/access/layout、barrier 所绑定图像、generation/imageIndex/frameIndex；
- 独立标识计划与 submitted 状态，代表帧 present 返回后记录真实请求结果，不称 presentation completed。

稳定帧不完整 dump。测试目标沿用有限逐帧记录，采集实际传给 barrier/render pass/submit/present 的参数，核对同一计划和实际绑定；日志格式化不能另存一份固定操作表。编译/录制成功不等于已提交，失败不能输出该帧成功记录。

### 8. 三层验证，清屏画面验证真实多 pass

| 层次 | 覆盖与证据 | 不能据此宣称 |
|---|---|---|
| 独立 CPU CTest | 多资源/多 pass、RAW/WAR/WAW、多个读者、读读兼容、布局变化、内容有效性、引用/反馈错误、确定顺序及一对一分组；不链接 Vulkan/GLFW | GPU 已正确执行所有用途 |
| 生产后端映射检查 | 实际使用的 barrier 参数、组附件/布局、acquire wait stage；与逻辑依赖关联 | 采样 shader、分族硬件等已实测 |
| 真窗口/GPU | 生产单清屏、测试同图像双清屏、跨帧与跨代、失败与资源复用 | 任意多 pass、采样绘制或 pass 合并已验收 |

双清屏测试只在已有独立测试目标中选择不同声明，复用生产编译器/映射/执行器，绝不直接手写两次清屏与 barrier 来代替图：

```text
Acquire
  -> pre-barrier
  -> Group 0: clear image to A, store
  -> WAW barrier, keep color attachment layout
  -> Group 1: clear same image to B, store
  -> present transition
  -> one submit
  -> present
```

两组使用相同实际图像、render pass/framebuffer 和同一帧槽 primary buffer；不创建 shader 或额外图像。Debug/Release 各核对实际两次组录制、WAW barrier、唯一最终转换、每帧一次 submit 和最终 25 点 B 色。正确像素与无 validation 诊断不能独立证明依赖正确，必须同时核对 CPU 依赖断言和实际 API 输入。

其余验收沿用既有脚本：两配置默认色持续至少 10 秒、多次缩放、至少三次最小化恢复、可见/最小化关闭；两配置替代色与双清屏分别验证。核对暂停计数冻结、恢复推进、当前代绑定、5 秒内正常退出 0、资源平衡及应用 validation/sync 无 WARNING/ERROR。可重复窗口流程复用公共步骤，不复制整套脚本。

全部既有 50 项 CTest 加新测试、Debug/Release 无警告构建、可用 ASan 和静态分析均需记录。MAILBOX/FIFO 纯策略与分族参数回归保留；记录本轮硬件，明确哪些是合成/状态控制，未提供的 UBSan 等不虚报。

## Risks / Trade-offs

- [通用名词堆出过大框架] → 保持上述数据和普通阶段函数；只有生产真实复用需要才提取文件，不为每阶段建类。
- [只剩清屏包装，自动同步没有实际作用] → 支持多次使用，CPU 检查全读取者关系，真 GPU 双清屏核对 WAW 与执行组。
- [自动同步遗漏未声明访问] → 后端只接受有效声明/绑定及支持的内容；新增绘制时必须把所有实际资源访问纳入声明，本轮不声称能反射任意 shader。
- [同一转换由两处负责] → 本轮显式组边界转换，render pass 布局保持一致；同时检查旧 EXTERNAL 依赖和实际参数。
- [后续合并破坏 Clear/Load/Store 或依赖] → 一对一分组真实参与执行，保留逻辑依赖；未来合并需重新校验并降低同步，本轮无合并算法。
- [跨代附件失效或隐藏别名] → 局部绑定验证当前代与真实图像身份，编译计划不持有附件引用。
- [CPU/映射验证被写成全部 GPU 验收] → 报告逐条注明覆盖层次，真实采样绘制和复杂依赖留待后续实际功能验证。
- [基础 WSI 的呈现完成缺口] → 保留现有 WaitIdle 限制，不以图计划、提交 fence 或 validation 通过补足保证。

## Migration Plan

收到新的 apply 请求后按 tasks 推进：声明与 CPU 使用分析、分组与同步降低、Vulkan 映射、真实帧接入、诊断及双清屏，再完成整体回归。每组同时补齐自己的测试和职责文档；本次 update 不编辑产品代码或已完成状态。

验收后更新 README/ARCHITECTURE/ROADMAP/STATUS 与 verification，按 AGENTS 核对任务、范围、origin/分支/upstream，再独立提交推送；归档等待用户新请求。需要回退时逆向撤销本 change 的实现提交，恢复原清屏同步，不改写历史。

**暂停条件：** 实施若要求超出已定义的颜色整图像用途、实际采样绘制、临时 GPU 资源、自动合并/重排、多队列/线程、提高 API 基线或改变 WSI 所有权，先记录具体证据并修订规划，不临时扩展实现。
