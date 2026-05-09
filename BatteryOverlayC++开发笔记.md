# Battery Overlay C++ 开发笔记

## 项目文件说明

| 文件路径 | 说明 |
|---------|------|
| `main.cpp` | 核心源码（约 1090 行），包含窗口创建、渲染、输入法检测、配置管理、对话框等全部逻辑 |
| `CMakeLists.txt` | CMake 构建配置，C++17 + MSVC，静态链接 CRT (`/MT`)，启用 `/utf-8` 解决中文乱码 |
| `icon.ico` / `icon.png` | 应用图标资源 |
| `res/resource.h` | RC 资源头文件，定义 `IDI_ICON` (101) 和 `VS_VERSION_INFO` (1) |
| `res/app.rc` | RC 资源脚本，包含图标、manifest 嵌入、VERSIONINFO（文件版本、公司名等） |
| `res/app.manifest` | Windows 应用程序清单，声明 DPI 感知（PerMonitorV2）和 UAC 级别 |
| `.github/workflows/build.yml` | GitHub Actions CI 配置，Visual Studio 2022 + MSVC + CMake 构建 |

---

## 编译踩坑记录

### 1. RC2104: VERSIONINFO 常量未定义

**问题：** RC 编译器报错 `VS_FFI_FILEFLAGSMASK`、`VOS_NT_WINDOWS32`、`VFT_APP`、`VFT2_UNKNOWN`、`VS_FF_DEBUG` 未定义。

**原因：** 这些常量在 SDK 头文件中的定义方式 RC 编译器无法识别。

**解决：** 直接使用数值常量替代：

```rc
FILEFLAGSMASK 0x3FL      // 替代 VS_FFI_FILEFLAGSMASK
FILEOS 0x40004           // 替代 VOS_NT_WINDOWS32
FILETYPE 0x1             // 替代 VFT_APP
FILESUBTYPE 0x0          // 替代 VFT2_UNKNOWN
FILEFLAGS 0x0L           // 替代 VS_FF_DEBUG
```

### 2. C2059: EM_SETSEL 重定义

**问题：** MSVC 编译报错 `syntax error: ')'`，定位到 `EM_SETSEL` 附近。

**原因：** `winuser.h` 已定义 `EM_SETSEL`，代码中又重复声明造成冲突。

**解决：** 删除手动 `constexpr UINT EM_SETSEL` 声明，直接使用 Win32 API 已定义的常量。

### 3. std::max / std::min 宏冲突

**问题：** Windows.h 定义了 `max`/`min` 宏，与 `std::max`/`std::min` 模板冲突。

**解决：** 在包含 windows.h 之前定义：

```cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
```

### 4. 源文件编码导致中文乱码

**问题：** 中文字符串（如"关于"、"退出"等）在程序中显示为乱码。

**解决：** CMakeLists.txt 添加 `/utf-8` 编译选项：

```cmake
target_compile_options(battery_overlay PRIVATE /utf-8)
```

### 5. C2059: 括号语法错误

**问题：** 编译报错 `syntax error: ')'`，位于 DrawTextW 调用处。

**原因：** 代码中存在重复的 `DrawTextW` 调用行，注释行被误删但调用残留。

**解决：** 清理重复代码行。

### 6. GET_X_LPARAM / GET_Y_LPARAM 未定义

**问题：** `GET_X_LPARAM` 和 `GET_Y_LPARAM` 宏在某些 SDK 版本中不可用。

**解决：** 改用：

```cpp
DWORD pos = GetMessagePos();
mp.x = (int)(short)LOWORD(pos);
mp.y = (int)(short)HIWORD(pos);
```

---

## 代码修改踩坑记录

### 1. 拖动窗口时漂移或消失

**问题：** 用 `GetCursorPos` 获取鼠标位置拖动窗口时，窗口会持续自动飘走或消失。

**原因：** `GetCursorPos` 返回的是调用时的即时鼠标位置，在消息队列中有延迟；拖动时需要用 `GetMessagePos` 获取消息触发时的鼠标位置。

**解决：** 点击时记录初始鼠标位置 + 窗口位置，拖动中用 `GetMessagePos` 差值计算新窗口位置。

### 2. IME 输入法状态检测不准确

**问题：** 跨进程获取前台窗口输入法状态时，`ImmGetDefaultIMEWnd` 返回 null 或获取的转换模式不准确；Caps Lock 状态检测也不稳定。

**原因：** 输入法 IME 窗口跨进程通信本身就不稳定；Caps Lock 检测错误地使用了 key-down 位 (0x8000) 而非 toggle 位 (0x0001)。

