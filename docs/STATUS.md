# 实际进度

当前活动 change：`handle-swapchain-resize`。2026-10-14 实施、验收及 Git 交付通过，19/19 项完成；未同步主规格或归档。已按用户要求通过 oldSwapchain 创建新链，完成新一代附件后切换并清理旧代，保留两个帧槽；等待后复查尺寸/关闭、零 extent 延迟和重复最小化恢复已验证。Debug/Release/ASan 各 50/50 CTest 通过，/analyze 无警告，五个实际窗口流程通过，详细报告见下方。

本机 AMD Radeon RX 7900 XT，graphics/present 族均为 0；当前 surface 模式选择实际为 FIFO 回退，3 张图像，格式 B8G8R8A8_SRGB（50）。MAILBOX 优先由纯选择测试覆盖；分族 sharing 和配置变化是合成输入/代码检查，未宣称真实分族或 MAILBOX 硬件实测。

基础 WSI 限制按提案保留：稳定帧的 present semaphore 按图像和再次 acquire 同步复用；退出/简化重建采用常用 WaitIdle 清理，它证明 GPU 提交完成，不能严格证明呈现引擎完成。未启用 maintenance1/present fence，validation 无错误不能作为补足此保证的证据。细节见 ARCHITECTURE。

## handle-swapchain-resize 验收与交接

报告：`openspec/changes/handle-swapchain-resize/verification.md`，4 条 delta 要求、25 个场景逐项关联实际/合成/控制及代码证据；严格校验与 git diff --check 通过。共享创建/释放用于初建与候选代，unique_ptr 保护候选异常清理；创建调用后的旧链已退役，失败停止正常循环并清理两代，不回滚 acquire 旧链。

独立 `build-resize` 的 Debug/Release 无警告构建与各 50/50 CTest 通过。使用现有固定 GLFW 3.4 源码目录配置独立产物，未重新下载；完整 build/ctest 汇总在该目录的 `build-<配置>.log` 与 `ctest-<配置>.log`，原始测试诊断在 `test-logs/<配置>/`。连续 1→2→3 真交换链参数链、附件存活与帧归属、两个帧槽身份、候选各阶段失败均核验；创建失败的调用前注入只验证 host 控制，不声称真实驱动失败/退役。

| 实际窗口流程 | submitted/presented | 代数 | 证据目录 |
|---|---|---|---|
| Debug 三次最小化恢复、可见关闭 | 2393/2393 | 9 | build-resize/final-window-logs/Debug |
| Debug 三次恢复后最小化关闭 | 2348/2348 | 9 | build-resize/final-minimized-close-logs/Debug |
| Release 三次恢复、可见关闭 | 2360/2360 | 9 | build-resize/final-window-logs/Release |
| Release 三次恢复后最小化关闭 | 2342/2342 | 9 | build-resize/final-minimized-close-logs/Release |
| ASan Release 三次恢复、可见关闭 | 2341/2341 | 9 | build-resize-asan/final-window-logs/Release |

每流程持续无输入至少 10 秒，逐尺寸匹配实际 framebuffer/客户区/extent，在同一 DPI 上下文查询并保存物理客户区 PNG 与25点内部/边缘颜色采样（RGB 63,89,124）；快速缩放最终收敛，稳定阶段不增加代计数；每轮暂停/恢复快照的提交/呈现计数相同，恢复后继续推进。可见与最小化关闭都在正常设备条件下 5 秒内退出 0，资源计数平衡，无未知应用 validation/sync WARNING/ERROR。脚本只将自己启动的窗口置顶，不操作其他窗口。

`build-resize-asan`：/EHsc /Zi /fsanitize=address、链接 /DEBUG，Release 50/50 CTest 与窗口通过；ASan DLL 路径只设于验证进程 PATH。`build-resize-analysis`：/EHsc /analyze，四个 C++ 目标无警告。未发现可用 Clang/UBSan，MSVC 不提供 UBSan，未宣称该项通过。测试目标显式 layer settings 启用 sync validation，生产窗口脚本给子进程 VK_LAYER_VALIDATE_SYNC=1；未修改驱动或全局配置。

