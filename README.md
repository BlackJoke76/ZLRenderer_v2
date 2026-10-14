# ZLRenderer

Windows 上的 C++20 / GLFW / Vulkan 开发工程。通过最小 RHI 在可调整大小的窗口持续显示清屏颜色，使用两个帧槽及交换链，同步获取、录制、提交与呈现，启用 validation 并正常关闭。

## 前置条件

- Windows 10/11 x64，有可交互桌面。
- Visual Studio 2022，安装“使用 C++ 的桌面开发”、MSVC x64 工具链、Windows SDK 和 C++ CMake 工具。
- CMake 3.24 或更新版本、Git 可用。首次配置需要访问 GitHub 下载 GLFW。
- 使用 PowerShell，当前目录为项目根目录。源码路径包含空格或单引号时仍需正常引用。
- 安装 Windows x64 Vulkan SDK（包含头文件、import library 和 `VK_LAYER_KHRONOS_validation`），重新打开终端使 `VULKAN_SDK` 生效。CMake 自动查找 SDK，也可用 `-DVulkan_ROOT="$env:VULKAN_SDK"` 指定安装根目录。
- 安装支持 Vulkan 1.0、`VK_KHR_swapchain` 和当前 surface 颜色附件呈现的显卡驱动及 Vulkan loader。先运行 `& "$env:VULKAN_SDK\Bin\vulkaninfoSDK.exe" --summary`，确认设备和 Khronos validation layer 可见。
- Debug 和 Release 都要求 validation 与 `VK_EXT_debug_utils`；缺项时明确失败，不会自动关闭 validation。SDK 缺失会使配置失败；loader DLL 缺失可能由 Windows 在进入 `main` 前拒绝启动。
- 本工程验收使用 Vulkan SDK 1.4.341.1；构建测试目标需要头文件包含 `VK_EXT_layer_settings` 的类型定义。GPU 核心 API 基线仍为 Vulkan 1.0，layer settings 只用于可用时的同步验收配置。

## 配置、编译与运行

打开 Visual Studio 的 Developer PowerShell，确认 `cmake --version` 和 `git --version` 成功，执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
& .\build\Debug\zlRenderer.exe
```

若普通 PowerShell 中 CMake 未加入 PATH，可以在当前终端使用 VS 自带版本（默认安装路径）：

```powershell
$env:Path = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;" + $env:Path
```

配置使用固定的 GLFW 3.4 提交 `a74efa0d5628b74adc0426af4c5710e287fa7c2c`，源码下载到 `build/_deps`，无需手动安装 GLFW。

运行后应出现标题为 `ZLRenderer`、初始客户区 1280×720 的清屏窗口。默认线性 RGBA 为 `(0.05, 0.10, 0.20, 1.0)`；sRGB surface 上显示为深蓝色（约 RGB 63,89,124）。日志确认 validation、设备/族、`RHI initialization succeeded`、所选模式、格式、尺寸和实际图像数。支持 MAILBOX 时优先 MAILBOX，否则 FIFO；无输入事件也持续呈现。缩放后恢复完整颜色，最小化时暂停，恢复后继续；关闭时输出成功提交/呈现帧摘要与清理记录，退出码为 0。

Release 使用同一个配置目录：

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
& .\build\Release\zlRenderer.exe
```

要验证从头构建，使用新的目录（例如将以上 `build` 替换为 `build-clean`），避免依赖已有构建缓存。初始化、窗口创建或运行失败会向标准错误输出阶段和原因，并返回非零退出码。

CTest 默认开启，目前共 50 项：队列/交换链选择策略，以及实际 Vulkan 上的初始化回滚、诊断、帧槽复用、FIFO、替代颜色、资源回滚、超时、重建与错误控制子进程测试；新增连续 oldSwapchain 参数链、候选各阶段失败、等待后尺寸变化/关闭和受控零 framebuffer/iconified/surface extent。需满足上述 SDK、驱动及桌面条件。故障注入只在 `vulkanLifecycleTests` 目标编译，正常应用没有注入开关。只构建应用可在配置时指定 `-DBUILD_TESTING=OFF`。

清屏颜色在 `src/main.cpp` 中初始化 `RhiConfig rendererConfig` 时指定，例如 `rendererConfig.clearColor = {0.2f, 0.05f, 0.1f, 1.0f};`，重新编译后生效；分量必须有限且位于 [0,1]。颜色编码依据实际所选格式，配置无需修改帧同步代码。

