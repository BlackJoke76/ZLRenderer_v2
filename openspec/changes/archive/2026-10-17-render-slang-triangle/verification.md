# render-slang-triangle 实施验收

## 范围与工具链

2026-10-17；基线 d5adb950dee00495af40f3581d88733d6eb87b21，Windows/MSVC 19.38，SDK 1.4.341.1。Slang `2026.1-52-gc8ddf20bb`，命令为 `slangc shaders/triangle.slang -entry <vertexMain|fragmentMain> -stage <vertex|fragment> -target spirv -profile spirv_1_0 -emit-spirv-via-glsl -o <output>`。两个模块经 `spirv-val --target-env vulkan1.0` 通过；spirv-dis确认版本1.0、Shader capability、对应Vertex/Fragment、入口main。SV_VertexID首次探测产生DrawParameters/1.3，改用直接VertexIndex语义SV_VulkanVertexID后通过；未升级Vulkan设备或feature。

CMake独立目录 `build-triangle`，沿用已固定的 `build-present-clean/_deps/glfw-src`，没有重新下载。Debug/Release全新构建无警告，各61/61 CTest通过；最终Debug37.25秒，Release35.58秒。后续错误源码探测恢复后重建通过。版本、反汇编、原始build/ctest日志及辅助脚本保存在该忽略目录。第一次配置误指向未保存GLFW源码的build-resize-verify，失败记录保留；纠正到实际源码目录后配置构建通过。

## 要求与场景映射

| 要求 | 场景 | 验证与结论 |
|---|---|---|
| Build and deploy Slang shaders | Clean and incremental builds | Debug/Release独立配置产物；verifyBuild.ps1检查无修改时间戳不变、修改后更新；build-dependency-evidence.txt通过 |
| 同上 | Tool or source failure | invalid-tool.log明确无效编译器路径；invalid-source-build.log非零失败，finally还原源码，restored-build.log通过 |
| 同上 | Launch outside the build directory | exe相对getShaderDirectory；CTest从配置上级启动；verifyIsolated.ps1从TEMP启动，missing-asset和isolated-clear原始日志核验 |
| Select triangle content through the rendering boundary | Demonstration application | main显式triangle，实际产品窗口截图和像素通过 |
| 同上 | Existing clear only caller | RhiConfig默认clearOnly；既有案例及两配置clear窗口通过；无shaders隔离测试exe退出0且不创建module/layout/pipeline |
| Display an interpolated color triangle | Sustained triangle output | 两配置至少10秒、多帧、25点内部/外部采样，按像素中心独立重心与sRGB编码计算；容差8显示字节，避开边界；保存每点期望/实际/坐标/格式及PNG，已目视确认首张三角形 |
| 同上 | Changed background | Release triangle-alternate-window通过，新背景不影响内部插值 |
| Preserve drawing across presentation transitions | Resize and repeated restore | 四个triangle窗口流程通过快速缩放、三轮暂停/恢复、稳定期代数和pipeline数量不变；暂停提交/呈现计数冻结、恢复推进 |
| 同上 | Generation compatibility | triangle-rebuild真替换且draw句柄在已发布当前代存活集合中；原extent选择、格式/图像数量合成选择测试及代码核查仍有效。格式/pipeline/render pass按新代一起创建；未宣称真实跨格式或分族硬件覆盖 |
| Fail and release drawing resources correctly | Invalid deployed shader | shader-file用实际文件覆盖缺失/空/非4倍数/坏头及合法头读取；隔离triangle进程缺文件返回1，含路径原因并清理 |
| 同上 | Partial pipeline creation failure | vertex-module、fragment-module、pipeline-layout、graphics-pipeline及对应rebuild四例，全是成功创建后的host checkpoint注入；核对阶段错误、退出1、创建释放次数、逐代所有权，不声称真实驱动创建失败；重建后旧链不再acquire沿原路径保持 |
| Validate triangle rendering and shutdown | Complete acceptance sequence | 下表窗口证据及61项回归，退出0、请求关闭5秒内返回；原始诊断保留，无未知应用validation/sync WARNING/ERROR |
| 同上 | Supported mode policy | swapchain-selection覆盖MAILBOX优先/FIFO回退及分族合成参数；本机RX7900XT同族0、FIFO、格式50、3图像/2帧槽，未称MAILBOX硬件实测 |

6条要求、13个场景均有上述实现及验证证据。

## 实际窗口结果

