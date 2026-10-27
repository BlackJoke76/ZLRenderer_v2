# Design

## Context

动机和范围见 proposal.md。已核对当前实现：

- prepareGraphFrame 在 acquire 后输出值及借用句柄快照；PreparedGraphFrame.groups 按计划组序保存，不持有交换链对象所有权。
- VulkanPresenter::recordRenderGroup 已独立接收 PreparedRenderGroup 和 primary，录制完整 before barriers、render pass、after barriers；recordGraphFrame 当前复位单池并串行调用它。
- FrameResources 当前每槽一个池，预分配每组一个 primary；submitGraphFrame 从该数组生成 VkSubmitInfo，reset fence 后单次提交。renderFrame 在提交成功后才推进状态。
- TaskSystem 提供稳定 workerIndex、零 worker 内联、整批完成与按任务索引异常汇总，等待线程不帮助执行；任务异常不取消其他任务。当前 renderer 尚未链接它。
- rebuildSwapchain 先等待已提交工作，再复位每槽池以解除旧附件引用；releaseResources 先销毁池再清理附件。新增 CPU 使用者必须先结束。
- tests/checkRenderGraph.cmake 当前假定组序文本与 primary 配对；verifyPresentation.cmake、verifyWindow.ps1 共同检查复用与像素。组 begin/body/end 故障注入已存在，但尚无并发门控或 worker 证据。
- 主规格落后于已完成的 prepare-render-group-recording delta。本 change 修改其中 Record complete physical groups into independent primaries、Reuse group primaries with frame slot lifetime 两块，并保留其余既有要求；验证要求以主规格为底并保留前置 change 的多 primary 修订。后续同步顺序固定为前置 change → 本 change，不能反向覆盖。

## Goals / Non-Goals

**Goals:** 保持一条录制逻辑和一条提交路径，把并发所有权限制为帧槽内的 worker 池与独立结果槽；用完成屏障把 CPU 生命周期与 GPU fence 生命周期接合。

**Non-Goals:** 不让 worker 接触窗口、acquire、queue、present、帧推进或重建；不允许跨帧 CPU 录制批次在途，不引入全局任务服务或 GPU 依赖调度；不修改 TaskSystem 的通用契约。

## Decisions

### 1. 显式配置，执行器由 presenter 生命周期持有

在 RhiConfig 增加 recordingWorkerCount，通过 initialize 传至 presenter。默认零，保持当前单清屏入口无后台线程；并行通过同一生产配置显式启用，测试覆盖 0、1、4。固定计划有 G 个组，请求 W>0 时启用 min(W,G) 个固定 worker，报告请求和实际数量；W=0 使用 TaskSystem 的内联路径。无硬件线程数推导、环境变量产品开关或运行时改数。

Presenter 以 RAII 持有执行器，在初始化命令资源前建立；部分 worker 或后续 Vulkan 资源创建失败均走相同收敛路径。正常 renderFrame 一次最多接受一个批次并等待完成；执行器及帧槽资源跨帧复用，不逐帧创建线程。CMake 只让 renderer 目标链接现有 taskSystem，库本身仍不依赖 Vulkan/GLFW。

替代方案：全局共享服务增加无需求的跨模块生命周期；每帧 std::async/建线程增加开销且无法稳定映射池；单独写串行循环易导致两条录制实现漂移。默认并行对当前单组清屏无实际收益，因此保留零 worker 默认。

### 2. 每槽每 worker 一个池，结果句柄按组收集

将 FrameResources 的池/primary 数组封装为 WorkerCommandResources 集合。资源 lane 数为 max(1,实际 worker 数)，零 worker 的 lane 0 专供协调线程。每 lane 在初始化时创建一个 pool，并预分配 G 个 PRIMARY：任务的 workerIndex 选择 lane，groupIndex 选择该 lane 的 primary。同一 worker 的任务依次执行，不会并发访问同一个 pool；不同 worker 池互不相同。

该直接映射让任意组都可由任意 worker 执行，无需给 TaskSystem 增加亲和性或工作窃取接口。空间为每槽 G×max(1,min(W,G)) 个 primary，是固定计划下为简单所有权付出的成本；当前清屏规模很小，不声称适用于大规模组数。分配前校验计数转换和乘积溢出。后续若真实规模要求更紧预算，单独评估 worker 局部容量策略，不在本轮加入热路径分配。

