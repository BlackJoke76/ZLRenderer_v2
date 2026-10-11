# 实际进度

当前 change：`bootstrap-project`，本地功能验收通过，工程提交已推送 main；尚未归档。

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

上述临时验证脚本和产物在忽略的 build 目录内，不属于发布工程。

## 交接与限制

只实现无图形 API 的窗口；尚未实现 Vulkan、swapchain 或 MAILBOX 模式。
OpenSpec schema 读取在本机 Windows 沙箱内会因 realpath EPERM 失败，获准的沙箱外 CLI 调用正常。
GitHub 目标为 https://github.com/BlackJoke76/ZLRenderer_v2（PUBLIC），仓库已存在，复用并保留其 MIT LICENSE。
用户已授权接到远程 main 历史并改用 HTTPS；本地分支 main，origin 为该 HTTPS URL。原远程提交为 `20132ca888504f4e0f73b8dfe5369686340e6187`。
SSH 原因是 host key verification 失败；未更改用户 SSH 设置。
工程提交 `98a9a62f0b36bcf1cd12eaae0dae49274106351e` 已成功推送到 `origin/main`，远程 SHA 核对一致，upstream 为 `origin/main`。
随后用仅包含本 change 交付记录的文档提交同步任务完成状态；最终提交 SHA 以 Git 历史为准。
预先存在的 `.agents/` 本地技能文件保留，未纳入本 change 提交。无其他未提交的工程文件。
