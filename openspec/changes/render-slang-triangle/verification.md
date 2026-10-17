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

OpenSpec严格校验及git diff --check通过。当前10/11任务完成，仅Git交付回执待完成；不在此阶段同步或归档主规格。远程已核对为 `https://github.com/BlackJoke76/ZLRenderer_v2.git`、`main`、upstream `origin/main`，远程基线仍为d5adb950dee00495af40f3581d88733d6eb87b21。仅提交此change相关文件，保留既有未跟踪 `.agents/` 和两个 `.gitkeep`。最终提交及推送回执在后续记录及Git输出。
