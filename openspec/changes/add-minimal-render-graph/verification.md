# add-minimal-render-graph 验证与交接

日期：2026-10-18（北京时间）。本报告为 apply 阶段的实现自检与验收证据，不表示已执行独立 verify 或 archive。实现范围为单 graphics queue、整张单 mip/layer/sample 外部颜色图像、有序 raster pass、自动同步与一对一执行组；真实 GPU 运行生产单清屏及测试双清屏。没有 shader、采样绘制、自动合并、重排、临时 GPU 图像或资源别名。

## 结果与环境

- 独立 `build-render-graph`：Debug/Release 无警告构建；各 68/68 CTest 通过，分别 39.97 / 41.70 秒。保留原 50 项，新增 9 项纯 CPU 图测试、3 项 Vulkan 参数/绑定/能力测试及 6 项实际后端执行/错误控制。
- 强化内容负例后，Debug/Release 各复建 CPU 测试目标并复跑 `graph-contents` 通过（各 0.08 秒总计时）；有效输出先建立，保证 Load 未定义内容不能被其他失败掩盖，并另测 Discard 后 Load 拒绝。该补强未改产品代码。
- `build-render-graph-asan`：Release 无警告构建，68/68 CTest，87.37 秒；默认与双清屏窗口均通过，无 ASan 报告。参数为 `/EHsc /Zi /fsanitize=address`、链接 `/DEBUG`；仅验收进程 PATH 加入 MSVC ASan DLL 所在目录。
- `build-render-graph-analysis`：全部六个 C++ 目标 `/EHsc /analyze` 构建通过，无警告；内容负例补强后对应目标再次分析通过。MSVC 不提供 UBSan，本轮未运行或宣称 UBSan 通过。GLFW C、外部驱动/layers、PowerShell 截图助手不在 C++ ASan 插桩范围。
- MSVC 19.38.33145、VS CMake 3.31.6-msvc6、SDK 1.4.341.1；API 仍为 Vulkan 1.0。新构建目录通过 `FETCHCONTENT_SOURCE_DIR_GLFW` 使用既有固定 GLFW 源码，不复用旧应用对象文件，不重新下载依赖。
- 本轮实际 AMD Radeon RX 7900 XT，graphics/present family=0，FIFO 回退，format=50 / B8G8R8A8_SRGB，3 张图像 / 2 帧槽。MAILBOX 优先与分族 sharing 由生产选择函数的合成测试覆盖；图的 queue-family indices 为 IGNORED，不宣称 MAILBOX 或分族硬件实测。

主要命令（完整原始日志在下述忽略的 build 目录）：

```powershell
# 使用 README 的 VS CMake PATH；配置 Debug/Release 的独立目录
cmake -S . -B build-render-graph -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_GLFW="$PWD/build-present-clean/_deps/glfw-src"
cmake --build build-render-graph --config Debug --parallel 1 -- /nr:false
cmake --build build-render-graph --config Release --parallel 1 -- /nr:false
ctest --test-dir build-render-graph -C Debug --output-on-failure
ctest --test-dir build-render-graph -C Release --output-on-failure

powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-render-graph/Debug/zlRenderer.exe -logDirectory build-render-graph/window-logs/Debug/default-visible
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-render-graph/Debug/vulkanLifecycleTests.exe -testCase graph-double-window -logDirectory build-render-graph/window-logs/Debug/double
```

默认最小化关闭增加 `-closeWhileMinimized`，替代色使用 `-testCase alternate-window`；Release 替换配置名。ASan 配置日志记录插桩及链接参数，运行命令与常规目录一致；两次 ASan 窗口只将 executable/logDirectory 替换为该目录。原始构建/CTest 汇总文件名为 `configure.log`、`build-<配置>.log`、`ctest-<配置>.log`，子进程输出为 `test-logs/<配置>/<case>.stdout.log/.stderr.log`。

## 真实窗口证据

| 流程 | submitted / presented | 代数 | 本地证据目录 |
|---|---|---|---|
| Debug 默认，可见关闭 | 2322 / 2322 | 9 | build-render-graph/window-logs/Debug/default-visible |
| Debug 默认，最小化关闭 | 2313 / 2313 | 9 | build-render-graph/window-logs/Debug/default-minimized |
| Debug 替代颜色 | 1993 / 1993 | 9 | build-render-graph/window-logs/Debug/alternate |
| Debug 双清屏 | 1958 / 1958 | 9 | build-render-graph/window-logs/Debug/double |
| Release 默认，可见关闭 | 2312 / 2312 | 9 | build-render-graph/window-logs/Release/default-visible |
| Release 默认，最小化关闭 | 2344 / 2344 | 9 | build-render-graph/window-logs/Release/default-minimized |
| Release 替代颜色 | 2013 / 2013 | 9 | build-render-graph/window-logs/Release/alternate |
| Release 双清屏 | 1815 / 1815 | 9 | build-render-graph/window-logs/Release/double |
| ASan Release 默认 | 2264 / 2264 | 9 | build-render-graph-asan/window-logs/Release/default |
| ASan Release 双清屏 | 1738 / 1738 | 9 | build-render-graph-asan/window-logs/Release/double |

