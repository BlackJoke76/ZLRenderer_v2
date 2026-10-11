# 实际进度

当前 change：`initialize-vulkan`，14/14 任务完成，实现与运行验收通过，已提交推送 `main`，尚未归档。2026-10-11 环境基线：SDK `C:\VulkanSDK\1.4.341.1` 的 Vulkan 头文件、import library、validation JSON/DLL 和 `vulkaninfoSDK.exe` 可用；loader 1.4.341，Khronos validation 1.4.341，AMD Radeon RX 7900 XT（设备 API 1.4.349）。未修改驱动、注册表或全局 layer 设置。

本次 `vulkaninfoSDK.exe --summary` 原始输出保存在 `build/vulkanBaseline.log`，包含 `VK_LAYER_GAMEPP uses API version 1.3 ... application ... 1.4` 的 loader WARNING。提案探测还曾出现旧 AMD ICD JSON 路径无法打开、EOS Overlay Win64/Win32 重复的 loader 消息；本次 summary 未复现，不能将这些环境诊断归为应用生命周期错误，也不将未复现视为系统已修复。

## initialize-vulkan 实施验证

已接入 `Vulkan::Vulkan`、空 RAII context、instance/messenger/surface/device 和两种 queue；Debug 初步构建及 10/10 CTest 通过（`build-vulkan`）。真实设备为 AMD Radeon RX 7900 XT，graphicsFamily=0、presentFamily=0，去重请求 1 个族，两个角色取得同一个非空队列。

故障目标复用应用入口，八种条件分别返回 1、输出具体阶段/原因、不输出初始化成功：

| 测试条件 | Vulkan 清理记录（之后均 window → GLFW） |
|---|---|
| 缺 validation layer / debug-utils extension | 没有 Vulkan 对象 |
| instance 成功后失败 | instance |
| messenger 成功后失败 | messenger → instance |
| surface 成功后失败 / 无合适设备 / 模拟 vkCreateDevice 返回 -3 | surface → messenger → instance |
| device 成功后失败 | device → surface → messenger → instance |

各记录逐项比较顺序及次数；诊断测试用真实 debug-utils 提交受控 WARNING，观察标识 `ZLRenderer-ControlledDiagnostic`、正文和退出 0。它只验证回调传递。分族及无队列/缺能力由公共选择函数的合成输入覆盖，未宣称本机走过真实分族设备路径。

Debug 的 Win32 窗口验收通过：标题/可见性、移动到 (100,100)、外框调整为 900×600、事件响应、发送标题栏对应的 SC_CLOSE 系统命令（未模拟鼠标点击）、5 秒内退出 0；日志严格为 device → surface → messenger → instance → window → GLFW。初步日志在 `build-vulkan/test-logs/Debug/` 与 `build-vulkan/Debug/window-logs/`。

应用 pNext 回调复现了旧 AMD ICD JSON ERROR 和 EOS Overlay 重复 WARNING，两者消息标识均为 `Loader Message`。完整 stdout/stderr 保留；测试只按 `tests/knownEnvironmentDiagnostics.txt` 中这两条具体消息归因，其他 WARNING/ERROR 仍失败。无应用 validation WARNING/ERROR。未修复、关闭或隐藏系统层；API 1.0 应用未出现 vulkaninfo 的 GamePP API 1.4 请求警告。

从全新 `build-vulkan-clean` 按 README 配置（重新下载 GLFW）、编译 Debug/Release 均成功，使用 `--parallel 2` 限制验证并行度。两个配置各 10/10 CTest 和完整窗口验收通过。命令分别为 `cmake -S . -B build-vulkan-clean -G "Visual Studio 17 2022" -A x64`、`cmake --build build-vulkan-clean --config <Debug/Release> --parallel 2`、`ctest --test-dir build-vulkan-clean -C <Debug/Release> --output-on-failure`、`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-vulkan-clean/<Debug/Release>/zlRenderer.exe`。原始日志保存在该目录的 `test-logs/<配置>/` 与 `<配置>/window-logs/`，两个配置均显示 validation 启用、族 0 和完整清理序列。

附加验证：

- `build-vulkan-asan` 使用 `/EHsc /Zi /fsanitize=address` 和链接 `/DEBUG`，Release 的 10/10 CTest 及同样窗口验收通过，无 ASan 报告。C++ 应用与测试代码插桩，GLFW C 源码及外部驱动/layers 未插桩。验证进程 PATH 临时加入 MSVC `14.38.33130/bin/Hostx64/x64` 以找到 ASan DLL；首次未加时 Windows 返回 `0xc0000135`，发生于程序进入 main 前。MSVC 不支持 UBSan，本次没有可用的 clang 编译器，未运行 UBSan。
- `build-vulkan-analysis` 使用 `/EHsc /analyze`，三个 C++ 目标静态分析通过，无警告。日志 `static-analysis.log`。首次临时覆盖 CXX_FLAGS 时漏 `/EHsc`，触发 C4530；修正验证参数后通过，普通配置一直使用 CMake 默认异常展开参数。
- 单次验证进程设置 `VK_LOADER_DEBUG=layer`，原始日志 `build-vulkan-clean/validation-layer-evidence/window.stderr.log` 明确显示 `Insert instance layer "VK_LAYER_KHRONOS_validation"` 和 `Inserted device layer "VK_LAYER_KHRONOS_validation"`，装载 SDK 中实际 DLL；窗口仍完整验收通过。此诊断变量仅属于该进程，未更改系统 layer 设置。
- 新规格 6 条要求及现有窗口/入门规格逐项核对：正常/失败路径、README 从头配置、强制 validation、公共队列选择与去重、所有权/清理顺序均有上述代码和记录支撑。没有 swapchain、MAILBOX 选择、GPU 提交或等待；既有 AGENTS 约定与 bootstrap 归档未改动。

