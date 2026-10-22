# Tasks

## 1. 帧准备与借用输入

- [x] 1.1 在现有 Vulkan 后端文件中引入最小 `PreparedGraphFrame`/`PreparedRenderGroup`，解析当前代绑定、附件和全部边界映射，复用已有映射/校验函数；用生产准备入口的 CPU 参数测试验证单/双组、不同 slot/image、错误代号、缺失绑定和不支持采样均在录制前拒绝。
- [x] 1.2 明确一 pass 一物理组的当前后端支持边界，准备结果不保存可悬空原生指针、不跨帧持有附件；以单/双组准备测试及代码核查证明全部组输入就绪后才进入录制，并在 ARCHITECTURE 中记录准备结果的借用生命周期。

## 2. 完整物理组独立录制

- [x] 2.1 将两个帧槽改为各自 pool 内预分配固定组数 PRIMARY 数组，验证组数/count 合法、初始化分配失败清理完整；测试诊断证明单组/双组各槽实际数量正确，稳定帧无 allocate/free。
- [x] 2.2 将 pool reset 移至槽等待、acquire 和准备成功后的统一录制入口；实现只消费准备结果的 `recordRenderGroup`，串行执行 begin→before barriers→完整 render pass→after barriers→end；单/双清屏实际调用记录证明每组句柄不同、begin/end 成对、WAW 和最终转换归属正确。
- [x] 2.3 在测试目标加入可与双组计划组合的后组 begin/组内/end 失败检查点（保留已有 record/graph-group 覆盖），同时更新 CTest 注册与阶段断言；验证前组已结束时仍无本帧 submit/present、fence reset、计数和槽推进，错误包含组索引/阶段且非零退出。
- [x] 2.4 更新 ARCHITECTURE 的命令所有权、录制链和稳定复用说明；对照生产调用检查录制函数不读取可变交换链状态、不 acquire/rebuild/submit，并运行受影响参数与录制测试。

## 3. 统一提交与调用证据

- [x] 3.1 所有组 end 成功后构造一个 `VkSubmitInfo` 并一次 `vkQueueSubmit` 提交按组序排列的 primary 数组；保留 wait stage、帧槽 acquire、图像级 signal 和单 fence，提交成功后再推进/发布；实际参数记录验证单/双组数组数量、顺序和同步对象归属。
- [x] 3.2 从实际 begin/barrier/render pass/end/submit 输入采集有界组句柄和数组记录，失败单列阶段；更新 `checkRenderGraph.cmake` 与 `verifyPresentation.cmake`（含窗口共用检查调用），加入错序、重复/缺失句柄、数量错误、缺失最终转换的反例，验证正常记录通过且损坏记录失败，不能只匹配 submits=1。
- [x] 3.3 扩展 submit/reset-fence 失败及成功提交后 present 失败的控制覆盖；验证失败帧无成功诊断、不等待未提交 fence，旧成功提交仍进入正确清理，SUBOPTIMAL 获取真实信号被完整消费，并明确注入证据的 host 控制性质。
- [x] 3.4 更新 ARCHITECTURE 的统一提交、计数提交点和诊断说明；以单/双组 CTest 与已有 presentation 回归核对应用 RHI、默认颜色、load/store 与错误分类保持一致。

## 4. 全集合复用与重建

- [x] 4.1 适配重建、部分初始化失败及 shutdown 的 primary 数组清理；验证先完成真实旧提交、reset 所有槽的 pool、再销毁旧附件，pool 销毁释放全部 primary，宿主数组清空，不独立销毁借用图像。
- [x] 4.2 将双组模式与重建控制组合，升级按槽/组 primary 数组身份及按代/图像 semaphore 检查；在多代、跨槽和候选代失败测试中验证命令/同步数量稳定，无旧 framebuffer 后续使用，oldSwapchain 退役规则不变，更新 ARCHITECTURE 对应生命周期说明。
- [x] 4.3 复核 fence/acquire 超时、NOT_READY/OUT_OF_DATE、暂停/零 extent/关闭分支，按需增加缺失的测试目标控制；验证不录制/提交、不 reset fence、不推进，以及重复恢复后正常继续，运行现有重建失败回归。

## 5. 集成验收与交接

- [x] 5.1 Debug/Release 构建并运行全部 CTest（包含既有图依赖、队列选择、MAILBOX 优先/FIFO 回退、Vulkan 生命周期和本次故障覆盖），严格校验 change 并执行 git diff --check；保存命令、结果与测试总数，失败项不得标完成。
- [x] 5.2 使用现有窗口脚本验收单清屏默认/替代色及双清屏 A/B，启用可用 validation/sync；保存真实提交数组、组内调用、25点最终颜色 B、至少10秒持续运行、多次缩放、三轮最小化恢复及可见/最小化关闭证据，验证暂停计数冻结、恢复推进与正常条件下5秒内退出0。
- [x] 5.3 在可用环境运行 ASan 与静态分析并记录工具限制，完成 requirement/scenario 到 CPU、映射、真实 GPU、合成/故障控制的对应报告；不将模拟 MAILBOX/分族或注入失败报告为真实硬件条件，不声称 WaitIdle 保证呈现引擎完成。
- [ ] 5.4 验收通过后更新 docs/STATUS.md 记录范围、验证结果及限制，核对 ARCHITECTURE 与最终代码一致；按项目规则仅暂存本 change 相关文件，以包含 prepare-render-group-recording 的独立提交交付，核实 remote/branch/upstream 后推送并报告 SHA、目标、验证和远端结果；未完成验收时只记录实际进度。

## Workflow follow-up

- 已按新的 apply 请求实施；验收与交付结果见 verification.md 和 docs/STATUS.md。
- 后续按用户请求执行独立 verify、主规格同步与归档，并核对主规格与归档记录；本任务不自动归档。
