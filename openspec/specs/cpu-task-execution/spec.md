# cpu-task-execution Specification

## Purpose

为独立 CPU 工作提供按批次接受、执行、完成等待和失败汇总的明确契约，保证不同 worker 数量下的任务归属与关闭生命周期可验证，为后续录制调度提供基础而不绑定图形 API、窗口或 GPU 完成语义。

## Requirements

### Requirement: Accept independent owned task batches

执行器 SHALL 接受有限个独立任务组成的批次并返回完成句柄；每个已接受任务 SHALL 恰好执行一次。空批次 SHALL 立即完成。任务在批次间及批次内不隐含依赖，系统 SHALL 允许多个批次在途并分别等待。空回调或关闭后的提交 SHALL 在任何该批次任务开始前被拒绝；提交准备失败 SHALL 不留下部分可执行批次。

#### Scenario: Run empty and nonempty batches
- **WHEN** 对开放执行器先提交空批次，再提交含若干有效回调的批次
- **THEN** 空批次等待立即完成且无任务失败；非空批次每个任务恰好执行一次并归属于该次提交

#### Scenario: Reject a batch before acceptance
- **WHEN** 提交含空回调的批次，或批次准备/入队在接受前失败
- **THEN** 提交报告错误，整个批次没有任何回调执行，不返回一个永远不能完成的已接受批次

#### Scenario: Keep batch completion independent
- **WHEN** 多个批次已经接受，等待其中一个批次
- **THEN** 等待条件只对应该批次全部任务完成，其结果不与另一批次混合，也不要求其他独立批次先全部完成

### Requirement: Execute with explicit zero single and multiple workers

执行器 SHALL 使用显式配置的非负 worker 数量。零 worker SHALL 不创建工作线程，在提交调用线程按批次内输入顺序执行全部任务后返回完成句柄；正数 SHALL 使用固定工作线程执行任务，协调线程等待时不帮助执行。每条 worker SHALL 有执行器内稳定且不共享的索引，范围为零至数量减一；零 worker 回调使用索引零。多 worker 不保证任务完成顺序或特定任务的 worker 分配。

#### Scenario: Execute inline with zero workers
- **WHEN** 配置零 worker 并提交多个任务
- **THEN** 所有回调在提交线程按输入顺序执行，索引均为零，无后台线程，完成句柄在提交返回时已经完成

#### Scenario: Execute on a single worker
- **WHEN** 配置一个 worker 并提交多个批次
- **THEN** 任务只在同一工作线程执行，索引始终为零，该线程区别于提交与等待线程，执行器能重复接受和完成工作

#### Scenario: Execute independent tasks concurrently
- **WHEN** 配置至少两个 worker 并提供可同时运行的独立任务
- **THEN** 至少两个不同工作线程可以同时执行回调，各自索引稳定且互不相同，不要求它们按固定顺序结束

### Requirement: Wait for full completion and retain results

批次等待 SHALL 在本批全部回调终结、执行器释放其任务回调持有的捕获且结果写入对等待者可见后返回；重复等待 SHALL 返回相同的完成结果。非回调线程 SHALL 可以同时等待同一批次。丢弃所有外部句柄 SHALL 不取消已接受工作；执行器关闭或析构完成后，仍保留的有效句柄 SHALL 继续可等待和读取结果。调用者 SHALL 保证回调借用的外部对象在其使用期间存活。

#### Scenario: Observe completed task outputs
- **WHEN** 回调各自写入不同输出位置，协调线程等待对应批次
- **THEN** 等待返回时全部输出已经可见，回调对象持有的捕获已释放，再次等待不会重跑任务或改变结果

#### Scenario: Wait repeatedly and from several external threads
- **WHEN** 多个非回调线程持有有效句柄并等待同一批次，完成后再次等待
- **THEN** 所有等待者都在全批完成后返回并观察相同结果，不丢唤醒

#### Scenario: Drop handles and keep accepted work
- **WHEN** 提交后立即丢弃该批次全部外部句柄，随后关闭执行器
- **THEN** 所有已接受任务仍执行完毕，不访问已销毁的批次状态，关闭返回时没有遗留任务

