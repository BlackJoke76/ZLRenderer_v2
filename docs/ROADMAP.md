# 路线图

1. `bootstrap-project`：Windows CMake/C++20/GLFW、窗口生命周期与工程文档，已验收并归档。
2. `initialize-vulkan`：instance、validation、surface、物理/逻辑设备和 queues，已验收并归档。
3. `present-clear-frame`：具体最小 RHI、交换链、两个帧槽的持续清屏、图像级呈现同步、缩放和最小化恢复，已验收、独立复核、同步主规格并归档，Git 交付状态见 STATUS。支持 MAILBOX 时优先选择，否则 FIFO；本机实际为 FIFO 回退，MAILBOX 策略与分族参数有合成覆盖。
4. 后续待规划：基础绘制的 shader/pipeline/顶点输入等，用独立 OpenSpec change 明确范围与验收。
5. 后续待规划：按目标环境评估 maintenance1/present fence 等扩展，为交换链重建和退出增加严格的呈现完成保证；当前基础 WSI WaitIdle 清理限制见 ARCHITECTURE。

后续阶段尚未实现；不因为路线图而提前引入通用资源体系、渲染图或多线程架构。实际验证、兼容限制及提交/归档状态见 STATUS。
