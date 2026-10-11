# ZLRenderer

Windows 上的 C++20 / GLFW / Vulkan 开发工程。在可调整大小的空窗口中建立 Vulkan instance、surface、设备和队列，启用 validation，处理事件并正常关闭。

## 前置条件

- Windows 10/11 x64，有可交互桌面。
- Visual Studio 2022，安装“使用 C++ 的桌面开发”、MSVC x64 工具链、Windows SDK 和 C++ CMake 工具。
- CMake 3.24 或更新版本、Git 可用。首次配置需要访问 GitHub 下载 GLFW。
- 使用 PowerShell，当前目录为项目根目录。源码路径包含空格或单引号时仍需正常引用。
- 安装 Windows x64 Vulkan SDK（包含头文件、import library 和 `VK_LAYER_KHRONOS_validation`），重新打开终端使 `VULKAN_SDK` 生效。CMake 自动查找 SDK，也可用 `-DVulkan_ROOT="$env:VULKAN_SDK"` 指定安装根目录。
- 安装支持 Vulkan 1.0 和当前窗口呈现的显卡驱动及 Vulkan loader。先运行 `& "$env:VULKAN_SDK\Bin\vulkaninfoSDK.exe" --summary`，确认设备和 Khronos validation layer 可见。
- Debug 和 Release 都要求 validation 与 `VK_EXT_debug_utils`；缺项时明确失败，不会自动关闭 validation。SDK 缺失会使配置失败；loader DLL 缺失可能由 Windows 在进入 `main` 前拒绝启动。

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

运行后应出现标题为 `ZLRenderer`、初始客户区 1280×720 的空窗口，控制台确认 validation 开启、所选设备/队列族、非空 queues 和 `Vulkan initialization succeeded`。移动、调整大小后点击系统关闭按钮；进程退出且 `$LASTEXITCODE` 为 `0`。尚无 swapchain 和渲染内容，空闲时等待事件。

Release 使用同一个配置目录：

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
& .\build\Release\zlRenderer.exe
```

要验证从头构建，使用新的目录（例如将以上 `build` 替换为 `build-clean`），避免依赖已有构建缓存。初始化或创建窗口失败会向标准错误输出原因并返回非零退出码。

CTest 默认开启，包含队列选择及去重测试，以及在实际 Vulkan 上运行的缺项/分阶段失败/受控诊断子进程测试，需满足上述 SDK、驱动及桌面条件。故障注入只在 `vulkanLifecycleTests` 目标编译，正常应用没有注入开关。只构建应用可在配置时指定 `-DBUILD_TESTING=OFF`。

可重复的可见窗口验收（初始化完成后测试移动、调整大小、系统关闭与精确清理顺序）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verifyWindow.ps1 -executablePath .\build\Debug\zlRenderer.exe
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verifyWindow.ps1 -executablePath .\build\Release\zlRenderer.exe
```

脚本保存完整日志到对应配置目录的 `window-logs`；CTest 日志在 `build/test-logs/<配置>/`。脚本发出与标题栏关闭按钮一致的 SC_CLOSE 命令，不模拟鼠标点击。

## 诊断与退出

成功日志示例（设备/族索引随机器不同）：

```text
Created instance; validation enabled: VK_LAYER_KHRONOS_validation (API 1.0)
Selected device: AMD Radeon RX 7900 XT; graphicsFamily=0 presentFamily=0
Vulkan initialization succeeded
Destroyed device
Destroyed surface
Destroyed debug messenger
Destroyed instance
Destroyed window
Terminated GLFW
```

失败示例：`Initialization failed: instance prerequisites: missing layer VK_LAYER_KHRONOS_validation`，或 `Initialization failed: logical device: vkCreateDevice failed (VkResult=...)`。失败返回 1，不进入主循环；只销毁已经成功创建的对象。

本机存在旧 AMD ICD JSON 无法打开、EOS Overlay 重复的 loader 环境诊断。应用照常输出，验证保留原始日志，只依据 `tests/knownEnvironmentDiagnostics.txt` 中两条具体消息归因；新的消息仍需检查。应用 validation 消息不能因此忽略。详细环境与验证证据见 STATUS。

## 项目文档

- [路线图](docs/ROADMAP.md)
- [实际进度与验证](docs/STATUS.md)
- [当前架构](docs/ARCHITECTURE.md)
- [开发规则](AGENTS.md)

OpenSpec 变更记录位于 `openspec/changes`；验收通过后按项目规则独立提交并推送。