每个组只将实际使用的 primary 写入 RecordedRenderGroup 结果；提交数组来自结果而非某个 lane 的完整数组。稳定性以 (slot,workerIndex,groupIndex) 对应的池和 primary 身份核验，组跨帧可能换 worker，因此不要求同一组跨帧总用同一句柄。所有命令对象在初始化后稳定，交换链重建不重新分配。

替代方案：共享池加锁会串行化 pool 操作且隐藏所有权；固定 group→worker 会改变现有动态调度契约；每任务创建池或分配 primary 违反稳定资源复用。

### 3. 共享 recordRenderGroup，批次结果隔离

协调线程完成全部准备、结果数组与任务构造后，在本槽旧 fence 完成且本帧准备成功的前提下复位本槽所有 lane 的池，再提交批次。PreparedGraphFrame 和结果数组在批次完成前地址与容量固定；回调只读各组准备结果，只写自己 groupIndex 对应的非 vector<bool> 结果元素，不 push_back、不写共享日志或 presenter 状态。

每个回调从稳定 workerIndex 选择池和 primary，调用现有 recordRenderGroup；该函数继续承担全部真实 Vulkan 录制。结果保存本帧关联字段、groupIndex、workerIndex、池/primary 身份、完成标记和局部 trace；仅在 vkEndCommandBuffer 成功后标记完整。异常直接交给 TaskSystem 捕获标准及非标准类型，TaskBatchResult 的 taskExceptions 通过 taskIndex=groupIndex 定位。组/阶段诊断保留原有 begin/body/end 上下文，不复制第二套错误汇总。

协调线程立即持有句柄并 wait；从批次已接受到完成之间不做可能抛出并提前展开借用对象的操作，必要的作用域守卫在任何异常出口等待已接受批次。wait 保证结果可见且捕获释放后，检查失败汇总和每组结果，再按组序拼接 trace 及提交数组。所有组本帧身份必须匹配，组索引完整且唯一，primary 非空、独立且录制完成。诊断格式化和结果校验都在 reset fence 前完成。

替代方案：共享字符串/结果追加需锁并混淆顺序；首个异常即退出会留下仍借用帧快照的任务；按完成次序提交会改变图语义。

### 4. 一次完整提交，CPU 重叠不修改 GPU 同步

submitGraphFrame 接收已校验的按组序 primary span，继续构造一个 VkSubmitInfo、一次 vkQueueSubmit、一个槽 fence。所有 worker 均不调用队列接口。acquire wait stage、imageAvailableSemaphore、按 imageIndex 的 renderFinishedSemaphore 和 present 规则保持原来源。

第二组可以先在 CPU 录完，但实际数组仍为组 0、组 1；第二组开头的 WAW barrier 保留，唯一最终转换仍归最后一组。CPU 任务之间不等待图的 GPU RAW/WAR/WAW 依赖。只有全批成功、结果完整后 reset fence；只有 submit 成功后发布成功日志及推进计数/槽。即使一组已录完，其他组失败也不提交任何子集。

准备、reset pool、批次接受、组录制或汇总校验失败均停止正常循环，不复用已获取但未消费的信号；submit 失败沿现有未提交 fence 清理规则处理。测试允许多组分别抛标准和非标准异常，汇总后按组定位，不把非标准异常变为进程 terminate。

替代方案：逐组提交增加开销并破坏半帧拒绝契约；任务完成顺序或 command 数组顺序不能替代资源 barrier。

### 5. 回收先等 CPU，再等 GPU，再解除引用

正常每帧 wait 后已无在途 CPU 任务；重建仍显式依赖该不变量。清理顺序为：排空/关闭并 join 执行器 → 等待此前成功提交 GPU 工作 → 复位/销毁所有槽全部 lane 池 → 销毁旧 framebuffer/view/render pass 和同步对象。重建保留工作线程及池对象，只在等待 GPU 后复位所有池，再走既有 oldSwapchain 候选代发布规则。

池复位覆盖未被最近一帧使用的 lane/primary，不能仅按本帧结果列表复位。初始化第 N 个池/primary 失败要释放此前所有已拥有对象；命令资源存储在拥有者内立即登记句柄。关闭/最小化处理仍在接受批次之前，不扩展为异步窗口控制。不可恢复的录制失败不尝试继续下一帧。