| 流程 | submitted/presented | 交换链代数 / pipeline创建释放 | build-triangle下证据 |
|---|---|---|---|
| Debug三角形、可见关闭 | 2392/2392 | 9 / 9对 | final-window-logs/Debug |
| Debug三角形、最小化关闭 | 2295/2295 | 8 / 8对 | minimized-close-logs/Debug |
| Release三角形、可见关闭 | 2345/2345 | 9 / 9对 | window-logs/Release |
| Release三角形、最小化关闭 | 2359/2359 | 9 / 9对 | minimized-close-logs/Release |
| Release三角形、替代背景 | 2203/2203 | 9 / 9对 | alternate-logs/Release |
| Debug仅清屏、替代背景回归 | 2178/2178 | 9 / 0对 | clear-logs/Debug |
| Release仅清屏、替代背景回归 | 2319/2319 | 9 / 0对 | clear-logs/Release |

调试窗口请求最终被系统合并时可能为8代，验收依据最后有效extent及稳定不重复创建，不要求无意义固定9代。早期Debug窗口通过2371/2371，作为中间记录保留，以最终代码窗口流程为准。原始图像均为物理客户区；脚本只置顶自身验证窗口，未改驱动、注册表或全局layer配置。产品二进制未发现ZL_VULKAN_TEST_CASE或准备阶段注入标识。

## 限制

仍保留基础WSI WaitIdle不能严格证明呈现引擎完成的约定，零诊断不能补足它。纯shaderFile测试的合法头fixture只验证加载，不宣称是完整有效shader；产品真实编译模块另经spirv-val与实际pipeline验证。没有runtime Slang依赖、额外buffer/descriptor、深度或跨代pipeline缓存。每个必要重建创建一个pipeline，未作性能提升声明。

## 附加验证与交付

`build-triangle-asan` 使用 `/EHsc /Zi /fsanitize=address`、链接 `/DEBUG`，Release构建无警告；61/61 CTest通过（194.62秒），实际三角形窗口2352/2352帧、9代，各资源创建释放平衡，三轮暂停恢复、缩放及退出0通过，无ASan报告。窗口证据在 `build-triangle-asan/window-logs/Release`，build/ctest原始汇总在 `build-triangle/asan-*.log`。ASan DLL路径只加入验证子进程PATH。C++代码插桩，外部GLFW C代码、driver和layers未插桩。

`build-triangle-analysis` 使用 `/EHsc /analyze`，全部C++目标构建无警告，原始汇总 `build-triangle/analysis-build.log`。未发现可用clang++/clang-cl，MSVC不提供UBSan；未运行或宣称UBSan通过。

OpenSpec严格校验及git diff --check通过。11/11任务完成，实施已提交并推送；不在此阶段同步或归档主规格。远程已核对为 `https://github.com/BlackJoke76/ZLRenderer_v2.git`、`main`、upstream `origin/main`，远程基线仍为d5adb950dee00495af40f3581d88733d6eb87b21。仅提交此change相关文件，保留既有未跟踪 `.agents/` 和两个 `.gitkeep`。实施提交 `f2ee745da61e64644333d658fc4225cdacdb6271` 已推送main，git ls-remote核对远程SHA一致。任务完成及交接回执在后续独立文档提交保存，最终回执SHA以Git历史和交付输出为准。首次暂存检查指出新增shader末尾多余空行，已在回执提交移除并重新核对diff检查；该修正只去除空白及UTF-8 BOM，不改变shader语义，未重复运行功能验收。

## 2026-10-17 OpenSpec 独立复核

复核对象：`11878bf2054e600e54a3130df8ef8f23eacc46d6`，schema `spec-driven`。重新读取status及apply返回的全部proposal/specs/design/tasks；CLI任务11/11，剩余0。对比基线d5adb95识别实施代码，并在 `build-triangle-verify/verification-inputs.json` 保存6条要求、13个场景、5项决策及实施源码哈希。复核期间实施文件哈希保持一致，未修产品或测试代码，未同步主规格或归档。

| 维度 | 检查结果 |
|---|---|
| Completeness | Task Completion 11/11；Spec Coverage 6/6，全部ADDED；无REMOVED/RENAMED |
| Correctness | Requirement Implementation Mapping 6/6；Scenario Coverage 13/13（实际/合成/注入/代码证据分别核对） |
| Coherence | Design Adherence 5/5；Code Pattern Consistency通过：lowerCamelCase/角色句柄、共享录制及候选代创建释放、局部RAII与原错误处理模式一致 |

适用检查全部完成，无Not verified或部分核查项。critical 0、warning 0、suggestion 0。结论：全部检查通过，可进入归档流程。

### 源码与设计核对

