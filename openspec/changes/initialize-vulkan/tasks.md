# Tasks

## 1. Vulkan 工具链与文档

- [x] 1.1 检查当前 Vulkan SDK、loader、validation layer 与设备，记录原始环境诊断到 STATUS；验证头文件、import library、vulkaninfoSDK.exe 可用，缺项报告具体阻塞，不修改系统驱动或 layer 设置。
- [x] 1.2 在 CMake 接入 Vulkan::Vulkan，README 增加 SDK、驱动、loader 和 validation 前置条件及构建命令；验证新目录配置和 Debug 编译成功，Vulkan 路径不硬编码。

## 2. Instance、诊断与 surface 生命周期

- [x] 2.1 建立不可复制/移动的空 RAII VulkanContext，初始化前核查 GLFW 所需 extensions、debug-utils 和 validation layer，创建 instance 与 debug messenger；通过测试或临时验证版本确认 layer/extension 缺失时非零退出，诊断回调可收到受控消息且异常不跨 C API 边界。
- [x] 2.2 复用现有窗口创建 surface，按 instance → messenger → surface 阶段建立异常回滚；分别在这些对象创建成功后注入失败，检查已创建对象按依赖顺序且仅一次释放，未创建对象不被销毁，日志包含失败阶段和原因。
- [x] 2.3 更新 ARCHITECTURE 的所有权表、回调状态生命周期和初始化/销毁链；逐项核对代码及上述失败记录，明确 physical device/queue 是借用句柄。

## 3. 物理设备、队列与逻辑设备

- [x] 3.1 实现对真实 surface 的 graphics/present 查询和最小选择函数，检查 Vulkan 枚举/查询结果并处理 VK_INCOMPLETE；新增 CTest 覆盖同族优先、分族接受、空族与缺失能力拒绝，确认选择函数由生产路径复用。
- [x] 3.2 按去重后的族创建 logical device 并获取 queue index 0，输出设备名称及族索引；CTest 核查实际创建设备所用的族列表去重，同族与分族两个输入均通过，真实硬件运行验证两种角色得到有效队列。
- [x] 3.3 验证无合适设备、逻辑设备创建失败和设备创建后完成初始化前失败；用测试目标或临时版本注入对应条件，检查非零退出、不报告成功、device 首先清理及随后正确回滚，ARCHITECTURE/STATUS 记录选择规则与实际证据。

## 4. 窗口整合与实际退出

- [x] 4.1 main 在窗口拥有者的生命周期内建立 context，仅全部初始化成功后进入原事件循环，错误返回非零；运行真实窗口验证可见、移动、调整大小、系统关闭，退出码 0，清理日志按 device → surface → messenger → instance → window → GLFW 排列，无应用生命周期 validation WARNING/ERROR。
- [x] 4.2 更新 README 的成功/失败诊断示例、ROADMAP 的初始化与未来 swapchain 边界及 STATUS 的实际状态；对照代码和日志确认 Debug/Release validation 都启用，MAILBOX 尚未实现，SDK/loader 缺失等环境限制如实说明。

## 5. 集成验收

- [x] 5.1 按 README 从新的构建目录配置和编译 Debug/Release，分别运行 CTest 及完整窗口验收；保留初始化到退出的原始日志，区分本机已有 loader/overlay 环境诊断与应用错误，将命令、设备、队列和验证结果更新 STATUS。
- [x] 5.2 对照 vulkan-context 与现有窗口/入门主规格逐项核对，审查代码命名、复用、回滚与生命周期，运行 openspec validate initialize-vulkan --strict 和 git diff --check；确认临时注入不在产品中，且没有 swapchain/GPU 提交/等待的范围扩展。

## 6. Git 交付

- [x] 6.1 验收通过后核实 main、origin/upstream 与目标 BlackJoke76/ZLRenderer_v2，更新交接并只暂存本 change 文件；独立提交且信息包含 initialize-vulkan，以 staged diff 和提交 SHA 验证范围，保留其他未跟踪文件与既有归档。
- [x] 6.2 推送至已核实的 main，比较本地与远程 SHA，报告提交链接、分支、验证与推送结果；失败时保留本地成果并记录真实状态。

## Workflow follow-up

- 用户请求时使用 verify 技能复核；请求归档时按 archive 技能同步 vulkan-context 主规格、归档并提交/推送对应记录。