替代方案：只依赖 fence 无法保护尚未提交的 CPU 录制；只等待 TaskSystem 也无法保护 pending GPU primary。基础 WSI 呈现引擎完成限制继续记录，不新增维护扩展。

### 6. 验收区分受控重叠、真实调用和结果

在 ZL_RENDERER_TESTING 的真实 recordRenderGroup 内、vkBeginCommandBuffer 成功后且 vkEndCommandBuffer 前放置有超时门控。记录线程 ID、workerIndex、slot/group、池/primary、进入/退出事件及最大同时在途数。至少两个不同 worker/线程同时停留在各自真实 primary 的录制区间；门控到达和退出都用 RAII 及超时释放，失败不挂住 wait/join。该证据证明受控区间重叠，不证明两个 Vulkan API 同时执行或未经控制自然加速。

另外强制后组先完成，验证真实 submit 数组始终按计划排列。局部 trace 在协调线程按组序输出供现有检查器消费；线程时间/门控事件独立保存，不能从排序文本推导重叠。正常日志仍每代每计划首次成功提交采集，测试限定前若干帧；失败日志不伪装成功记录。已有 isVulkanTestCase 可能抛异常，worker 使用的测试配置需由协调线程预先解析成不可变控制值，门控状态本身同步，禁止并发复用旧的可变测试标记。

测试矩阵：0/1/4 请求 worker；单组、双组、组数大于 worker 的测试计划；至少 12 帧覆盖两个槽反复复用；跨代缩放/最小化恢复及关闭。参数比较仅归一化线程/池/primary/时间等身份，仍严格比较组语义、clear、load/store、barrier、image 绑定关联、获取与呈现同步和提交次序。GPU 运行两配置下串/单/多模式的单清屏与双清屏 25 点匹配；第一组漏录不能被第二次清屏最终颜色掩盖，须同时检查实际 begin/end、render pass 和 barrier 输入。

故障控制覆盖 startup/池初始化中途失败、接受前失败、前组已结束后组 begin/body/end 失败、一组失败另一组仍在门控、非标准异常、多组失败、缺失/重复/空/未完成结果和 submit 失败。验证无半帧 submit/present、reset fence 的位置、无未提交 fence 等待、全部 CPU 使用者退出在附件释放前。检查器以损坏的记录反例证明能拒绝错序、伪重叠、重复/缺失身份。参数/结果校验测试尽量走生产校验函数，不为注入错误在产品增加分支。

## Risks / Trade-offs

- [多池与预分配 primary 成本随组数增长] → 数量受固定计划和配置约束、展示实际对象数；不增加逐帧分配，不承诺清屏性能提升。
- [任务迁移使旧按组句柄复用检查失效] → 按槽/worker/组核验资源集合，提交只检查本帧真实结果。
- [并行输出与故障钩子发生竞态] → 组局部输出、协调线程汇总、不可变控制及同步门控；ASan/压力测试不冒充 TSan。
- [旧 pool 仍引用旧附件] → 重建覆盖全部槽全部 lane，按 CPU/GPU/引用解除顺序验证。
- [门控超时导致 join 卡住] → 有界等待和任何异常出口释放门控；CTest 总超时作最后保护。
- [主规格和前置 delta 不一致] → 本轮保留前置材料；后续同步先前置再本 change，并比对完整要求及所有场景。

## Migration Plan

1. apply 前重新核对前置实现/规格；依次接入配置/所有权、worker 池、统一任务录制与完整提交，再升级证据与故障检查，不改图编译器语义。
2. Debug/Release 完整回归、CPU-only 构建隔离、真实窗口与同步 validation 验收；按环境运行 ASan/静态分析及可用的 TSan/UBSan，明确未运行工具。分别记录受控 CPU 重叠、GPU、合成与失败控制覆盖。
3. 通过验收后更新 STATUS/ARCHITECTURE/说明、完成任务核对，确认 remote/branch/upstream 后依项目授权独立提交推送。同步/归档等待相应新请求，遵守前置顺序。
4. 回退本 change 时一并恢复录制协调、命令所有权和对应测试，回到 prepare-render-group-recording 的串行基线；保留通用 TaskSystem 与其他变更。

停止条件：发现前置实现/规格发生实质漂移、需要 secondary/新队列/新绘制功能、稳定计划下无法避免命令资源热路径分配或生命周期不清时，停止实施并修订规划；不得通过削弱实际重叠、结果一致或无半帧提交验收来完成任务。