`openspec validate initialize-vulkan --strict`、工作区与暂存区 `git diff --check` 均通过。产品 Release 二进制不包含测试环境变量、故障注入或受控消息标识。

Git 交付已核实：工程提交 [`ffa2f73065111b18d346934f11ee7772bc4639df`](https://github.com/BlackJoke76/ZLRenderer_v2/commit/ffa2f73065111b18d346934f11ee7772bc4639df) 已推送，`git ls-remote` 与 GitHub API 的 main SHA 均一致。分支 `main`，upstream `origin/main`，origin 为 `https://github.com/BlackJoke76/ZLRenderer_v2.git`。任务完成状态和本交接记录随单独的 `docs(initialize-vulkan)` 提交保存，最终文档 SHA 以 Git 历史为准。本地 `.agents/` 与两个预存 `.gitkeep` 未暂存；既有 bootstrap 归档不变。

2026-10-11，`openspec-verify-change` 已复核：14/14 任务、6/6 新要求、13 个场景均有实现与验证依据，6 项设计决策及代码一致性检查通过；0 critical / warning / suggestion。复跑 Debug/Release 编译，各 10/10 CTest 和真实窗口验收通过；新窗口原始日志在 `build-vulkan-clean/verify-logs/<配置>/`。分族按设计使用合成输入及代码核对；本机仍只实际验证族 0。完整报告在 `openspec/changes/initialize-vulkan/verification.md`，此次仅增加报告和交接记录。

后续：用户请求 archive 时同步 `vulkan-context` 主规格后归档。当前 change 保持 active，现有两个主规格未改写。

## bootstrap-project 历史记录

已完成 change：`bootstrap-project`，14/14 任务完成，已归档至 `openspec/changes/archive/2026-10-11-bootstrap-project/`。
`desktop-window` 和 `project-onboarding` 已同步为主规格，各 4 条要求，Purpose 与场景原文保留，严格校验通过。

## 环境

- Windows x64；Visual Studio Community 2022 17.14.7。
- VS 自带 CMake 3.31.6-msvc6；Git 2.54.0.windows.1。
- 实际编译器 MSVC 19.38.33145，Windows SDK 10.0.26100.0。
- GLFW 3.4，官方 tag 已核实为 `a74efa0d5628b74adc0426af4c5710e287fa7c2c`。

## 验证记录

2026-10-11，在普通 PowerShell 使用 README 的 VS CMake PATH 配置：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` 与 `cmake --build build --config Debug --parallel` 成功。
- 将目录替换为全新的 `build-clean` 后，两条命令仍成功；运行 `build-clean/Debug/zlRenderer.exe` 打开可见的 ZLRenderer 窗口。
- 用临时 Win32 验证脚本观测窗口可见性、标题、移动和调整到 900×600、事件响应；发送标题栏关闭按钮使用的 `SC_CLOSE` 系统命令，进程在 5 秒内退出，退出码 0。未模拟鼠标点击。
- 初始化故障注入（非法平台）与窗口创建故障注入（宽度为 0）均输出 GLFW 错误和应用错误，退出码 1；创建失败时观测到 GLFW 终止。注入仅在忽略的 build 目录复制源码中进行，临时源码已移除；产品源码未加入测试开关，最终 Debug 重建通过。
- 应用的 MSVC ASan Release 构建与同样窗口验收通过，无 ASan 报告；验证参数 `/EHsc /Zi /fsanitize=address`、链接 `/DEBUG`。GLFW 的 C 源码未被该 C++ 参数插桩。MSVC 不提供 UBSan，本次未运行 UBSan。
- `openspec validate bootstrap-project --strict` 通过。命名、资源释放顺序和文档已对照规格检查。
- verify 阶段复跑普通 Release 和 Debug 窗口验收、两种失败验证二进制均通过。无阻塞问题；1 项非阻塞警告：CMake 当前固定的 `a74efa0d5628b74adc0426af4c5710e287fa7c2c` 是 GLFW 3.4 的附注 tag 对象 SHA，实际 commit SHA 为 `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`。构建来源仍固定，但 README 称其为“提交”与设计要求的 commit SHA 有描述偏差，后续修订时统一。

上述临时验证脚本和产物在忽略的 build 目录内，不属于发布工程。

## 交接与限制

只实现无图形 API 的窗口；尚未实现 Vulkan、swapchain 或 MAILBOX 模式。
OpenSpec schema 读取在本机 Windows 沙箱内会因 realpath EPERM 失败，获准的沙箱外 CLI 调用正常。
GitHub 目标为 https://github.com/BlackJoke76/ZLRenderer_v2（PUBLIC），仓库已存在，复用并保留其 MIT LICENSE。
用户已授权接到远程 main 历史并改用 HTTPS；本地分支 main，origin 为该 HTTPS URL。原远程提交为 `20132ca888504f4e0f73b8dfe5369686340e6187`。
SSH 原因是 host key verification 失败；未更改用户 SSH 设置。
工程提交 `98a9a62f0b36bcf1cd12eaae0dae49274106351e` 已成功推送到 `origin/main`，远程 SHA 核对一致，upstream 为 `origin/main`。
随后用仅包含本 change 交付记录的文档提交同步任务完成状态；最终提交 SHA 以 Git 历史为准。
2026-10-11，用户确认同步主规格后归档；两份主规格与 delta 内容逐项核对一致，active changes 已为空。归档和主规格更新纳入独立 `bootstrap-project` 归档提交，目标仍为 `main` / `origin/main`；同步结果以 Git 历史和远程 SHA 核对为准。
预先存在的 `.agents/` 本地技能文件保留，未纳入本 change 提交。无其他未提交的工程文件。