实际硬件仍为 RX 7900 XT、同族 0、FIFO、格式 50、3 图像/2 帧槽。MAILBOX、分族及跨代格式/图像数量变化保持合成/代码核查性质。保留原始具体 AMD loader 诊断，没有扩大过滤。基础 WSI WaitIdle 限制保持明示，oldSwapchain 不补足呈现引擎完成保证；扩展路径仍另行规划。

交付目标已确认：`https://github.com/BlackJoke76/ZLRenderer_v2.git`，main / origin/main。实施提交 [`5921f80e7577a896b5a66c62c6e98769b6d48de7`](https://github.com/BlackJoke76/ZLRenderer_v2/commit/5921f80e7577a896b5a66c62c6e98769b6d48de7) 已推送，git ls-remote 核对远程 SHA 一致。只提交本 change 的源码、测试、规划、报告及文档；既有 `.agents/`、两个 `.gitkeep` 保留。19/19任务与交付回执随本 change 的后续独立交接提交保存，最终回执 SHA 以 Git 历史及交付输出为准。

## present-clear-frame 验收与交接

完整报告：`openspec/changes/archive/2026-10-12-present-clear-frame/verification.md`，覆盖8条新增帧呈现要求、3条context变更要求、1条文档变更要求，共34个场景。全部24/24任务完成，实施、验证与Git交付均已核对。实施与复核阶段未同步主规格；随后同步归档的结果见下方。

全新 `build-present-clean` 从头配置和 Debug/Release 构建通过，/W4 /WX；各34项测试包含生产策略、真实帧对象身份复用、缺项/阶段回滚、重建和错误控制。测试目标用VK_EXT_layer_settings显式启用同步validation，生产窗口脚本使用SDK进程变量，不改全局配置。Release产品无测试环境变量/注入标识。

| 实际窗口验收 | submitted/presented | 证据目录（原始日志、截图、verification.txt） |
|---|---|---|
| Debug持续颜色、缩放、最小化恢复、关闭 | 2036/2036 | build-present-clean/window-logs/Debug |
| Debug最小化期间关闭 | 1998/1998 | build-present-clean/minimized-close-logs/Debug |
| Release持续颜色、缩放、最小化恢复、关闭 | 2044/2044 | build-present-clean/window-logs/Release |
| Release最小化期间关闭 | 2000/2000 | build-present-clean/minimized-close-logs/Release |
| Release替代颜色（测试目标） | 2064/2064 | build-present-clean/alternate-color-logs/Release |
| ASan Release完整窗口流程 | 2068/2068 | build-present-asan/window-logs/Release |

每次无输入至少10秒，重复缩放至5代，9点采样默认RGB63,89,124、替代RGB124,63,89；退出0、资源创建/释放平衡，只有既有具体AMD loader诊断，无未知应用validation/sync错误。截图DPI修正后重验；一次并发窗口遮挡失败截图/日志保留，最终顺序运行通过。

`build-present-asan`：/EHsc /Zi /fsanitize=address，链接/DEBUG，34/34回归及窗口通过，无主机内存问题。`build-present-analysis`：/EHsc /analyze，四个C++目标无警告。未安装Clang，MSVC无UBSan，本次未运行或宣称UBSan通过。未修改驱动或全局配置。

交付目标已确认：`https://github.com/BlackJoke76/ZLRenderer_v2.git`、`main`、upstream `origin/main`。只提交本change成果，未跟踪的 `.agents/` 与两个既有 `.gitkeep` 保留。实现提交 [`4dfcc6512689c4c2818e2c3c03fa042c38b55aaa`](https://github.com/BlackJoke76/ZLRenderer_v2/commit/4dfcc6512689c4c2818e2c3c03fa042c38b55aaa) 已推送，git ls-remote确认main一致。任务完成与交付记录随单独docs(present-clear-frame)提交保存，最终文档SHA以Git历史及交付输出为准。

2026-10-12，`openspec-verify-change` 独立复核 `present-clear-frame`：24/24任务、12/12要求、34/34场景有对应证据，7项设计与代码模式核查通过；0 critical / warning / suggestion，无跳过检查。重新构建Debug/Release，各34/34 CTest；两种配置的正常/最小化关闭和Release替代颜色窗口验收均通过。新证据在 `build-present-clean/verify-logs/`，详细计数及代码定位见归档内的verification.md。该次复核只更新报告和交接记录，未同步或归档。

真实硬件仍为FIFO、族0、3图像/2帧槽；MAILBOX/分族的合成覆盖与基础WSI清理限制保持明示。替代色截图两次被聊天界面遮挡，失败证据保留；通过忽略目录内的临时脚本仅将验收自身窗口置顶后重验通过，仓库脚本与产品代码未改动。已核对先前ASan记录及静态分析构建参数，该轮未重跑。复核提交 [`114c73a9781ce5f68d1454c8f0bc136538b9c921`](https://github.com/BlackJoke76/ZLRenderer_v2/commit/114c73a9781ce5f68d1454c8f0bc136538b9c921) 已推送main并核对远程SHA。

2026-10-12，沿用用户“同步主规格后归档”的选择，完成 `present-clear-frame` 归档（schema：spec-driven）。新增 `openspec/specs/frame-presentation/spec.md` 的8条要求；更新 `vulkan-context` 的3条要求与 `project-onboarding` 的1条要求。12条delta要求和34个场景逐项与主规格比对一致；新Purpose原文保留，既有主规格标题、Purpose及未涉及要求不变，无delta操作标题或TBD占位。

同步后的4份主规格严格校验全部通过，归档前change严格校验通过。8个change文件（含 `.openspec.yaml`、三份delta及verification）移动前后SHA256完全一致，归档路径为 `openspec/changes/archive/2026-10-12-present-clear-frame/`，active changes为空，无未完成材料或任务警告。核查快照在忽略目录 `build-present-clean/verify-logs/pre-archive-main-specs.json` 和 `archive-manifest.json`。归档内报告保留当时实施/复核状态文字，当前状态以本交接记录为准。

本次只同步规格、移动归档并更新STATUS/ROADMAP；产品代码未改动，不重复运行已通过的构建和窗口验收。归档和主规格更新纳入独立 `present-clear-frame` 文档提交，目标仍为已核实的HTTPS origin、main / origin/main；最终提交与远程同步SHA以Git历史及交付结果为准。既有其他归档、desktop-window及未跟踪的 `.agents/` 和两个 `.gitkeep` 保留。

## initialize-vulkan 历史交付

最近完成：`initialize-vulkan`，14/14 任务完成，实现与运行验收通过，已提交推送 `main`，已归档至 `openspec/changes/archive/2026-10-11-initialize-vulkan/`。2026-10-11 环境基线：SDK `C:\VulkanSDK\1.4.341.1` 的 Vulkan 头文件、import library、validation JSON/DLL 和 `vulkaninfoSDK.exe` 可用；loader 1.4.341，Khronos validation 1.4.341，AMD Radeon RX 7900 XT（设备 API 1.4.349）。未修改驱动、注册表或全局 layer 设置。

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

2026-10-11，`openspec-verify-change` 已复核：14/14 任务、6/6 新要求、13 个场景均有实现与验证依据，6 项设计决策及代码一致性检查通过；0 critical / warning / suggestion。复跑 Debug/Release 编译，各 10/10 CTest 和真实窗口验收通过；新窗口原始日志在 `build-vulkan-clean/verify-logs/<配置>/`。分族按设计使用合成输入及代码核对；本机仍只实际验证族 0。完整报告现位于 `openspec/changes/archive/2026-10-11-initialize-vulkan/verification.md`。复核记录提交 `e28bfeb43366f0b541476aa8c655c818d3dd936c` 已推送至 main。

2026-10-11，沿用用户“同步主规格后归档”的选择，新增 `openspec/specs/vulkan-context/spec.md`，保留 delta 的 Purpose、6 条要求及 13 个场景原文，仅规范主规格标题和 Requirements 分区。同步前后逐项内容比较一致，`openspec validate --specs --strict` 三份主规格全部通过。归档前 change 的严格校验通过；移动后的 6 个文件（包括 metadata 和 verification）SHA256 均不变，`openspec list --json` 的 active changes 已为空，无未完成任务/材料警告。

归档和主规格更新纳入独立 `initialize-vulkan` 文档提交，目标为已核实的 `main` / `origin/main`；提交及远程 SHA 以 Git 历史和交付结果为准。此次归档时无 active change；已有窗口/入门主规格和 bootstrap 归档保持原样。之后由新的 present-clear-frame change 实施交换链与清屏。

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