#### Scenario: Retain a handle beyond executor lifetime
- **WHEN** 批次句柄比执行器活得更久
- **THEN** 执行器析构先排空工作，随后该句柄的等待与结果读取仍然有效，不访问已销毁执行器

#### Scenario: Reject an invalid completion handle
- **WHEN** 等待默认无效或已移出状态的句柄
- **THEN** 明确报告无效句柄，不无限等待或解引用无效状态

### Requirement: Aggregate callback failures without abandoning the batch

执行器 SHALL 捕获每个任务回调抛出的异常，包括非标准异常，并按任务在批次内的原始索引保留可重新抛出的异常信息及失败总数。任一任务失败 SHALL 不取消本批其他任务、不终止 worker，也不污染其他批次结果。等待 SHALL 先完成全批再提供汇总，不在首个失败时提前返回；零、单、多 worker 的失败语义 SHALL 一致。

#### Scenario: Collect several task failures
- **WHEN** 同批多个任务分别成功、抛标准异常或抛非标准异常，结束次序与输入顺序不同
- **THEN** 所有任务都执行一次，等待在全部终结后返回，失败数量及原任务索引准确，异常类型与信息可检查，成功任务不被标成失败

#### Scenario: Continue accepting work after task failure
- **WHEN** 某批次发生任务失败，之后提交有效新批次
- **THEN** worker 仍可完成新任务，新批次失败汇总独立，旧批次重复等待的结果保持不变

### Requirement: Drain accepted work and join workers during shutdown

显式关闭 SHALL 先停止接收新批次，再完成全部已接受工作并回收全部工作线程后返回；排空期间任务失败 SHALL 仍被汇总且不妨碍关闭。关闭 SHALL 可重复调用，析构 SHALL 执行同一排空生命周期且不传播任务异常。部分工作线程创建失败 SHALL 回收已创建线程后报告初始化失败。关闭能够完成的前提是调用者任务最终返回或抛异常，本轮不提供强制中断或取消。

#### Scenario: Close an idle executor repeatedly
- **WHEN** 关闭从未提交任务或已经空闲的执行器，然后重复关闭并析构
- **THEN** 零、单、多 worker 配置均完成清理，没有重复 join 或遗留线程；后续提交即使为空批次也被拒绝

#### Scenario: Close with queued running and failing tasks
- **WHEN** 已接受任务中存在正在运行、等待执行以及会抛异常的回调，协调线程发起关闭
- **THEN** 关闭等到这些任务全部终结并 join 全部 worker，结果仍可从保留句柄读取，不抛出任务异常或丢弃排队任务

#### Scenario: Destroy without explicit close
- **WHEN** 已接受工作后直接离开执行器作用域
- **THEN** 析构排空已接受工作并回收线程，保留句柄仍反映准确完成与失败结果

#### Scenario: Fail during worker startup
- **WHEN** 尚无任务提交而创建后续 worker 失败
- **THEN** 已启动 worker 被唤醒并回收，初始化报告失败，不遗留线程或因等待未发布任务而卡住

### Requirement: Keep control operations external and validate without graphics

批次提交与关闭 SHALL 由同一协调线程串行调用；调用者 SHALL 不从任务回调重入同一执行器的提交、等待、关闭或销毁操作。模块及其独立测试 SHALL 不引用或链接 Vulkan、GLFW 或 RenderGraph，并可在不查找图形 SDK、不下载图形依赖、不创建窗口或设备的配置下运行。测试 SHALL 区分真实线程并发与控制注入，不以 CPU 完成证明 GPU 同步或渲染正确。

#### Scenario: Configure and test without graphics dependencies
- **WHEN** 在新构建目录关闭 renderer 构建选项，使用 C++ 工具链配置并运行任务执行器测试
- **THEN** 不调用 Vulkan 查找、GLFW 下载或图形目标生成，零、单、多 worker 的批次、异常与关闭测试均可构建和运行

#### Scenario: Preserve the existing renderer path
- **WHEN** 使用默认 renderer 构建选项配置工程
- **THEN** 现有图形依赖、目标和测试继续可用，执行器不会自动替换现有串行组录制或改变提交与呈现规则
