# Tasks

## 1. 配置与执行器所有权

- [x] 1.1 为 RhiConfig、Rhi 到 presenter 的初始化增加 recordingWorkerCount，默认零、正数按组数限制实际 worker 数；链接现有 taskSystem 并建立 RAII 生命周期。通过 0/1/4 请求配置、单组实际数量及默认应用构建验证，不改 TaskSystem 通用语义。
- [x] 1.2 为执行器启动/初始化后续失败补充测试专用控制，检查已启动线程全部回收；运行既有 CPU-only 测试配置确认不查找 Vulkan/GLFW，正常库/产品不含控制标识，并在 README 记录配置及实际 worker 数规则。

## 2. 帧槽与 worker 命令资源

- [x] 2.1 将帧槽命令资源改为每 worker 独占池和按组预分配 primary，零 worker 使用内联 lane；校验数量转换/溢出并复用公共创建释放步骤。资源测试核对池数、primary 数、非空与独立身份以及中途池/primary 创建失败的完整释放。
- [x] 2.2 将正常槽复位、重建和 shutdown 接入全部 worker 池，落实 CPU 排空、GPU 完成、解除引用、附件释放顺序；用多槽多帧/重建和初始化失败日志检查全部池及线程回收，更新 ARCHITECTURE 中所有权表。

## 3. 共享组录制与完整有序提交

- [x] 3.1 将 recordGraphFrame 改为每组一个 TaskSystem 任务，预先固定快照、结果和任务存储，0/1/多 worker 都调用现有 recordRenderGroup；每组只写独立结果及局部 trace。用单/双组及五组测试计划验证每组恰好一次、worker 索引范围和同 worker 后续组不覆盖先前 primary。
- [x] 3.2 等待全批并汇总标准/非标准异常，校验组完整性、唯一 primary、完成状态及本帧关联，再按计划构造提交数组；submitGraphFrame 消费该数组且保持单次提交。生产校验函数测试拒绝缺组、重复组、空/重复句柄、未完成及错误帧身份；现有串行组/同步回归继续通过。
- [x] 3.3 为已接受批次的所有异常出口增加必要完成守卫，成功提交前不推进状态；通过一组失败且同批另一组延迟结束、多个异常、接受前失败和 submit 失败验证全批收敛、零半帧提交/呈现、reset fence 时机及未提交 fence 不被等待，并更新 ARCHITECTURE 的失败路径说明。

## 4. 并发证据与检查器

- [x] 4.1 在真实 recordRenderGroup 的 begin 成功与 end 之间加入测试专用超时门控和 RAII 在途计数，测试控制由协调线程预解析；记录 slot/generation/version/group/worker/thread/pool/primary。实际双组多 worker 测试证明不同线程和池的最大录制在途数至少二，门控超时/异常也能释放并终止。
- [x] 4.2 增加后组先完成的控制，协调线程有界汇总组局部 trace 与独立时序事件，升级 checkRenderGraph.cmake、verifyPresentation.cmake；检查真实提交输入仍按组序及最终转换唯一，以错序、缺失/重复身份、伪重叠等损坏记录验证检查器确实拒绝。
- [x] 4.3 增加跨模式语义比较和对象复用检查，按 (slot,worker,group) 核对固定资源集合，归一化仅排除句柄/线程/时间差异；通过 0/1/4 请求 worker 至少 12 帧、五组计划及多次重建，验证无稳定帧对象增减、池过早复位或旧绑定使用，并在验证说明写明受控重叠的证明边界。

## 5. 真实画面与故障集成

- [x] 5.1 扩展 verifyWindow.ps1 的测试模式以组合 worker 配置、单/双清屏和窗口流程；运行 Debug/Release 的 0/1/多 worker 单/双清屏，25 点采样一致，实际参数保留第一组完整调用证据，应用 validation/sync 无 WARNING/ERROR；保存日志与截图，不把最终颜色单独作为完整录制证明。
- [x] 5.2 运行多 worker 持续至少 10 秒、缩放、至少三轮最小化恢复、可见/最小化关闭；结合 SUBOPTIMAL、超时/NOT_READY/OUT_OF_DATE、重建失败回归，验证暂停冻结、恢复推进、全部池复用、正常关闭五秒内退出及旧附件释放顺序，保留真实 MAILBOX/FIFO/队列族与合成覆盖区别。
- [x] 5.3 在先前已有成功帧后执行后组 begin/body/end、并发兄弟失败、非标准/多组异常与 submit 失败控制；检查本帧无半帧提交、无错误状态推进、线程终结先于资源释放且非零退出，确认所有测试注入仅存在于测试目标，记录控制覆盖不等于驱动自然故障。

## 6. 整体验收与交付

- [x] 6.1 在新构建目录运行 Debug/Release 全部 CTest 及 CPU-only 隔离回归，运行可用 ASan/静态分析和可用 TSan/UBSan；汇总实际命令、测试数和日志，明确未运行工具及 ASan 不证明无数据竞争，不重复把 CPU 证据算作 GPU 验收。
- [x] 6.2 对照全部 delta 要求/场景及设计决策完成 verification.md、更新 STATUS 和最终架构/使用说明，记录前置 change 状态和规格同步顺序；执行 openspec validate record-render-groups-in-parallel --strict 与 git diff --check，仅将有证据的任务标为完成。
- [x] 6.3 验收后核对 remote、branch、upstream 及相关文件范围，按项目授权以包含 record-render-groups-in-parallel 的独立提交交付并推送；核对远端 SHA，记录提交链接、分支、验证及推送结果，保留其他工作区改动；失败如实记载，不强推或切换分支。

## Workflow follow-up

- 本轮 propose 只生成规划材料，等待新的 apply 请求后开始实施。
- 用户后续请求同步或归档时，先处理 prepare-render-group-recording 的主规格增量，再处理本 change；核对完整要求和场景，避免旧串行契约覆盖新并行契约。本轮不代为同步或归档前置 change。