**解决：**
- `GetKeyState(VK_CAPITAL) & 0x0001` 检测 Caps Lock 切换状态（bit0 为 toggle 状态）
- `GetKeyboardLayout(0)` 获取当前线程键盘布局
- `ImmGetDefaultIMEWnd(GetForegroundWindow())` 跨进程获取 IME 窗口
- `GetKeyboardLayoutList` 枚举键盘布局列表作为备选方案

### 3. 行间距过大

**问题：** 上下两行文字间距过大，没有充分利用窗口高度。

**原因：** `DrawText` 的 `DT_VCENTER` 配合 `DT_SINGLELINE` 会产生额外垂直居中效果；字号偏小；两行矩形有重叠但没有正确利用。

**解决：**
- 字号从 `h/2 - 2` 增大到 `h/2`（最大化利用空间）
- 上下两行各占半窗口，第一行 `{0, 0, w, h/2}`，第二行 `{0, h/2, w, h}`
- 使用 `DT_VCENTER` 让每行在各自半区垂直居中

### 4. Caps Lock 状态只闪现一下

**问题：** 按 Caps Lock 键时 "EN" 只闪现一下就变回 "en" 或 "中"。

**原因：** 错误使用了 `GetKeyState(VK_CAPITAL) & 0x8000`（key-down 位，仅在按键按下瞬间为 1）。

**解决：** 使用 `GetKeyState(VK_CAPITAL) & 0x0001`（toggle 位，1=CapsLock开, 0=CapsLock关）。

### 5. 显示文本格式问题

**问题：** "eng" 应为 "en"，"ENG" 应为 "EN"；输入法状态与 ⚡ 符号之间有多余空格。

**解决：**
- `getInputLang()` 返回值直接使用 `L"EN"`、`L"en"`、`L"中"`
- 连接字符串时直接拼接：`lang + L"\u26A1"`（无空格）

### 6. WM_TIMER 响应延迟

**问题：** 输入法状态切换后，悬浮窗更新有明显延迟。

**原因：** 定时器间隔设为 200ms，响应不够灵敏。

**解决：** 将 `SetTimer(hwnd, 1, 200, nullptr)` 改为 `SetTimer(hwnd, 1, 100, nullptr)`。

---

### 10. 自定义背景透明度无效

**问题：** 在右键菜单设置背景透明度后，背景透明度没有变化，始终全透明或全不透明。

**原因：** `render` 函数中 Alpha 通道处理的颜色比较顺序错误。32bpp BI_RGB DIB 像素格式为 **BGRA**（B 在最低地址），但代码按 `px[i4]==br && px[i4+1]==bg_ && px[i4+2]==bb`（RGB 顺序）比较，导致背景像素的 Alpha 值永远不匹配，`ba` 设置无效。

**解决：** 将颜色比较改为正确的 BGR 顺序：
`px[i4] == bb && px[i4+1] == bg_ && px[i4+2] == br`

---

## 关键 API 参考

| 功能 | API |
|-----|-----|
| 分层窗口每像素 Alpha 渲染 | `UpdateLayeredWindow` + 32bpp DIB + `BLENDFUNCTION` |
| 跨进程 IME 状态 | `ImmGetDefaultIMEWnd` + `WM_IME_CTRL` + `IMC_GETCONVERSIONMODE` |
| Caps Lock toggle 状态 | `GetKeyState(VK_CAPITAL) & 0x0001` |
| 拖动时鼠标位置 | `GetMessagePos()` 而非 `GetCursorPos()` |
| 中文乱码解决 | `/utf-8` 编译选项 + `L"中文字符串"` 前缀 |
| 窗口不抢焦点 | `WS_EX_NOACTIVATE` + `MA_NOACTIVATE` 返回值 |

---

## 新增功能

### 1. 单击切换显示时间模式

**功能描述：** 单击悬浮窗（不是拖拽）可以在两种显示模式间切换：
- 正常模式：第一行输入法状态 + 充电标识，第二行电池百分比
- 时间模式：第一行 `hh:mm`（小时:分钟），第二行实时秒数

**实现原理：**
- 在 `WM_LBUTTONDOWN` 时记录鼠标起始位置
- 在 `WM_LBUTTONUP` 时判断鼠标移动距离是否小于 3 像素
- 移动距离 < 3 像素认为是单击，切换 `showTime` 状态
- `WM_CAPTURECHANGED` 中也做同样处理（防止窗口外释放鼠标时漏判）

**关键代码：**
```cpp
// 判断是否是单击
int dx = pt.x - AppState::clickStartX.load();
int dy = pt.y - AppState::clickStartY.load();
int moveDist = dx * dx + dy * dy;
bool wasClick = (moveDist < 9);  // 3^2 = 9
```

