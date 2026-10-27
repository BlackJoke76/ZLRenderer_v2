# 路线图

1. `bootstrap-project`：Windows CMake/C++20/GLFW、窗口生命周期与工程文档，已验收并归档。
2. `initialize-vulkan`：instance、validation、surface、物理/逻辑设备和 queues，已验收并归档。
3. `present-clear-frame`：具体最小 RHI、交换链、两个帧槽的持续清屏、图像级呈现同步、缩放和最小化恢复，已验收、独立复核、同步主规格并归档，Git 交付状态见 STATUS。支持 MAILBOX 时优先选择，否则 FIFO；本机实际为 FIFO 回退，MAILBOX 策略与分族参数有合成覆盖。
4. `handle-swapchain-resize`：oldSwapchain 整代替换、等待后尺寸/关闭复查、零 extent 延迟、重复最小化恢复及可重复逐代验收，2026-10-14 实施验收通过；Git 交付状态见 STATUS。2026-10-17 已同步主规格并归档，19/19任务完成。
5. `add-minimal-render-graph`：单队列有序图声明、自动同步、内容校验、一对一物理执行组、显式 barrier 和真实参数诊断已接入清屏；2026-10-18 Debug/Release/ASan 各68项测试、10个窗口流程和静态分析通过，同日独立复核通过。双清屏在真实GPU验证WAW；采样关系只按CPU/参数覆盖，自动合并未实现。2026-10-19 已同步 render-graph 主规格，2026-10-22 已归档，20/20任务完成，交付见 STATUS。
6. `prepare-render-group-recording`：2026-10-22已验收交付，17/17任务；帧准备、完整物理组独立primary录制与按序一次提交已拆分，目前仍串行录制，未同步主规格或归档，证据见STATUS。
7. `add-recording-task-system`：2026-10-26纯CPU任务执行器实施与验收；零worker调用线程执行，固定worker批次调度、完成等待、异常汇总及排空关闭独立验证。CPU-only构建不查找Vulkan/GLFW。2026-10-27已同步cpu-task-execution主规格并归档，15/15任务完成；最终交付与限制见STATUS，尚未接入渲染调用链。
8. 后续待规划：将完整物理组录制接入任务执行器，另行处理每worker/帧槽命令池、录制结果收集和Vulkan生命周期；CPU任务完成不替代GPU同步。
9. 后续待规划：基础绘制的 shader/pipeline/顶点输入等，以实际功能扩展组内执行内容，用独立 OpenSpec change 明确范围与验收。
10. 后续待规划：按目标环境评估 maintenance1/present fence 等扩展，为交换链重建和退出增加严格的呈现完成保证；当前基础 WSI WaitIdle 清理限制见 ARCHITECTURE。

后续绘制、自动合并、多队列和多线程GPU录制接入尚未实现；按实际资源使用扩展现有图，不预建通用资源注册表、调度或别名体系。实际验证、兼容限制及提交/归档状态见 STATUS。
