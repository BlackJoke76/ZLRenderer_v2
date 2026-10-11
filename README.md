# ZLRenderer

Windows 上的 C++20 / GLFW 工程。目前打开一个可调整大小的空窗口，处理事件并正常关闭。

## 前置条件

- Windows 10/11 x64，有可交互桌面。
- Visual Studio 2022，安装“使用 C++ 的桌面开发”、MSVC x64 工具链、Windows SDK 和 C++ CMake 工具。
- CMake 3.24 或更新版本、Git 可用。首次配置需要访问 GitHub 下载 GLFW。
- 使用 PowerShell，当前目录为项目根目录。源码路径包含空格或单引号时仍需正常引用。

## 配置、编译与运行

打开 Visual Studio 的 Developer PowerShell，确认 `cmake --version` 和 `git --version` 成功，执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
& .\build\Debug\zlRenderer.exe
```

若普通 PowerShell 中 CMake 未加入 PATH，可以在当前终端使用 VS 自带版本（默认安装路径）：

```powershell
$env:Path = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;" + $env:Path
```

配置使用固定的 GLFW 3.4 提交 `a74efa0d5628b74adc0426af4c5710e287fa7c2c`，源码下载到 `build/_deps`，无需手动安装 GLFW。

运行后应出现标题为 `ZLRenderer`、初始客户区 1280×720 的空窗口。移动、调整大小后点击系统关闭按钮；进程退出且 `$LASTEXITCODE` 为 `0`。当前没有渲染内容，空闲时等待事件。

Release 使用同一个配置目录：

```powershell
cmake --build build --config Release --parallel
& .\build\Release\zlRenderer.exe
```

要验证从头构建，使用新的目录（例如将以上 `build` 替换为 `build-clean`），避免依赖已有构建缓存。初始化或创建窗口失败会向标准错误输出原因并返回非零退出码。

## 项目文档

- [路线图](docs/ROADMAP.md)
- [实际进度与验证](docs/STATUS.md)
- [当前架构](docs/ARCHITECTURE.md)
- [开发规则](AGENTS.md)

OpenSpec 变更记录位于 `openspec/changes`；验收通过后按项目规则独立提交并推送。
