# Battery Overlay C++ 开发笔记

## 项目文件说明

| 文件路径 | 说明 |
|---------|------|
| `main.cpp` | 核心源码，包含窗口创建、渲染、电池状态检测、配置管理、对话框等全部逻辑 |
| `CMakeLists.txt` | CMake 构建配置，C++17 + MSVC，静态链接 CRT (`/MT`)，启用 `/utf-8` 解决中文乱码 |
| `icon.ico` / `icon.png` | 应用图标资源 |
| `res/resource.h` | RC 资源头文件，定义 `IDI_ICON` (101) 和 `VS_VERSION_INFO` (1) |
| `res/app.rc` | RC 资源脚本，包含图标、manifest 嵌入、VERSIONINFO（文件版本、公司名等） |
| `res/app.manifest` | Windows 应用程序清单，声明 DPI 感知（PerMonitorV2）和 UAC 级别 |
| `.github/workflows/build.yml` | GitHub Actions CI 配置，Visual Studio 2022 + MSVC + CMake 构建 |

---

### v2.0.5.0 (2026-05-11)
- ✅ 移除输入法（IME）检测功能，因做不到安全地准确获取输入法状态（如powershell、wps中不能获取）
- ✅ 第一行显示改为符号：⚡(充电) / BAT(电池) / PWR(台式机)
-

## 编译踩坑记录

### 1. RC2104: VERSIONINFO 常量未定义

**问题：** RC 编译器报错 `VS_FFI_FILEFLAGSMASK`、`V_OS_NT_WINDOWS32`、`VFT_APP`、`VFT2_UNKNOWN`、`VS_FF_DEBUG` 未定义。

**原因：** 这些常量在 SDK 头文件中的定义方式 RC 编译器无法识别。

**解决：** 直接使用数值常量替代：

