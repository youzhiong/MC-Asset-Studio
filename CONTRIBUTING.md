# Contributing

感谢你愿意参与 MC Asset Studio。请先阅读 README 和 `docs/ARCHITECTURE.md`。

## 开发环境

- Windows 10/11 x64
- Visual Studio 2022 或 2026（Desktop C++）
- Qt 6.12 MSVC 2022 64-bit（Core / Gui / Widgets，含 private headers）
- CMake >= 3.24

```powershell
$env:QTDIR = "C:\Qt\6.12.0\msvc2022_64"
cmake --preset windows-msvc
cmake --build --preset release
```

## 提交前检查

1. Windows Release 构建通过。
2. 没有提交 `build/`、`dist/`、`data/`、Qt DLL、客户端 jar、模组 jar 或游戏素材。
3. 新增/修改的功能在真实项目上做过基本烟测。
4. 不引入与现有主题/QSS 冲突的内联样式；优先使用对象名和 `Theme::Colors`。
5. 不在日志和截图中包含个人路径、账号或未公开项目内容。

## Pull Request

- 一个 PR 聚焦一个主题，附简短说明和测试方式。
- UI 变化请说明主题（尤其 Windows 11 原生主题）下的表现。
- 新增文件请确认许可证兼容；不要加入来源不明的图片、字体或游戏资源。
- 提交代码即表示你同意以本项目的 MIT License 授权你的贡献。

## 报告问题

使用 GitHub Issues，附：

- MC Asset Studio 版本（窗口右下角或 About）
- Windows 版本
- 复现步骤
- 预期/实际结果
- 不含个人信息的日志

不要在 issue 中上传 Minecraft 客户端 jar、模组 jar 或完整资源包。

## 行为准则

参与本项目即表示同意遵守 [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)。