可重复的可见窗口验收（每次至少持续 10 秒；逐次匹配实际客户区与 framebuffer/extent 日志后截图和25点内部/边缘颜色采样、反复/快速缩放收敛、三次最小化恢复、系统关闭与资源计数）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verifyWindow.ps1 -executablePath .\build\Debug\zlRenderer.exe
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verifyWindow.ps1 -executablePath .\build\Release\zlRenderer.exe
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verifyWindow.ps1 -executablePath .\build\Debug\zlRenderer.exe -closeWhileMinimized -logDirectory .\build\Debug\minimized-close-logs
```

脚本保存完整 stdout/stderr、客户区 PNG 和 verification.txt 到对应配置目录的 `window-logs`；CTest 日志在 `build/test-logs/<配置>/`。暂停/恢复快照要求 submitted/presented 计数不变，恢复后的下一阶段必须有帧推进；稳定尺寸期间不重复重建。`-closeWhileMinimized` 在三次恢复后再次最小化并关闭，最终帧摘要必须等于最后暂停快照。脚本只将自己启动的验收窗口置顶以减少截图遮挡，并向该窗口发送标题栏对应的 SC_CLOSE，未模拟鼠标点击。需要可交互桌面，多个窗口验收顺序运行；像素自动检查支持 B8/R8 的 UNORM/SRGB 四种格式，其他格式需人工核验截图。客户区几何和 GDI 截图在同一 per-monitor v2 线程 DPI 上下文执行，恢复/缩放日志匹配后等待桌面动画稳定再采样。

## 诊断与退出

成功日志示例（设备/族索引随机器不同）：

```text
Created instance; validation enabled: VK_LAYER_KHRONOS_validation (API 1.0)
Selected device: AMD Radeon RX 7900 XT; graphicsFamily=0 presentFamily=0
Vulkan initialization succeeded
Swapchain generation=1 mode=FIFO format=50 colorSpace=0 extent=1280x720 images=3 sharing=EXCLUSIVE (MAILBOX unavailable in selection)
RHI initialization succeeded; clearColor=(0.050,0.100,0.200,1.000); frameSlots=2
GPU completion wait result=0
Released command pool
...（按依赖顺序释放所有呈现/帧资源）
Frame summary: submitted=... presented=... generations=...
Destroyed device
Destroyed surface
Destroyed debug messenger
Destroyed instance
Destroyed window
Terminated GLFW
```

失败示例：`Renderer failed: instance prerequisites: missing layer VK_LAYER_KHRONOS_validation`，或 `Renderer failed: logical device: vkCreateDevice failed (VkResult=...)`。初始化失败返回 1 且不进入帧循环；运行失败立即停止正常推进，仅清理已经成功创建的对象。

本机存在旧 AMD ICD JSON 无法打开、EOS Overlay 重复的 loader 环境诊断。应用照常输出，验证保留原始日志，只依据 `tests/knownEnvironmentDiagnostics.txt` 中两条具体消息归因；新的消息仍需检查。应用 validation 消息不能因此忽略。详细环境与验证证据见 STATUS。

## 同步验证与清理范围

Debug/Release 均启用基础 validation。窗口脚本只给其子进程设置 SDK 的 `VK_LAYER_VALIDATE_SYNC=1`；也可在当前终端设置该变量后运行 CTest/产品，再移除该进程变量。测试目标使用可用的 `VK_EXT_layer_settings` 显式启用同步检查，日志包含 `Synchronization validation enabled`；生产二进制不含测试开关。

本机 surface 实际不支持 MAILBOX，验收为 FIFO；MAILBOX 优先由合成策略测试验证。分族 sharing 由合成输入与代码检查覆盖，本机实际为同族 0，不能视为分族硬件实测。

交换链重建使用当前未退役链作为 `oldSwapchain`：等待旧 GPU 工作并解除命令引用，重读窗口事件及 framebuffer 像素尺寸，完成新交换链与全部附件后切换并清理旧代。最小化/零 extent 延迟创建并保留重建请求；同尺寸恢复没有待处理请求时继续复用，有请求则重建。新链创建调用后旧链退役，即使失败也不能回到旧链继续 acquire；失败明确非零退出并清理两代。

交换链重建与退出允许低频 `vkDeviceWaitIdle`，稳定帧只等待当前帧槽。基础 WSI 的 WaitIdle 只证明 GPU 提交完成，不能严格证明呈现引擎已释放交换链/呈现信号量；oldSwapchain 参数也不能补足这一保证。本阶段保留这一已记录限制，未启用 maintenance1/present fence。详见 ARCHITECTURE。

## 项目文档

- [路线图](docs/ROADMAP.md)
- [实际进度与验证](docs/STATUS.md)
- [当前架构](docs/ARCHITECTURE.md)
- [开发规则](AGENTS.md)

OpenSpec 变更记录位于 `openspec/changes`；验收通过后按项目规则独立提交并推送。