```rc
FILEFLAGSMASK 0x3FL      // 替代 VS_FFI_FILEFLAGSMASK
FILEOS 0x40004           // 替代 V_OS_NT_WINDOWS32
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

### 5. GET_X_LPARAM / GET_Y_LPARAM 未定义

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

### 2. 行间距过大

**问题：** 上下两行文字间距过大，没有充分利用窗口高度。

**原因：** `DrawText` 的 `DT_VCENTER` 配合 `DT_SINGLELINE` 会产生额外垂直居中效果；字号偏小；两行矩形有重叠但没有正确利用。

**解决：**
- 字号从 `h/2 - 2` 增大到 `h/2`（最大化利用空间）
- 上下两行各占半窗口，第一行 `{0, 0, w, h/2}`，第二行 `{0, h/2, w, h}`
- 使用 `DT_VCENTER` 让每行在各自半区垂直居中

### 3. 自定义背景透明度无效

**问题：** 在右键菜单设置背景透明度后，背景透明度没有变化，始终全透明或全不透明。

**原因：** `render` 函数中 Alpha 通道处理的颜色比较顺序错误。32bpp BI_RGB DIB 像素格式为 **BGRΑ**（B 在最低地址），但代码按 `px[i4]==br && px[i4+1]==bg_ && px[i4+2]==bb`（RGB 顺序）比较，导致背景像素的 Alpha 值永远不匹配，`ba` 设置无效。

**解决：** 将颜色比较改为正确的 BGR 顺序：
`px[i4] == bb && px[i4+1] == bg_ && px[i4+2] == br`

### 4. 透明度为 0 时程序无响应

**问题：** 在右键菜单将背景透明度设置为 0 后，窗口无法拖拽，右键点击也无菜单弹出，表现为"卡死"。

**原因：** `ba=0` 时，背景像素的 Alpha 值为 0（完全透明）。Windows 对分层窗口（WS_EX_LAYERED + 每像素 Alpha）**只对 Alpha > 0 的像素发送鼠标消息**。背景全透明后，只有文字像素（Alpha=255）能接收鼠标消息，但文字区域很小，很难点中，表现为窗口无响应。

**修复：**
1. `render()` 中背景 Alpha 使用 `std::max(ba, (BYTE)1)`，确保背景始终有至少 1 的 Alpha 值
2. `setAlpha()` 中存储 `std::max(a, (BYTE)1)`，拒绝 `ba=0`
3. `loadConfig()` 中 `ba` 下限改为 1，防止从配置文件加载到 0
4. `parseAlpha()` 中拒绝 0，有效范围改为 1-255
5. 透明度输入框提示和错误提示同步更新为 1-255

**经验：** Windows 分层窗口的鼠标命中检测依赖 Alpha 通道，Alpha=0 的像素对该窗口"不存在"。设置透明度时必须保留至少 1 的 Alpha 值以保证交互性。

### 5. RGB 颜色 R/B 分量互换

**问题：** 自定义背景颜色和字体颜色时，R 和 B 值实际效果互换，配置文件也一样。

**原因：** `RGB` 宏参数是 `(r, g, b)`，但代码中写成了 `RGB(fb, fg, fr)` 和 `RGB(bb, bg, br)`，顺序错误。

**解决：**
- `fontColorRef()`：`RGB(fb, fg, fr)` → `RGB(fr, fg, fb)`
- `bgColorRef()`：`RGB(bb, bg, br)` → `RGB(br, bg, bb)`

---

## 关键 API 参考

| 功能 | API |
|-----|-----|
| 分层窗口每像素 Alpha 渲染 | `UpdateLayeredWindow` + 32bpp DIB + `BLENDFUNCTION` |
| 电池状态检测 | `GetSystemPowerStatus` + `SYSTEM_POWER_STATUS` |
| 拖动时鼠标位置 | `GetMessagePos()` 而非 `GetCursorPos()` |
| 中文乱码解决 | `/utf-8` 编译选项 + `L"中文字符串"` 前缀 |
| 窗口不抢焦点 | `WS_EX_NOACTIVATE` + `MA_NOACTIVATE` 返回值 |
| 定时器设置 | `SetTimer` + 根据显示模式动态设置间隔 |

---

## 功能说明

### 1. 单击切换显示模式

**功能描述：** 单击悬浮窗（不是拖拽）可以在两种显示模式间切换：
- **正常模式**：第一行 ⚡(充电)/BAT(电池)/PWR(台式机)，第二行电池百分比
- **时间模式**：第一行 `hh:mm`（小时:分钟），第二行实时秒数

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

### 2. 双模式定时器

**功能描述：** 根据显示模式动态调整定时器刷新间隔，平衡实时性与 CPU 占用。

**实现原理：**
- 时间模式（显示秒数）：100ms 间隔，保证秒数实时更新
- 正常模式（显示电池状态）：300ms 间隔，降低 CPU 占用
- 切换模式时重新设置定时器

**关键代码：**
```cpp
// 根据显示模式返回定时器间隔（毫秒）
static UINT getTimerInterval() {
    return AppState::showTime.load() ? 100 : 300;
}

// 切换模式后重新设置定时器
SetTimer(hwnd, 1, getTimerInterval(), nullptr);
```

**定时器重置时机：**
- `WM_CREATE`（窗口创建时）
- `WM_LBUTTONUP`（单击切换模式时）
- `WM_CAPTURECHANGED`（鼠标捕获改变时）

### 3. 右键菜单自定义

**功能描述：** 右键点击悬浮窗弹出菜单，可自定义窗口大小、字体颜色、背景颜色、透明度。

**配置持久化：** 所有设置保存在 `battery_overlay.json` 文件中，程序重启后自动加载。

**配置文件格式：**
```json
{"x":100,"y":100,"w":33,"h":33,"fr":255,"fg":255,"fb":255,"br":0,"bg":32,"bb":63,"ba":204}
```

---

## 符号说明（v2.0.5.0+）

| 符号 | 含义 | 颜色 |
|------|------|------|
| ⚡ | 充电中 | 字体颜色 |
| BAT | 使用电池（笔记本） | 字体颜色 |
| PWR | 台式机（无电池） | 字体颜色 |
| 数字% | 电池百分比 | 字体颜色 |
| hh:mm | 当前时间（小时:分钟） | 字体颜色 |
| ss | 当前秒数 | 字体颜色 |

---

## 构建说明

### 本地构建（MSVC）
```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A Win32
cmake --build . --config Release
```

### GitHub Actions 自动构建
推送至 `main` 分支后，GitHub Actions 自动构建并生成 `battery_overlay.exe`，可在 Actions 页面下载制品。
