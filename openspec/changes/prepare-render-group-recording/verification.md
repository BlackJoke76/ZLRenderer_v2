# prepare-render-group-recording 实施验收

## 实现与证据范围

- prepareGraphFrame 复用绑定/支持校验及用途映射，产生本帧值与借用句柄快照；recordRenderGroup 是 static，只消费完整组输入；submitGraphFrame 负责按序数组及单次 vkQueueSubmit。每槽一个 pool 和固定 primary 数组，重建复位全部 pool 后释放旧附件。
- 单/双清屏共享生产编译、准备、录制与提交路径。组入口/出口保留原转换：首组获取入口、第二组同布局 WAW、最终组唯一 present 出口。未引入 secondary、多线程、额外提交或图像分配。
- 先完成录制和所有可能分配内存的提交诊断文本，再 reset fence 与 submit；成功之后返回字符串移动并推进 hasSubmittedWork/槽/计数。后组失败无部分提交，已提交帧后的失败仍等待旧工作。

## 自动化

原始证据根目录：`build-render-graph-verify/`（忽略产物）。

| 层次 | 覆盖与定位 |
|---|---|
| CPU 图依赖 | 原有 declarations/references/shapes/exits/RAW/WAR/WAW/readers/contents 测试保持 |
| 生产准备/映射 | graph-mapping/bindings/recording；准备快照核对 slot=0/image=2/generation=3、组转换分布与真实借用句柄；缺失/旧代/缺少 render pass/不支持采样拒绝 |
| 真实调用证据 | checkRenderGraph.cmake 同时检查 barrier/附件参数、primary begin/end 完整范围、两个不同句柄、VkSubmitInfo count/有序数组、同槽跨代集合稳定；graph-primary-evidence 从真实双清屏采集输入，五类损坏记录均拒绝 |
| 失败控制 | group-begin/body/end 前组已完成、后组失败，0提交/呈现且无 fence reset；reset-fence 是调用前 host 检查点；submit/submit-later 是实际 API 前替代结果，后者保留前三帧并等待旧工作；均明确不声称自然驱动失败 |
| 重建与跳帧 | double-rebuild 真实三代；double-rebuild-failure 第二代 framebuffer 创建后注入并清理两代；primary-allocation 是创建 pool 后、分配 primary 前检查点；NOT_READY 为获取前 host 结果控制；原有窗口/surface/OUT_OF_DATE/SUBOPTIMAL 与退役处理回归 |

Debug、Release 最终各 78/78 CTest；日志分别为 group-debug-tests.log（45.46秒）、group-release-tests.log（43.37秒）。无编译警告，严格 change 校验和 git diff --check 通过。ASan 与 /analyze 配置沿用项目独立目录，构建日志保存为 group-asan-build.log / group-analysis-build.log。运行结果与窗口证据见下方。

## 规格映射

- Separate logical passes / Validate compiler and clear execution：原图 CPU 测试与真实单/双清屏参数、窗口共同覆盖，当前仍一 pass 一组。
- Prepare frame execution：CPU 准备测试及生产 acquire 后准备调用；错误绑定/采样在录制前拒绝。
- Record complete physical groups：primary begin/end 范围检查、组前后 barrier、双清屏 GPU 和后组 begin/body/end 控制。
- Submit all recorded groups：实际单个 VkSubmitInfo 数组、部分录制失败、submit 与 submit-later、SUBOPTIMAL 回归。
- Reuse group primaries：按 slot 集合身份和跨代检查，双组重建、旧附件所有权检查、跳帧及暂停恢复。
- Expose actual recording identity：实际调用输入有界记录及五类反例，失败日志带组索引和检查点阶段，提交失败不发布成功诊断。

## 限制

真实硬件及模式以本轮原始窗口日志为准；MAILBOX 优先/FIFO 回退的纯选择测试与实际 FIFO 不混淆，分族和跨代格式变化仍只按既有合成/代码证据报告。采样绘制仍拒绝，CPU 支持模型不等于 GPU 绘制覆盖。仅保留既有两条 AMD loader 环境诊断的精确归因，未放宽应用诊断过滤。基础 WSI WaitIdle 仅保证已提交 GPU 工作完成，不证明呈现引擎完成；本 change 不引入扩展保证。MSVC 环境未提供 UBSan，未声称运行。

## ASan 与静态分析结果

`build-render-graph-asan` Release 78/78 CTest（211.67秒），无 ASan 报告；DLL 路径仅设置于验证进程。`build-render-graph-analysis` Release 全部六个 C++ 目标 /analyze 构建无警告。构建与测试日志在上述证据根目录。原有 UBSan 工具限制保持。

## 真实窗口结果

Windows PowerShell 执行现有 scripts/verifyWindow.ps1；PowerShell 7 的 System.Drawing Add-Type 引用不兼容，改用系统 Windows PowerShell 后八流程全部通过，未修改脚本或产品兼容性范围。每个流程至少10秒无输入持续呈现，多次尺寸变化、三轮最小化恢复、25点内部/边缘颜色、暂停计数冻结、恢复推进和5秒内退出0均通过；自有对象创建/释放平衡，无应用 validation/sync WARNING/ERROR。双清屏最终 RGB=(63,89,124)，替代色使用对应预期值。原始 PNG/日志/verification.txt 在 build-render-graph-verify/group-window/<配置>/<流程>/。

| 窗口流程 | submitted / presented | 交换链代数 |
|---|---|---|
| Debug / alternate | 2099 / 2099 | 9 |
| Debug / default | 2274 / 2274 | 9 |
| Debug / double | 1462 / 1462 | 9 |
| Debug / minimized | 2318 / 2318 | 9 |
| Release / alternate | 2110 / 2110 | 9 |
| Release / default | 2227 / 2227 | 9 |
| Release / double | 1586 / 1586 | 9 |
| Release / minimized | 2300 / 2300 | 9 |

本轮真实设备为 AMD Radeon RX 7900 XT，graphics/present family=0，FIFO、format=50、3张图像、2帧槽。双组各槽分配2个PRIMARY，稳定帧及重建后保持身份集合。