每个目录有完整 stdout/stderr、verification.txt，以及 initial/sustained/逐尺寸/恢复的客户区 PNG。脚本逐次匹配 framebuffer/extent，在同一 DPI 上下文截图，25 点内部/边缘颜色检查通过；默认及双清屏最终为 RGB 63,89,124，替代色为 RGB 124,63,89。每流程无输入持续至少 10 秒、多次缩放及快速变化收敛、三轮最小化恢复、暂停计数冻结/恢复推进，正常设备条件下请求关闭后 5 秒内退出 0；资源数量平衡。脚本仅操作自己启动的窗口，顺序运行以避免遮挡。

生产窗口完整图记录为每代一次、共 9 个有界帧；测试窗口另有前 12 帧记录，本轮各为 19 个有界帧。共享 checkRenderGraph.cmake 对全部这些记录核对实际组/barrier/图像绑定与呈现请求。双清屏记录为 `before-0 → group-0 → before-1 → group-1 → after-1`，两个组共用实际 image/framebuffer/render pass，在同一个 command buffer 内只 submit 一次。组间 old/newLayout 均为 2、src/dst stage=1024、src/dst access=256；最终才转为 PRESENT_SRC_KHR。像素结果、CPU 依赖和实际参数三者共同构成证据。

## 9 条要求 / 30 个场景逐项映射

以下 CPU/参数测试均包含在 Debug、Release 与 ASan 的 CTest 中；“实际”指本轮真实设备执行，“控制”指测试目标的明确注入。

| 要求与场景 | 证据与覆盖方式 |
|---|---|
| Declare borrowed external color images / Declare multiple external resources | graph-declarations：两个外部资源及三个 pass，CPU 无 GPU 分配 |
| Reject an invalid resource reference | graph-references：默认无效与非本图 handle 拒绝 |
| Bind the acquired image | graph-bindings 参数检查；实际 normal 中 slot=0/image=2、slot=1/image=0，原 Frame submitted 记录按图像/帧槽分别核对 |
| Reject hidden physical aliasing | graph-bindings：不同逻辑 handle、同物理句柄拒绝；host-only fixture，不提交伪句柄 |
| Describe ordered raster work / Clear the same target twice | graph-waw、presentation-graph-double；两配置及 ASan 双清屏实际窗口 |
| Preserve defined attachment contents | graph-raw：Load 的 RAW 与 WAW；graph-mapping：LOAD/STORE 参数，非清屏保留路径不称 GPU 实测 |
| Reject unsupported use combinations | graph-references 的反馈/无效附件、graph-shapes 的 mip/layer/MSAA；API 不提供 MRT、多队列、深度入口 |
| Compile deterministic ordered plans / Compile a supported ordered graph | graph-declarations：相同输入两次编译的依赖和组相同，保持声明顺序 |
| Reject a read of undefined contents | graph-contents：采样/Load 未定义内容、Discard 后出口与 Load 均失败，重新 Clear 建立有效内容 |
| Reject an invalid presentation exit | graph-exits：空工作、缺失、非可呈现与重复出口 |
| Infer single queue synchronization / Make a color write available to sampling | graph-raw + graph-mapping：颜色写→fragment 读取、layout 与 scope；仅 CPU/参数 |
| Wait for reads before overwriting | graph-war + graph-mapping：真实逻辑 WAR、伴随布局变化与执行依赖参数区分；仅 CPU/参数 |
| Synchronize consecutive writes | graph-waw 与 graph-mapping；双清屏真实 WAW barrier、最终 B 色及 sync validation |
| Cover every outstanding reader | graph-readers：两个读取者到后续覆盖的 WAR 均存在，源范围包含读者及先前写入 |
| Avoid a dependency caused only by compatible reads | graph-readers：第二次兼容采样不新增图像 barrier，后续覆盖仍保留两位读者 |
| Separate logical passes / Preserve one to one grouping | graph-declarations：三 pass 三组、出口仅最终转换；真实双清屏两组 |
| Execute through group boundaries | recordGraphFrame 遍历编译组；checkRenderGraph.cmake 核对实际调用顺序 |
| Apply explicit backend synchronization / Acquire render and present one image | 单清屏实际；获取 barrier、submit wait stage、最终转换与实际 present 请求关联 |
| Keep the image writable between clear groups | 双清屏实际日志证明同布局 WAW，中间无呈现转换、最后一次转换唯一 |
| Reject an unusable backend binding | graph-bindings、graph-recording 参数测试；实际 graph-stale/missing/unsupported 控制分别报告对应原因 |
| Preserve frame/swapchain lifecycle / Replace the swapchain generation | 原 old-chain/候选失败/resize 测试与 10 个窗口流程；逐代附件日志和图绑定一致 |
| Skip a frame or pause presentation | 原 fence-timeout/acquire-timeout/acquire-out-of-date/零尺寸/关闭控制；实际三轮暂停及计数冻结，无虚构图成功记录 |
| Consume a suboptimal acquisition | acquire-suboptimal 在真实 acquire 后改变 host 结果，正常消费信号并完成呈现后重建；不宣称驱动真实产生该结果 |
| Fail before successful submission | 原 record/submit 与 graph-binding/stale/missing/group/unsupported；退出 1、submitted=presented=0，日志/清理平衡；编译错误由 CPU 测试验证 |
| Expose dependencies/groups/binding / Inspect a submitted clear plan | 生产每代 Graph plan/resource/dependency、实际 API 输入记录及帧槽/图像绑定 |
| Inspect the dependency between two clears | 双清屏 actual API 序列、WAW 原因、同 image/framebuffer/render pass 和唯一最终转换 |
| Keep diagnostics bounded and truthful | checkRenderGraph.cmake 检查代/版本 dump 唯一且与成功提交关联；失败帧不发布采集记录；实际 present result 在控制注入前记录 |
| Validate distinct coverage levels / Verify compiler cases without a GPU | 独立 renderGraphTests 仅链接 C++ 标准库，9 个纯 CPU 用例；Vulkan 参数测试另列 |
| Verify two clear passes on the GPU | Debug/Release/ASan 的双清屏 CTest 与窗口、25 点 B 色、日志及同步诊断 |
| Preserve the presentation regression baseline | 原 50 项全保留、全部 68 项通过、两配置默认/替代/双清屏及可见/最小化关闭；真实/合成与 WSI 限制明确列出 |

