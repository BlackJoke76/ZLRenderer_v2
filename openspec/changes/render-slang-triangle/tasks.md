# Tasks

## 1. Slang 构建与部署

- [x] 1.1 编写固定vertexMain/fragmentMain的最小Slang源码；用本机slangc编译SPIR-V 1.0并以spirv-val --target-env vulkan1.0、spirv-dis核对stage/入口/能力，保存版本与完整命令，失败按暂停条件报告。
- [x] 1.2 接入可覆盖工具路径、明确参数及按配置隔离的CMake OUTPUT/DEPENDS规则，共享产品/测试目标编译和部署；验证全新Debug/Release、源码修改触发编译、无修改不重编译，以及无效工具路径和错误源码非零失败。
- [x] 1.3 实现exe相对着色器路径及最小SPIR-V读取校验，增加缺失/空/非法大小或头部失败测试；从仓库外cwd运行验证，并更新README工具版本、依赖、构建及部署说明。

## 2. 内容选择与绘制资源

- [x] 2.1 增加默认clearOnly的配置枚举并沿现有初始化链传递，示例main选择triangle；测试既有默认/替代色调用仍clearOnly，且无shader文件时clearOnly正常；记录接口用法。
- [x] 2.2 复用候选代创建/释放流程，加入局部shader module RAII及每代pipeline/layout所有权；测试初建和重建的module、layout、pipeline部分失败，核对已创建对象释放一次、旧链不恢复acquire，更新ARCHITECTURE生命周期说明。
- [x] 2.3 将recordClearFrame调整为共享recordFrame，在triangle模式设置动态viewport/scissor、绑定兼容pipeline并draw三顶点；验证实际提交/画面、入口参数一致、当前代资源归属，稳定帧无新增pipeline创建或WaitIdle。

## 3. 可重复画面与窗口验收

- [x] 3.1 扩展现有verifyWindow共享捕获流程，以明确内容选项保留clear-only断言，增加独立重心插值、多内部点和外部/边缘背景采样；保存坐标、编码、容差、截图及结果，验证默认/替代背景并记录脚本命令。
- [x] 3.2 扩展测试目标三角形案例及逐代资源诊断，覆盖extent重建与失败边界；验证格式/图像数量变化的合成或代码证据，并明确实际覆盖性质，产品不包含注入开关。
- [x] 3.3 Debug/Release各执行至少10秒稳定显示、快速缩放、至少三轮最小化恢复及可见/最小化关闭；验证每个稳定尺寸三角形/背景、暂停计数冻结、恢复帧推进、5秒内退出0、资源平衡和validation/sync无应用WARNING/ERROR。

## 4. 集成与交接

- [x] 4.1 独立构建目录全新Debug/Release构建并运行全部CTest及clear-only窗口回归，确认MAILBOX优先/FIFO回退测试与实际模式记录；执行可用ASan/静态分析并如实记录UBSan等环境限制，不扩大已知环境诊断过滤。
- [ ] 4.2 完成verification.md要求/场景到证据映射，运行OpenSpec严格校验与git diff --check，更新STATUS/ROADMAP实际结果；全部验收通过后按AGENTS核对目标、独立提交本change并推送，保存SHA和远程核对结果。

## Workflow follow-up

- 用户新请求后执行openspec-verify-change独立复核。
- 用户请求归档时同步主规格并验证归档记录；不在本提案中提前归档。
