# Design

## Context

动机和切片契约见 proposal.md。当前 RhiConfig 只有 clearColor，Rhi::renderFrame 经后端调用 VulkanPresenter；recordClearFrame 在现有单颜色 render pass 中仅 begin/end。SwapchainResources 已独占每代 render pass/framebuffer，候选代完整后切换；固定两个 FrameResources 复用，重建等待旧提交并解除命令引用。CMake 3.24/C++20，MSVC /W4 /WX，产品与带 ZL_RENDERER_TESTING 的生命周期目标共用 rendererSources。已有50项CTest及Windows客户区截图脚本，脚本现有25点全背景假设需按内容区分。

已观察本机 `C:/VulkanSDK/1.4.341.1/Bin/slangc.exe -version` 为 `2026.1-52-gc8ddf20bb`；尚未编译本change shader或创建pipeline。当前应用声明Vulkan 1.0，不能因硬件支持较新版本就扩大基线。

## Goals / Non-Goals

Goals：用固定draw接通构建产物、入口、布局、pipeline、命令到画面；复用现有获取/提交/呈现与重建控制，保持clear-only回归。所有名字遵循lowerCamelCase及角色句柄约定。

Non-Goals：见proposal；不建立通用shader管理器、反射、资源注册表或pipeline数据库。无额外buffer读写，唯一GPU输出是当前swapchain颜色附件。

## Decisions

### 1. 构建期离线编译和可执行文件相对部署

CMake cache路径允许显式指定slangc，否则从SDK/PATH发现，配置输出实际路径及版本。为固定源码的vertexMain/fragmentMain分别设置entry、stage、SPIR-V目标，OUTPUT/DEPENDS及VERBATIM管理重编译；生成目录按配置隔离，应用及生命周期测试目标复用一个构建规则，部署到各自exe旁shaders目录，启动路径从exe位置解析，不依赖cwd。

选择兼容Vulkan 1.0的SPIR-V 1.0；实施第一项必须用本机工具编译实际最小入口，并以spirv-val --target-env vulkan1.0和spirv-dis核对版本、stage、入口及能力。优先使用Slang提供的GLSL中间输出后端（本机help确认选项）规避直接SPIR-V早期版本实验支持；若验证仍失败则触发暂停条件，不升级Vulkan或偷用新能力。此步骤是具体兼容性门槛，不将规划视为已通过编译。

产物入口名与pipeline pName必须一致，以反汇编验证，不能假定源函数名必然保留。发布运行不依赖Slang DLL。未选运行时Slang API，以减少依赖和启动状态；未选提交预编译二进制，以确保源码修改可追踪。

### 2. 显式内容选项维持原清屏契约

RhiConfig增加小枚举内容选项，默认clearOnly；main明确选择triangle；沿现有initialize参数传递，不新增另一套presenter或帧循环。测试目标的既有案例显式选clearOnly，新增triangle案例选择绘制。共享recordFrame始终CLEAR背景，仅在triangle模式绑定pipeline并draw(3,1,0,0)。clearOnly不加载shader或创建pipeline，缺少三角形资产不影响其契约。

### 3. 固定顶点与最少pipeline状态

Slang顶点入口使用SV_VulkanVertexID产生三顶点与RGB，片元入口插值输出alpha1。约定正height Vulkan viewport和明确NDC顶点/截图朝向，禁用culling避免初次演示绕序误判；空vertex input、triangle list、无depth/blend/MSAA，空descriptor/push-constant pipeline layout。动态viewport/scissor每帧由当前extent设置。无需buffer上传、descriptor或额外barrier；沿用颜色附件写入到present同步，不增加submit或稳定帧WaitIdle。

### 4. 绘制资源随交换链代完整发布

triangle模式下pipeline/layout由SwapchainResources独占，在候选render pass存在后创建。shader module只用于pipeline创建，局部RAII释放，任一步失败都不泄漏先前对象。每代重建pipeline作为本切片简单策略，保证格式/render pass兼容；动态extent不意味着必须引入跨代缓存。创建只发生于初建/必要重建，稳定帧不创建。

复用createSwapchain/releaseSwapchain及已存在候选回滚路径；GPU等待、命令池引用解除后按pipeline→layout→render pass等依赖顺序释放。创建新链后的失败沿已退役语义退出，不恢复旧链acquire。未选持久跨代pipeline cache，因为当前只有一个固定pipeline且代边界已明确；不为它扩大所有权体系。

### 5. 验收共享窗口流程，采样按内容区分

复用verifyWindow的DPI正确客户区捕获及窗口转换，不复制Win32驱动脚本；增加明确内容参数，保持原clear-only断言。triangle从约定NDC顶点算屏幕区域，在远离边缘/顶点的多个内部位置检查重心插值RGB（按实际sRGB或UNORM编码），四周和客户端边缘核对背景。截图与采样坐标/期望/实际/容差一并保存，容差需记录依据而不以放宽阈值掩盖错误；CPU期望独立计算。记录每代pipeline创建及释放，稳定尺寸计数不变，失败阶段只注入新绘制边界。

## Risks / Trade-offs

- Slang低版本SPIR-V兼容性 → 编译及spirv-val门槛优先，失败停止并带证据调整。
- 工具或shader部署路径含空格、多配置冲突 → VERBATIM、配置隔离和任意cwd启动验证。
- 入口或stage不匹配 → 构建期反汇编检查并对照实际pipeline参数。
- 颜色编码、Y方向和桌面遮挡导致误验 → 物理像素捕获、独立重心期望、远离边缘采样并保留原图。
- 每次重建重新创建pipeline有成本 → 当前接受，仅重建时发生；不作性能提升声明。
- WaitIdle不严格证明呈现引擎完成 → 原基础WSI限制保持，不以零validation错误声称扩展保证。

## Migration Plan

按tasks先通过工具兼容门槛，再构建部署、接入选项和代资源、更新验收。验收前可通过clearOnly保留原行为；回退实施提交同时撤销shader依赖与选项，不改已归档规格。实施后更新README工具命令、ARCHITECTURE所有权和STATUS/ROADMAP证据，再依项目规则提交推送；本提案阶段不改产品、不提交完成回执。

## References

- Slang官方编译指南：https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/08-compiling.html
- Slang官方SPIR-V说明（1.0至1.2直接后端支持为实验性质）：https://github.com/shader-slang/slang/blob/master/docs/user-guide/a2-01-spirv-target-specific.md