## 设计一致性与实现定位

| 决策 | 实现与核查 |
|---|---|
| 用途声明、附件清屏、typed handle | renderGraph.h/.cpp；clearColor.h 共用原颜色校验，不重复默认值 |
| 按序完整访问关系与内容有效性 | RenderGraph::compile，ImageHistory 保留 writer/readers，图内依赖与实际 barrier 分开 |
| 一对一物理执行组 | CompiledRenderGraph 的 groups，recordGraphFrame 实际遍历；没有自动合并和每逻辑 pass 单独 submit |
| 显式边界同步 | vulkanRenderGraph.cpp 映射及 Presenter 组边界发出；实际 render pass initial/final=2、显式 EXTERNAL dependencies=0 |
| 图与实际资源所有权 | VulkanBackend 持有 graph/compiledGraph，在 Presenter/context 之后析构；SwapchainResources 仅拥有实际对象，绑定为 acquire 后局部变量 |
| 最小执行与失败控制 | 单/双清屏用相同执行器；record/submit 失败不计成功，不等待未提交 fence，旧链退役规则未改 |
| 真实输入诊断 | captureTrace 仅在代表帧/测试有限帧采集 API 输入字符串，submit 成功才发布；稳定帧无 trace 分配及完整 dump |
| 分层验收 | CPU / 参数 / 真 GPU / 控制证据按上述表分类；原窗口脚本共用检查器，没有复制另一套完整窗口流程 |

Release 产品二进制检查未包含 ZL_VULKAN_TEST_CASE、graph-double-window 或 injected failure 标记；测试控制均在独立目标内。正常帧没有新增 WaitIdle 或命令/同步对象分配，原身份/计数检查继续通过。

## 验证中修正与限制

- 首轮图错误路径的日志检查器把两边均空的计划/提交列表比较为不同变量名，造成 5 项 harness 失败；改为展开并引用字符串后，7 项相关 GPU 用例复测和全部 CTest 通过。原始首次输出保留在 build-render-graph/initial-graph-log-check，不将这次 harness 失败称为 GPU 同步问题。
- 真 GPU 主要覆盖 Clear/Store、外部 acquire、WAW 与 present。RAW/WAR 采样关系、Load/Discard 变体、隐藏别名、非本图绑定以 CPU/参数/控制证据为主，不声称采样 shader、全部状态组合或分族硬件已验证。
- 未实现自动 pass 合并/重排、裁剪、别名、跨队列、子资源、MRT/深度/MSAA、绘制 payload。后续增加实际绘制必须完整声明资源使用；当前录制主动拒绝采样绘制，不从 shader 自动反射未声明访问。
- 既有基础 WSI 限制不变：提交 fence/WaitIdle 证明 GPU 提交完成，不能严格证明 presentation engine 完成使用 semaphore/swapchain。未启用 maintenance1/present fence，不以 validation 无错误补足保证。
- 原始日志、PNG 和构建产物保留在忽略目录，不提交二进制。没有修改全局驱动、注册表或 layer 配置；仍只按原两条具体 loader 环境消息归因。

## Git 与工作流状态

交付目标已核实为 origin = https://github.com/BlackJoke76/ZLRenderer_v2.git，main / origin/main。实施前本地 HEAD 与远端 main 均为 2590e4c58d7c055cd0b0b647ba9345387b9bd579；受限内网络线程失败后，获准的受限外只读核对成功。验收成果按 AGENTS 独立提交推送，最终 SHA 及推送回执见 Git 历史与交付输出。

本 change 的 apply instructions 已核对为 20/20、state=all_done，9条要求/30场景均已关联，严格规格及新文件/差异空白检查通过。主规格未同步、未归档。既有 .agents/ 与两个 .gitkeep 不属于本变更，不暂存；本次仅交付本 change 的源码、测试、规格和交接文档。
