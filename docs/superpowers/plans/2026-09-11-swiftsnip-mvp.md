# 瞬截 / SwiftSnip v1 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:executing-plans 逐任务实现此计划。
> 步骤使用复选框（`- [ ]`）语法来跟踪进度。执行方式：内联执行（用户已批准直接实施）。

**目标：** 交付 Windows 10 轻量截图工具，支持可自定义全局热键、鼠标选区截图、全屏截图，结果自动保存 PNG。

**架构：** 纯 Win32 C++17 程序，托盘常驻；`capture` 抓取虚拟屏幕位图，`overlay` 呈现遮罩并收集选区，`png_writer` 用 WIC 落盘，`settings` 以 INI 持久化，`settings_win` 提供设置界面。

**技术栈：** C++17 / Win32 / GDI / WIC / MSVC 14.44 + Windows SDK 10.0.26100，构建脚本 `build.bat`。

---

## 文件结构

| 文件 | 职责 |
| --- | --- |
| `build.bat` | vcvars64 + cl 编译，产出 `build\SwiftSnip.exe` |
| `src/app.h` | 应用常量、窗口类名、自定义消息、热键 ID |
| `src/dpi.cpp/.h` | PerMonitorV2 DPI 感知初始化 |
| `src/settings.cpp/.h` | INI 读写、热键字符串互转、默认保存目录 |
| `src/hotkey.cpp/.h` | RegisterHotKey 封装与冲突检测 |
| `src/tray.cpp/.h` | 托盘图标、菜单、气泡提示 |
| `src/capture.cpp/.h` | 虚拟屏幕/主屏捕获、裁剪、变暗副本 |
| `src/png_writer.cpp/.h` | WIC PNG 编码 |
| `src/overlay.cpp/.h` | 区域选择遮罩窗口 |
| `src/settings_win.cpp/.h` | 设置窗口（热键录制、目录选择、开关） |
| `src/autostart.cpp/.h` | HKCU Run 开机自启 |
| `src/main.cpp` | 入口、单实例、消息循环、流程编排、`--selftest` |
| `tests/verify_capture.py` | 用 PNG 解析校验自检产物（Python 标准库） |

## 任务 1：项目骨架与托盘常驻

**文件：** 创建 `build.bat`、`.gitignore`、`src/app.h`、`src/dpi.*`、`src/tray.*`、`src/main.cpp`（骨架版）

- [ ] 步骤 1：编写 `build.bat`（vcvars64 + `cl /std:c++17 /O2 /MT /W4`，链接 user32/gdi32/shell32/shlwapi/ole32/windowscodecs/comctl32/advapi32/msimg32）
- [ ] 步骤 2：编写 `app.h` 常量与 `dpi` 初始化，`main.cpp` 建隐藏主窗口 + 单实例互斥
- [ ] 步骤 3：实现 `tray`（NIM_ADD、右键菜单、气泡、NIM_DELETE）与退出流程
- [ ] 步骤 4：构建 `build.bat`，预期输出 `build\SwiftSnip.exe` 且无警告级错误
- [ ] 步骤 5：运行 exe，人工确认托盘图标出现、右键菜单可退出；重复启动不产生第二实例
- [ ] 步骤 6：Commit（`feat: 项目骨架与托盘常驻`）

## 任务 2：屏幕捕获与 PNG 保存

**文件：** 创建 `src/capture.*`、`src/png_writer.*`；修改 `src/main.cpp`（保存流程 + `--selftest`）

- [ ] 步骤 1：实现 `CaptureVirtualScreen` / `CapturePrimaryMonitor`（CreateCompatibleBitmap + BitBlt SRCCOPY|CAPTUREBLT，虚拟屏幕负坐标处理）
- [ ] 步骤 2：实现 `CaptureRect`（裁剪）与 `CreateDarkenedCopy`（AlphaBlend 40% 黑）
- [ ] 步骤 3：实现 `SaveBitmapAsPng`（WIC：流→编码器→帧→WriteSource→Commit）
- [ ] 步骤 4：实现保存流程 `HandleCapturedImage`（建目录→文件名→保存→气泡提示）与 `--selftest`（捕获主屏→保存到指定路径→打印尺寸→退出码 0）
- [ ] 步骤 5：构建并运行 `build\SwiftSnip.exe --selftest build\selftest.png`，预期退出码 0
- [ ] 步骤 6：运行 `python tests/verify_capture.py build\selftest.png`，预期输出 `OK` 且尺寸与主屏一致
- [ ] 步骤 7：Commit（`feat: 屏幕捕获与 PNG 保存`）

