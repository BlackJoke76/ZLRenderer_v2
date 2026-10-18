# Tasks

本文件记录实施与验证进度；仅已验证任务勾选完成。范围、数据职责与暂停条件见 design.md，行为契约见 specs/render-graph/spec.md，逐项证据见 verification.md。每组同时交付自己的测试和文档；CPU/映射支持与真实 GPU 覆盖分别记录。

## 1. 图资源与有序 raster 声明

- [x] 1.1 新增纯 CPU 图声明：多个借用外部颜色图像、图内 typed handle、有序 raster pass 的单颜色附件和 fragment 采样用途、Clear/Load 与 Store/Discard、呈现出口；核对无 Vk/GLFW 类型、真实附件指针或 GPU 资源所有权，不再限制恰好一个目标/一次清屏。
- [x] 1.2 复用 configureCppTarget 注册不链接 Vulkan/GLFW 的 CPU CTest，覆盖有效多资源/多 pass、无效/非本图引用、冲突用途、同 pass 反馈采样、重复或不可呈现出口及范围外输入；运行用例验证合法多次写入被接受、非法形状被明确拒绝。
- [x] 1.3 更新 ARCHITECTURE 的声明职责和当前支持范围，说明清屏属于附件 load、呈现属于外部出口；对照实际接口核验命名与所有权，不宣称后端已经接入。

## 2. 使用分析与自动依赖

- [x] 2.1 实现按声明顺序的使用分析，维护最近写入者、全部相关读取者、用途/读取范围和内容有效性，生成带资源及生产者/消费者的 RAW/WAR/WAW 与布局变化关系；以针对性断言验证无自依赖、无自动重排，Clear/Load/Discard 的有效性更新正确。
- [x] 2.2 扩展 CPU 测试覆盖写到采样 RAW、读到写 WAR、同布局连续清屏 WAW、多个读取者后覆盖、兼容读读、布局变化及读未定义/已丢弃内容；验证遗漏任一读取者或错误允许无效读取会使测试失败，结果在相同输入下确定。
- [x] 2.3 更新 ARCHITECTURE 的编译主路径、外部入口契约与同步原因示例；核对文档和测试输出一致，明确按序分析与自动调度、执行依赖与内存可见性的区别。

## 3. 执行分组与 Vulkan 同步映射

- [x] 3.1 添加物理执行组和同步降低，每个 raster pass 当前一组，出口不成组，保留逻辑依赖并生成组前/组后转换；CPU 测试验证分组包含正确 pass、组间依赖完整、没有中途呈现转换，不把逻辑 pass 固定为单独 command buffer/submit。
- [x] 3.2 实现生产使用的 Vulkan 1.0 同步映射及显式 image barrier 生成，覆盖颜色写/读写、fragment 读取与呈现出口；测试实际映射结构的 stage/access/layout、COLOR 整图范围和 IGNORED queue family，验证 WAR 与布局转换正确组合、兼容读取无凭空新增读读同步，不使用 ALL_COMMANDS/GENERAL 默认兜底。
- [x] 3.3 调整共用 render pass 创建，使 initial/subpass/final layout 都保持颜色附件布局、load/store 来自组，核对原 EXTERNAL 依赖并避免转换重复；参数测试验证 acquire 前后阶段链、同布局 WAW 及唯一最终 present barrier，采样映射只按 CPU/参数覆盖报告。
- [x] 3.4 更新 ARCHITECTURE 的逻辑 pass/执行组/synchronization/submit 边界和原生映射表，核对代码确实消费上述结构；标明本轮一对一分组、无合并算法，未来合并必须保留内容语义并重新降低同步。

## 4. 接入真实帧与交换链生命周期

- [x] 4.1 由现有 VulkanBackend 持有图及不可变计划，Presenter 消费计划；交换链代仅持有实际后端对象，成功 acquire 后建立局部绑定并验证当前代、索引、描述和实际图像唯一性；通过绑定测试及逐代日志验证缺失/过期/隐藏别名被拒绝，计划无旧附件引用。
- [x] 4.2 将现有清屏录制切换为遍历执行组，按计划发出组前后 barrier、begin/end 和清屏值，在同一帧槽 primary buffer 录制全部组并只 submit 一次；运行生产单清屏集成与资源身份检查，验证无绕过图的正常路径、无逐帧对象创建或新增 WaitIdle。
- [x] 4.3 复用 hooks 和 verifyPresentation.cmake 核对跳帧、暂停、SUBOPTIMAL、重建及提交失败，增加获取后绑定/组录制失败、缺失执行能力和非法绑定检查；验证失败不推进计数/帧槽或等待未提交 fence，候选失败与 oldSwapchain 退役规则不变，同步更新 ARCHITECTURE 实际帧流程。

## 5. 可观察执行与真实 GPU 双清屏

- [x] 5.1 从编译计划、生产映射和真实 API 参数输出逻辑顺序、执行组、资源用途、依赖原因、转换及 generation/imageIndex/frameIndex；测试验证每已执行代/计划版本仅首次成功提交完整输出，失败无成功记录，代表帧呈现结果真实且不称呈现引擎完成。
- [x] 5.2 在现有独立测试目标中选择“同图像 clear A → clear B → present”声明，复用生产编译/映射/执行器，不直接手写第二条清屏路径；Debug/Release 真实运行并保存两组录制、WAW barrier、唯一最终转换、一次 submit、对象复用和 25 点最终 B 色证据，确认 validation/sync 无应用 WARNING/ERROR。
- [x] 5.3 复用窗口脚本公共流程支持双清屏颜色及图日志核对，README 加入可实际运行的诊断和测试示例；核验文档字段来自真实输出、脚本命令运行有效，并注明双清屏不证明采样绘制或 pass 合并已通过 GPU 验收。

## 6. 整体验收与交接

- [x] 6.1 在独立干净目录配置并无警告构建 Debug/Release，运行全部 CTest（既有 50 项及新增项）；保存原始输出，核对 CPU 编译、分组、生产映射、绑定、双清屏、原失败路径和 MAILBOX/FIFO 策略回归通过，分族共享/映射准确标明覆盖方式。
- [x] 6.2 Debug/Release 各验证生产默认色持续至少 10 秒、25 点完整覆盖、多次缩放、至少三次最小化恢复及可见/最小化关闭，并各验证既有替代色；核对暂停计数冻结、恢复推进、跨代绑定、正常条件下 5 秒内退出 0、资源平衡及应用 validation/sync 诊断，保存日志和截图，结合 5.2 的双清屏证据完成窗口验收。
- [x] 6.3 对新增编译计划、绑定、映射与执行修改运行可用 ASan 和静态分析；记录检查命令、无内存问题/警告及不支持工具，不将主机检测替代 GPU 同步检查。
- [x] 6.4 在本 change 的 verification.md 将全部要求、场景与设计决策逐项关联 CPU/参数/真实 GPU/状态控制证据，更新 STATUS/ROADMAP 的实际状态和限制；运行 openspec validate add-minimal-render-graph --strict 与 git diff --check，只勾选已验证任务，不冒用旧硬件结果或宣称采样绘制/合并已实测。

## Workflow follow-up

- 验收通过后按 AGENTS 核对本 change 的文件、任务和交接，只暂存相关成果，以包含 add-minimal-render-graph 的独立提交交付；推送前核实 origin、当前分支和 upstream，有歧义则询问，不切分支、合并远端或强推。报告 SHA、分支、验证及实际推送结果和可核实的提交链接。
- 用户后续请求归档时使用归档 skill，同步并验证主规格及归档记录；本次 update 和后续 apply 不自动归档。