| 要求 | 当前源码证据 |
|---|---|
| Build and deploy Slang shaders | CMakeLists.txt:15-39、56、77：工具/版本、两stage明确入口、OUTPUT/DEPENDS、配置隔离及共享依赖；src/shaderFile.cpp:8-29：exe相对路径和uint32读取。新产物spirv-val Vulkan1.0通过，反汇编均1.0/Shader/main及正确stage；imports-Debug.txt无Slang DLL |
| Select triangle content through the rendering boundary | src/rhi.h:11-15默认clearOnly；src/main.cpp:49-57产品显式triangle、测试选择及背景；src/rhi.cpp:19-30配置沿原初始化链传递；clearOnly在src/vulkanPresenter.cpp:213、371避开资源加载和draw |
| Display an interpolated color triangle | shaders/triangle.slang:6-17：SV_VulkanVertexID、固定NDC与RGB、alpha1；src/vulkanPresenter.cpp:295-346无vertex input/depth/blend、空layout及动态状态；353-386同一render pass清屏后draw(3,1,0,0)；scripts/verifyWindow.ps1:92-137独立重心/编码多点验收 |
| Preserve drawing across presentation transitions | src/vulkanPresenter.cpp:158-214新配置render pass/pipeline，509-557等待旧提交/解除命令引用/复查窗口/候选完整后发布，371-378按当前extent动态设置；tests/verifyPresentation.cmake:205-225逐draw检查当前已发布代与存活pipeline |
| Fail and release drawing resources correctly | src/shaderFile.cpp:14-29明确文件及原因；src/vulkanPresenter.cpp:261-294局部module析构和候选所有权，560-604 pipeline/layout先于render pass释放；tests/verifyPresentation.cmake:196-203核对失败阶段；新两配置各8个绘制失败日志独立核对资源平衡、候选未发布、重建失败仍3帧/1代 |
| Validate triangle rendering and shutdown | scripts/verifyWindow.ps1:179-241稳定计数/三轮暂停恢复/5秒退出/资源平衡，原诊断归因保持；新窗口日志与61项CTest均通过，MAILBOX/FIFO选择测试仍在，实际硬件FIFO同族0不冒称MAILBOX或分族实测 |

5项设计决策逐项遵循：离线编译与exe相对资源；内容选项维持清屏；固定无buffer/descriptor三角形和动态viewport/scissor；逐代pipeline/layout及局部shader module；共享窗口驱动和独立颜色期望。没有新功能基线、额外submit、稳定帧pipeline创建或WaitIdle。

### 新运行结果

新目录从当前源码配置并构建Debug/Release，沿用已存在固定GLFW源码，不复用旧二进制；两配置无编译警告。`build-triangle-verify/ctest-Debug.log` 61/61（46.08秒），`ctest-Release.log` 61/61（42.55秒）；含shader-file、原50项回归及10项三角形/绘制失败测试。两配置8个module/layout/pipeline初建/重建失败stderr阶段与清理结果另行核对。产品二进制无测试环境选择器/准备阶段注入标识。

| 新实际窗口流程 | submitted/presented | 代数/pipeline创建释放 | build-triangle-verify下证据 |
|---|---|---|---|
| Debug可见关闭 | 2421/2421 | 9/9对 | window-logs/Debug |
| Debug最小化关闭 | 2347/2347 | 9/9对 | minimized-close-logs/Debug |
| Release可见关闭 | 2369/2369 | 9/9对 | window-logs/Release |
| Release最小化关闭 | 2372/2372 | 9/9对 | minimized-close-logs/Release |
| Release替代背景 | 2231/2231 | 9/9对 | alternate-logs/Release |

五流程均稳定至少10秒、快速缩放及三轮最小化恢复，逐尺寸25点采样与PNG保存，正常设备下请求关闭5秒内退出0；稳定窗口代数/pipeline创建计数不变，暂停提交/呈现冻结，恢复推进，资源平衡，无未知应用validation/sync WARNING/ERROR。另目视检查新Debug第三次恢复的实际客户区PNG，三角形方向、颜色及背景正常。保留原始AMD loader具体诊断，未修改过滤规则。

原实施阶段的增量/错误源码/无效工具日志已重新读取，确认修改触发编译、无修改不重编译、失败非零及恢复成功；仅查证这些历史证据，本轮没有修改shader制造编译错误。exe相对加载、隔离缺资源与无资源clearOnly日志和单元测试已核对；本轮CTest从exe配置目录之外启动并完成真实triangle/clearOnly路径。原Debug/Release实际clear-only窗口证据已核对，本轮未重跑长窗口clear-only流程。

历史ASan（61/61、194.62秒及2352帧窗口）及/analyze原始日志、构建flags已重新核对，本轮未重跑这两类构建；不将历史运行称为新运行。UBSan环境限制和基础WSI呈现完成限制仍明示，不以零诊断补足未启用的maintenance1/present fence。硬件仍RX7900XT、FIFO、format50、同族0、3图像/2帧槽；配置变化、MAILBOX与分族覆盖方式保持合成/代码核查。

OpenSpec严格校验与git diff --check通过。本轮仅更新verification.md与STATUS；复核记录按项目授权提交推送main，最终SHA见Git历史及交付输出。