## 任务 3：全局热键与全屏截图

**文件：** 创建 `src/hotkey.*`；修改 `src/main.cpp`、`src/settings.*`（本任务先落地默认热键读取）

- [ ] 步骤 1：实现 `RegisterAppHotkeys` / `UnregisterAppHotkeys`（MOD_NOREPEAT，区域=1、全屏=2）
- [ ] 步骤 2：实现冲突检测 `IsHotkeyAvailable`（临时注册再注销）
- [ ] 步骤 3：`main.cpp` 处理 `WM_HOTKEY`：区域→（暂用全屏直存占位，任务 4 替换）、全屏→按 `FullscreenScope` 捕获并保存
- [ ] 步骤 4：构建；运行 `--check-hotkeys` 预期打印 `region=OK fullscreen=OK`
- [ ] 步骤 5：人工按 `Ctrl+Alt+F`，确认生成 PNG 且有气泡提示
- [ ] 步骤 6：Commit（`feat: 全局热键与全屏截图`）

## 任务 4：区域选择遮罩窗口

**文件：** 创建 `src/overlay.*`；修改 `src/main.cpp`

- [ ] 步骤 1：实现遮罩窗口创建（覆盖虚拟屏幕、置顶、十字光标、捕获鼠标与键盘焦点）
- [ ] 步骤 2：实现双缓冲局部重绘（暗底图 + 选区亮图 + 边框 + 8 手柄 + 尺寸标签）
- [ ] 步骤 3：实现拖拽状态机（新建选区、移动、8 向缩放、最小 3×3）
- [ ] 步骤 4：实现确认（Enter/双击）与取消（Esc/右键），确认回调返回裁剪位图
- [ ] 步骤 5：构建；人工拖拽选区后回车，确认生成 PNG 尺寸与选区标签一致；Esc 取消无文件产生
- [ ] 步骤 6：Commit（`feat: 区域选择遮罩窗口`）

## 任务 5：设置窗口与配置持久化

**文件：** 创建 `src/settings_win.*`、`src/autostart.*`；修改 `src/settings.*`、`src/main.cpp`、`src/tray.*`

- [ ] 步骤 1：完成 `settings` INI 读写（热键、保存目录、全屏范围、自启）与默认值（`<exe目录>\Pictures`）
- [ ] 步骤 2：实现 `autostart`（HKCU Run 写入/删除/查询）
- [ ] 步骤 3：实现设置窗口控件与布局（热键录制按钮、目录编辑+浏览、全屏范围组合框、自启复选框、保存/取消/打开目录）
- [ ] 步骤 4：热键录制（子类化按钮捕获 WM_KEYDOWN/组合键，显示 `Ctrl+Alt+A` 文本）与试注册校验
- [ ] 步骤 5：保存后热键立即重新注册；失败时提示并保留旧值
- [ ] 步骤 6：构建；人工修改热键与目录，重启 exe 验证配置保持，新截图落入新目录
- [ ] 步骤 7：Commit（`feat: 设置窗口与配置持久化`）

## 任务 6：图标、文档与验收

**文件：** 创建 `res/app.rc`、`res/swiftsnip.ico`、`README.md`；修改 `build.bat`、`src/tray.cpp`

- [ ] 步骤 1：生成应用图标并转 `.ico`（16/32/48/256），资源脚本编入 exe，托盘与 exe 图标统一
- [ ] 步骤 2：编写 `README.md`（功能、热键、目录结构、构建方法、已知限制）
- [ ] 步骤 3：按设计规格第 10 节逐条人工验收，记录结果
- [ ] 步骤 4：测量体积/内存/启动耗时，写入 README 验收表
- [ ] 步骤 5：Commit（`docs: 图标、README 与验收记录`）

## 自检

- 规格覆盖：热键自定义（任务 3/5）、区域截图（任务 4）、全屏截图（任务 3）、
  GUI（任务 1/5）、PNG 保存与自定义目录（任务 2/5）、性能与轻量（任务 2/6）均有对应任务。
- 类型一致性：`CapturedImage`、`AppSettings`、`HotkeyConfig` 在任务 2/4/5 中保持同名同义。
- 占位符：无 TODO/待定项；每个任务以可运行产物或可执行验证结束。