**相关状态变量：**
- `AppState::showTime` - 显示模式切换标志
- `AppState::clickStartX/clickStartY` - 单击起始位置

### 7. 输入法状态检测不准确（getInputLang 逻辑错误）

**问题：** 输入法状态"中"/"en"有时检测不正确，且原解决方案本身有错误。

**原因：**
- `GetKeyboardLayout(0)` 读的是悬浮窗自己线程的键盘布局，永远不会跟随用户切换输入法
- fallback 分支用 `GetKeyboardLayoutList` 判断，那是"系统是否装过中文输入法"，不是"当前是否正在用中文输入"，逻辑完全错误
- 缺少 `IMC_GETOPENSTATUS` 检查，IME 关闭时应返回 "en"
- 原笔记第 2 条的解决方案有误，需更正

**解决：**
- 改用前台窗口线程的 HKL：`GetWindowThreadProcessId` + `GetKeyboardLayout(fgThreadId)`
- 新增 `IMC_GETOPENSTATUS` 检查，IME 关闭时直接返回 "en"
- fallback 分支改为保守返回 "en"（不再用 `GetKeyboardLayoutList`）
- 更正原笔记第 2 条的解决方法（见下方更新）

### 8. RGB 颜色 R/B 分量互换

**问题：** 自定义背景颜色和字体颜色时，R 和 B 值实际效果互换，配置文件也一样。

**原因：** `RGB` 宏参数是 `(r, g, b)`，但代码中写成了 `RGB(fb, fg, fr)` 和 `RGB(bb, bg, br)`，顺序错误。

**解决：**
- `fontColorRef()`：`RGB(fb, fg, fr)` → `RGB(fr, fg, fb)`
- `bgColorRef()`：`RGB(bb, bg, br)` → `RGB(br, bg, bb)`

### 9. CapsLock 检测导致"EN"只闪现一下

**问题：** 大写锁定时应该持续显示"EN"，但实际是每次按下大写锁定键时只闪现一下。

**原因：** 误用 `GetAsyncKeyState(VK_CAPITAL)`，其 bit0 表示"自上次调用以来是否按过"，不是切换状态。

**解决：** 改回 `GetKeyState(VK_CAPITAL) & 0x0001`，定时器在主线程，读取正确。

---

## 更正历史记录

### 第 2 条（IME 输入法状态检测）解决方法更正

~~原记录（错误）：~~
- `GetKeyState(VK_CAPITAL) & 0x0001` 检测 Caps Lock 切换状态
- `GetKeyboardLayout(0)` 获取当前线程键盘布局
- `ImmGetDefaultIMEWnd(GetForegroundWindow())` 跨进程获取 IME 窗口
- `GetKeyboardLayoutList` 枚举键盘布局列表作为备选方案

**更正后（正确）：**
- `GetKeyState(VK_CAPITAL) & 0x0001` 检测 Caps Lock 切换状态（定时器在主线程，读取正确）
- `GetWindowThreadProcessId` + `GetKeyboardLayout(fgThreadId)` 获取**前台窗口线程**的键盘布局
- `ImmGetDefaultIMEWnd(GetForegroundWindow())` 获取 IME 窗口后，先检查 `IMC_GETOPENSTATUS`
- IME 无法获取时保守返回 "en"，**不使用** `GetKeyboardLayoutList` 作为备选（逻辑错误）

### 11. 透明度为 0 时程序无响应（无法拖拽、右键菜单不弹出）

**问题：** 在右键菜单将背景透明度设置为 0 后，窗口无法拖拽，右键点击也无菜单弹出，表现为"卡死"。

**原因：** `ba=0` 时，背景像素的 Alpha 值为 0（完全透明）。Windows 对分层窗口（WS_EX_LAYERED + 每像素 Alpha）**只对 Alpha > 0 的像素发送鼠标消息**。背景全透明后，只有文字像素（Alpha=255）能接收鼠标消息，但文字区域很小，很难点中，表现为窗口无响应。

**修复：**
1. `render()` 中背景 Alpha 使用 `std::max(ba, (BYTE)1)`，确保背景始终有至少 1 的 Alpha 值
2. `setAlpha()` 中存储 `std::max(a, (BYTE)1)`，拒绝 `ba=0`
3. `loadConfig()` 中 `ba` 下限改为 1，防止从配置文件加载到 0
4. `parseAlpha()` 中拒绝 0，有效范围改为 1-255
5. 透明度输入框提示和错误提示同步更新为 1-255

**经验：** Windows 分层窗口的鼠标命中检测依赖 Alpha 通道，Alpha=0 的像素对该窗口"不存在"。设置透明度时必须保留至少 1 的 Alpha 值以保证交互性。

---
