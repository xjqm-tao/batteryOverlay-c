/**
 * battery_overlay - C++ 重写版
 * 作者：林涛 -920250443
 * 功能：桌面悬浮窗，显示电池百分比 + 输入法状态
 */

#define UNICODE
#define _UNICODE

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <atomic>
#include <mutex>
#include <optional>
#include <algorithm>
#include <cstdlib>

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

// ══════════════
// 菜单命令 ID
constexpr UINT ID_EXIT       = 1001;
constexpr UINT ID_TOPMOST    = 1002;
constexpr UINT ID_ABOUT      = 1003;
constexpr UINT ID_CUSTOM_SZ  = 1106;
constexpr UINT ID_CUSTOM_FC  = 1206;
constexpr UINT ID_CUSTOM_BG  = 1304;
constexpr UINT ID_CUSTOM_AL  = 1404;

// 窗口类名
constexpr wchar_t CLASS_NAME[]    = L"BatteryOverlayClass";
constexpr wchar_t DLG_CLASS[]    = L"BODlg";
constexpr wchar_t SZDLG_CLASS[]  = L"BOSzDlg";

// IME 控制常量
constexpr UINT WM_IME_CTRL     = 0x0283;
constexpr UINT IMC_GETOPENSTATUS  = 0x0005;
constexpr UINT IMC_GETCONVERSIONMODE = 0x001;

// ══════════════
// 配置结构体
struct Config {
    int x, y, w, h;
    BYTE fr, fg, fb;    // 字体颜色 RGB
    BYTE br, bg, bb, ba; // 背景颜色 RGB + 透明度

    static Config defaults() {
        return { -1, -1, 32, 32,
                 90, 202, 106,  // 默认字体颜色
                 80, 60, 50, 210 }; // 默认背景色 + 透明度
    }

    COLORREF fontColorRef() const {
        return RGB(fb, fg, fr);
    }

    COLORREF bgColorRef() const {
        return RGB(bb, bg, br);
    }

    int fontSize() const {
        // 文字尽可能大：每行高度约为窗口高度的一半，留 1px 边距
        return std::max(h / 2 - 1, 8);
    }
};

// ═════════════
// 全局状态
namespace AppState {
    std::atomic<bool>   topMost{true};
    std::atomic<bool>   dragging{false};
    std::atomic<int>    dragOffX{0};
    std::atomic<int>    dragOffY{0};
    std::atomic<HWND>   mainHwnd{nullptr};
    std::mutex          cfgMutex;
    Config              cfg = Config::defaults();

    // 输入对话框状态
    std::atomic<HWND>   dlgEdit{nullptr};
    std::atomic<bool>   dlgClosed{false};
    std::mutex          dlgResultMutex;
    std::optional<std::wstring> dlgResult;

    // 大小对话框状态
    std::atomic<HWND>   szEditW{nullptr};
    std::atomic<HWND>   szEditH{nullptr};
    std::atomic<bool>   szClosed{false};
    std::mutex          szResultMutex;
    std::optional<std::wstring> szResultW;
    std::optional<std::wstring> szResultH;
}

// ═════════════
// 配置持久化
static std::wstring configPath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    // 去掉文件名
    wchar_t* slash = wcsrchr(buf, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat_s(buf, L"battery_overlay.json");
    return buf;
}

static int jsonI32(const std::wstring& data, const wchar_t* key) {
    std::wstring pat = L"\"";
    pat += key;
    pat += L"\"";
    auto pos = data.find(pat);
    if (pos == std::wstring::npos) return 0;
    auto rest = data.substr(pos + pat.size());
    // skip ':'
    auto colon = rest.find(L':');
    if (colon == std::wstring::npos) return 0;
    rest = rest.substr(colon + 1);
    // skip whitespace
    size_t start = 0;
    while (start < rest.size() && (rest[start] == L' ' || rest[start] == L'\t' || rest[start] == L'\n' || rest[start] == L'\r'))
        start++;
    // parse digits (with optional leading '-')
    size_t end = start;
    if (end < rest.size() && rest[end] == L'-') end++;
    while (end < rest.size() && rest[end] >= L'0' && rest[end] <= L'9') end++;
    if (end == start) return 0;
    return _wtoi(rest.substr(start, end - start).c_str());
}

static void loadConfig() {
    auto path = configPath();
    std::wifstream ifs(path);
    if (!ifs.is_open()) {
        AppState::cfg = Config::defaults();
        return;
    }
    std::wstringstream wss;
    wss << ifs.rdbuf();
    auto data = wss.str();

    auto& c = AppState::cfg;
    c.x  = jsonI32(data, L"x");
    c.y  = jsonI32(data, L"y");
    c.w  = std::max(jsonI32(data, L"w"), 20);
    c.h  = std::max(jsonI32(data, L"h"), 16);
    c.fr = static_cast<BYTE>(std::clamp(jsonI32(data, L"fr"), 0, 255));
    c.fg = static_cast<BYTE>(std::clamp(jsonI32(data, L"fg"), 0, 255));
    c.fb = static_cast<BYTE>(std::clamp(jsonI32(data, L"fb"), 0, 255));
    c.br = static_cast<BYTE>(std::clamp(jsonI32(data, L"br"), 0, 255));
    c.bg = static_cast<BYTE>(std::clamp(jsonI32(data, L"bg"), 0, 255));
    c.bb = static_cast<BYTE>(std::clamp(jsonI32(data, L"bb"), 0, 255));
    c.ba = static_cast<BYTE>(std::clamp(jsonI32(data, L"ba"), 0, 255));
}

static void saveConfig() {
    auto& c = AppState::cfg;
    auto path = configPath();
    std::wofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << L"{\"x\":" << c.x
        << L",\"y\":" << c.y
        << L",\"w\":" << c.w
        << L",\"h\":" << c.h
        << L",\"fr\":" << static_cast<int>(c.fr)
        << L",\"fg\":" << static_cast<int>(c.fg)
        << L",\"fb\":" << static_cast<int>(c.fb)
        << L",\"br\":" << static_cast<int>(c.br)
        << L",\"bg\":" << static_cast<int>(c.bg)
        << L",\"bb\":" << static_cast<int>(c.bb)
        << L",\"ba\":" << static_cast<int>(c.ba)
        << L"}";
}

// ═══════════════════
// 输入法检测（跨进程方案）
// 预期逻辑：
//   Caps Lock 切换到开 → "EN"
//   Caps Lock 切换到关 + 英文输入 → "en"
//   Caps Lock 切换到关 + 中文输入 → "中"
static std::wstring getInputLang() {
    // GetKeyState(VK_CAPITAL) 返回值：
    //   bit0 (0x0001) = toggle 状态：1=CapsLock开, 0=CapsLock关
    //   bit15 (0x8000) = 按键当前是否按下（仅在按键按下瞬间为1）
    // 用 bit0 检测 CapsLock 切换状态，不用 bit15（否则松键后就丢了状态）
    bool capsOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    if (capsOn) return L"EN";

    // 获取当前输入法线程的键盘布局（稳定，不受前台窗口闪烁影响）
    HKL hkl = GetKeyboardLayout(0);
    LANGID lid = static_cast<LANGID>(reinterpret_cast<UINT_PTR>(hkl) & 0xFFFF);

    // 非 CJK 键盘：英文输入
    if (lid != 0x0804 && lid != 0x0404 && lid != 0x1004 && lid != 0x0C04
        && lid != 0x0411 && lid != 0x0412) {
        return L"en";
    }

    // 尝试获取前台窗口 IME
    HWND fg = GetForegroundWindow();
    if (fg && fg != AppState::mainHwnd.load()) {
        HWND ime = ImmGetDefaultIMEWnd(fg);
        if (ime) {
            LRESULT r = SendMessageW(ime, WM_IME_CTRL, IMC_GETCONVERSIONMODE, 0);
            bool cn = (r & 0x001) != 0;
            if (lid == 0x0411) return cn ? L"\u3042" : L"\u30a2";
            if (lid == 0x0412) return cn ? L"\ud55c" : L"\uac00";
            return cn ? L"\u4e2d" : L"en";
        }
    }

    // ime 为 null 或 fg 无效：枚举键盘布局列表判断
    HKL hklList[16];
    int n = GetKeyboardLayoutList(16, hklList);
    bool hasIME = false;
    for (int i = 0; i < n; i++) {
        LANGID tl = static_cast<LANGID>(
            reinterpret_cast<UINT_PTR>(hklList[i]) & 0xFFFF);
        if (tl == 0x0804 || tl == 0x0404 || tl == 0x1004 || tl == 0x0C04) {
            hasIME = true;
            break;
        }
    }
    // 备选：键盘布局本身是中文
    if (!hasIME && (lid == 0x0804 || lid == 0x0404 ||
                    lid == 0x1004 || lid == 0x0C04)) {
        hasIME = true;
    }

    if (lid == 0x0411) return hasIME ? L"\u3042" : L"\u30a2";
    if (lid == 0x0412) return hasIME ? L"\ud55c" : L"\uac00";
    return hasIME ? L"\u4e2d" : L"en";
}

// ══════════════════════════════
// 工具函数

// 解析 "R,G,B" 颜色字符串
static std::optional<std::tuple<BYTE,BYTE,BYTE>> parseRgb(const std::wstring& s) {
    // 按 L',' 分割
    size_t c1 = s.find(L',');
    if (c1 == std::wstring::npos) return {};
    size_t c2 = s.find(L',', c1 + 1);
    if (c2 == std::wstring::npos) return {};

    auto parseVal = [](const std::wstring& sub) -> std::optional<int> {
        try { return std::stoi(sub); }
        catch (...) { return {}; }
    };

    auto r = parseVal(s.substr(0, c1));
    auto g = parseVal(s.substr(c1 + 1, c2 - c1 - 1));
    auto b = parseVal(s.substr(c2 + 1));
    if (!r || !g || !b) return {};
    if (*r < 0 || *r > 255 || *g < 0 || *g > 255 || *b < 0 || *b > 255) return {};
    return {{static_cast<BYTE>(*r), static_cast<BYTE>(*g), static_cast<BYTE>(*b)}};
}

// 解析透明度 (0-255)
static std::optional<BYTE> parseAlpha(const std::wstring& s) {
    try {
        int v = std::stoi(s);
        if (v < 0 || v > 255) return {};
        return static_cast<BYTE>(v);
    } catch (...) { return {}; }
}

// 从 EDIT 控件读取文本
static std::wstring readEditText(HWND hedit) {
    int len = static_cast<int>(SendMessageW(hedit, WM_GETTEXTLENGTH, 0, 0));
    if (len <= 0) return L"";
    std::wstring buf(len + 1, L'\0');
    SendMessageW(hedit, WM_GETTEXT, len + 1, reinterpret_cast<LPARAM>(buf.data()));
    buf.resize(len);
    return buf;
}

// 设置子控件字体
static void setChildFont(HWND hwnd, HFONT hfont) {
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);
}

// 选中输入框全部文字
static void selectAll(HWND hedit) {
    SendMessageW(hedit, EM_SETSEL, 0, -1);
}

// ═══════════════════════════
// 窗口渲染
static void render(HWND hwnd) {
    Config c;
    {
        std::lock_guard<std::mutex> lock(AppState::cfgMutex);
        c = AppState::cfg;
    }

    int w = c.w, h = c.h;
    COLORREF fc = c.fontColorRef();
    COLORREF bc = c.bgColorRef();
    BYTE br = c.br, bg_ = c.bg, bb = c.bb, ba = c.ba;
    int fsz = c.fontSize();

    HDC hdcScr = GetDC(nullptr);
    HDC hdc = CreateCompatibleDC(hdcScr);
    ReleaseDC(nullptr, hdcScr);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(hdc, hbmp));

    // 填充背景色
    RECT rect = { 0, 0, w, h };
    HBRUSH brush = CreateSolidBrush(bc);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
    // 设置背景色使抗锯齿边缘与背景融合
    SetBkColor(hdc, bc);

    // 创建字体（微软雅黑 UI）
    HFONT font = CreateFontW(fsz, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
    SetTextColor(hdc, fc);
    SetBkMode(hdc, TRANSPARENT);

    // 获取电池状态
    SYSTEM_POWER_STATUS sps = {};
    GetSystemPowerStatus(&sps);
    bool charging = (sps.ACLineStatus == 1);

    // 第一行：输入法状态 + 充电标识（占上半部分）
    std::wstring lang = getInputLang();
    std::wstring display = charging ? lang + L"\u26A1" : lang;
    RECT r1 = { 0, 0, w, h / 2 };
    DrawTextW(hdc, display.c_str(), static_cast<int>(display.size()), &r1,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    // 第二行：电池百分比（占下半部分）
    std::wstring txt = std::to_wstring(sps.BatteryLifePercent) + L"%";
    RECT r2 = { 0, h / 2, w, h };
    DrawTextW(hdc, txt.c_str(), static_cast<int>(txt.size()), &r2,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, oldFont);
    DeleteObject(font);

    // 设置 Alpha 通道：背景像素半透明，文字/抗锯齿像素完全不透明
    auto px = static_cast<BYTE*>(bits);
    for (int i = 0; i < w * h; i++) {
        int i4 = i * 4;
        if (px[i4] == br && px[i4 + 1] == bg_ && px[i4 + 2] == bb) {
            px[i4 + 3] = ba;
        } else {
            px[i4 + 3] = 255;
        }
    }

    // 更新分层窗口
    SIZE size = { w, h };
    POINT pt = { 0, 0 };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, hdc, &pt, 0, &blend, ULW_ALPHA);

    SelectObject(hdc, oldBmp);
    DeleteObject(hbmp);
    DeleteDC(hdc);
}

// ══════════════════
// 配置修改函数
static void setSize(HWND hwnd, int w, int h) {
    {
        std::lock_guard<std::mutex> lock(AppState::cfgMutex);
        AppState::cfg.w = w;
        AppState::cfg.h = h;
        saveConfig();
    }
    RECT rc = {};
    GetWindowRect(hwnd, &rc);
    SetWindowPos(hwnd, nullptr, rc.left, rc.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    render(hwnd);
}

static void setFontColor(HWND hwnd, BYTE r, BYTE g, BYTE b) {
    {
        std::lock_guard<std::mutex> lock(AppState::cfgMutex);
        AppState::cfg.fr = r;
        AppState::cfg.fg = g;
        AppState::cfg.fb = b;
        saveConfig();
    }
    render(hwnd);
}

static void setBgColor(HWND hwnd, BYTE r, BYTE g, BYTE b) {
    {
        std::lock_guard<std::mutex> lock(AppState::cfgMutex);
        AppState::cfg.br = r;
        AppState::cfg.bg = g;
        AppState::cfg.bb = b;
        saveConfig();
    }
    render(hwnd);
}

static void setAlpha(HWND hwnd, BYTE a) {
    {
        std::lock_guard<std::mutex> lock(AppState::cfgMutex);
        AppState::cfg.ba = a;
        saveConfig();
    }
    render(hwnd);
}

static void showAbout(HWND hwnd) {
    int r = MessageBoxW(hwnd,
        L"电池悬浮窗 v2.0.1.0 (C++ 重写版)\n"
        L"作者：林涛-920250443\n\n"
        L"右键菜单可自定义：\n"
        L"  \xB7 窗口大小（手动输入长宽）\n"
        L"  \xB7 字体颜色（手动输入 RGB）\n"
        L"  \xB7 背景颜色（手动输入 RGB）\n"
        L"  \xB7 背景透明度（手动输入数值）\n\n"
        L"点击「确定」打开项目主页",
        L"关于 Battery Overlay",
        MB_OKCANCEL | MB_ICONINFORMATION);
    if (r == IDOK) {
        ShellExecuteW(hwnd, L"open", L"https://github.com/xjqm-tao/batteryOverlay-c",
            nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ════════════════
// 输入对话框（单输入框）
static LRESULT CALLBACK dlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        UINT id = LOWORD(wp);
        if (id == 1 || id == 2) { // IDOK or IDCANCEL
            if (id == 1) {
                HWND hedit = AppState::dlgEdit.load();
                std::lock_guard<std::mutex> lock(AppState::dlgResultMutex);
                AppState::dlgResult = readEditText(hedit);
            }
            AppState::dlgClosed.store(true);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        AppState::dlgClosed.store(true);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static std::optional<std::wstring> inputDialog(HWND parent, const wchar_t* title,
    const wchar_t* prompt, const wchar_t* defaultVal)
{
    AppState::dlgClosed.store(false);
    {
        std::lock_guard<std::mutex> lock(AppState::dlgResultMutex);
        AppState::dlgResult = {};
    }
    AppState::dlgEdit.store(nullptr);

    HINSTANCE hinst = GetModuleHandleW(nullptr);

    // 注册对话框窗口类（仅一次）
    static bool dlgRegistered = false;
    if (!dlgRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = dlgProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = DLG_CLASS;
        RegisterClassW(&wc);
        dlgRegistered = true;
    }

    int dw = 300, dh = 130;
    RECT prc = {};
    GetWindowRect(parent, &prc);
    int dx = prc.left + ((prc.right - prc.left) - dw) / 2;
    int dy = prc.top + ((prc.bottom - prc.top) - dh) / 2;

    HWND hdlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        DLG_CLASS, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dx, dy, dw, dh,
        parent, nullptr, hinst, nullptr);
    if (!hdlg) return {};

    HFONT hfont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // 提示文本
    HWND hlabel = CreateWindowExW(0, L"STATIC", prompt,
        WS_CHILD | WS_VISIBLE, 10, 10, dw - 20, 20,
        hdlg, nullptr, hinst, nullptr);
    setChildFont(hlabel, hfont);

    // 输入框
    HWND hedit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", defaultVal,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        10, 34, dw - 20, 22,
        hdlg, nullptr, hinst, nullptr);
    AppState::dlgEdit.store(hedit);
    setChildFont(hedit, hfont);

    // 确定按钮（ID=1=IDOK）
    HWND hok = CreateWindowExW(0, L"BUTTON", L"\u786E\u5B9A", // 确定
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        dw / 2 - 65, 65, 60, 26,
        hdlg, reinterpret_cast<HMENU>(1), hinst, nullptr);
    setChildFont(hok, hfont);

    // 取消按钮（ID=2=IDCANCEL）
    HWND hcancel = CreateWindowExW(0, L"BUTTON", L"\u53D6\u6D88", // 取消
        WS_CHILD | WS_VISIBLE,
        dw / 2 + 5, 65, 60, 26,
        hdlg, reinterpret_cast<HMENU>(2), hinst, nullptr);
    setChildFont(hcancel, hfont);

    selectAll(hedit);
    SetFocus(hedit);

    EnableWindow(parent, FALSE);

    // 模态消息循环
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND he = AppState::dlgEdit.load();
            std::lock_guard<std::mutex> lock(AppState::dlgResultMutex);
            AppState::dlgResult = readEditText(he);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (AppState::dlgClosed.load()) break;
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    DestroyWindow(hdlg);

    std::lock_guard<std::mutex> lock(AppState::dlgResultMutex);
    auto result = std::move(AppState::dlgResult);
    AppState::dlgResult = {};
    return result;
}

// ═══════════════════
// 大小输入对话框（双输入框：宽度 + 高度）
static LRESULT CALLBACK sizeDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        UINT id = LOWORD(wp);
        if (id == 1 || id == 2) {
            if (id == 1) {
                HWND hw = AppState::szEditW.load();
                HWND hh = AppState::szEditH.load();
                std::lock_guard<std::mutex> lock(AppState::szResultMutex);
                AppState::szResultW = readEditText(hw);
                AppState::szResultH = readEditText(hh);
            }
            AppState::szClosed.store(true);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        AppState::szClosed.store(true);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static std::optional<std::pair<int,int>> sizeDialog(HWND parent, int curW, int curH) {
    AppState::szClosed.store(false);
    {
        std::lock_guard<std::mutex> lock(AppState::szResultMutex);
        AppState::szResultW = {};
        AppState::szResultH = {};
    }
    AppState::szEditW.store(nullptr);
    AppState::szEditH.store(nullptr);

    HINSTANCE hinst = GetModuleHandleW(nullptr);

    static bool szRegistered = false;
    if (!szRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = sizeDlgProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = SZDLG_CLASS;
        RegisterClassW(&wc);
        szRegistered = true;
    }

    int dw = 300, dh = 180;
    RECT prc = {};
    GetWindowRect(parent, &prc);
    int dx = prc.left + ((prc.right - prc.left) - dw) / 2;
    int dy = prc.top + ((prc.bottom - prc.top) - dh) / 2;

    HWND hdlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        SZDLG_CLASS, L"\u7A97\u53E3\u5927\u5C0F", // 窗口大小
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dx, dy, dw, dh,
        parent, nullptr, hinst, nullptr);
    if (!hdlg) return {};

    HFONT hfont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // 宽度标签
    HWND hl1 = CreateWindowExW(0, L"STATIC", L"\u5BBD\u5EA6\uFF08\u6700\u5C0F 20\uFF09\uFF1A",
        WS_CHILD | WS_VISIBLE, 10, 10, dw - 20, 20,
        hdlg, nullptr, hinst, nullptr);
    setChildFont(hl1, hfont);

    // 宽度输入框
    std::wstring wStr = std::to_wstring(curW);
    HWND hew = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wStr.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        10, 34, dw - 20, 22,
        hdlg, nullptr, hinst, nullptr);
    AppState::szEditW.store(hew);
    setChildFont(hew, hfont);

    // 高度标签
    HWND hl2 = CreateWindowExW(0, L"STATIC", L"\u9AD8\u5EA6\uFF08\u6700\u5C0F 20\uFF09\uFF1A",
        WS_CHILD | WS_VISIBLE, 10, 64, dw - 20, 20,
        hdlg, nullptr, hinst, nullptr);
    setChildFont(hl2, hfont);

    // 高度输入框
    std::wstring hStr = std::to_wstring(curH);
    HWND heh = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", hStr.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        10, 88, dw - 20, 22,
        hdlg, nullptr, hinst, nullptr);
    AppState::szEditH.store(heh);
    setChildFont(heh, hfont);

    // 确定按钮
    HWND hok = CreateWindowExW(0, L"BUTTON", L"\u786E\u5B9A",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        dw / 2 - 65, 120, 60, 26,
        hdlg, reinterpret_cast<HMENU>(1), hinst, nullptr);
    setChildFont(hok, hfont);

    // 取消按钮
    HWND hcancel = CreateWindowExW(0, L"BUTTON", L"\u53D6\u6D88",
        WS_CHILD | WS_VISIBLE,
        dw / 2 + 5, 120, 60, 26,
        hdlg, reinterpret_cast<HMENU>(2), hinst, nullptr);
    setChildFont(hcancel, hfont);

    selectAll(hew);
    SetFocus(hew);

    EnableWindow(parent, FALSE);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND hw = AppState::szEditW.load();
            HWND hh = AppState::szEditH.load();
            std::lock_guard<std::mutex> lock(AppState::szResultMutex);
            AppState::szResultW = readEditText(hw);
            AppState::szResultH = readEditText(hh);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (AppState::szClosed.load()) break;
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    DestroyWindow(hdlg);

    std::optional<std::wstring> sw, sh;
    {
        std::lock_guard<std::mutex> lock(AppState::szResultMutex);
        sw = std::move(AppState::szResultW);
        sh = std::move(AppState::szResultH);
        // 保留 szResultW/szResultH 的状态给调用方检查（不清空）
    }
    if (!sw || !sh) return {};
    try {
        int w = std::stoi(*sw);
        int h = std::stoi(*sh);
        if (w < 20 || h < 20) return {};
        // 成功，清空结果
        {
            std::lock_guard<std::mutex> lock(AppState::szResultMutex);
            AppState::szResultW = {};
            AppState::szResultH = {};
        }
        return {{w, h}};
    } catch (...) { return {}; }
}

// ═════════════════
// 主窗口过程
static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        AppState::mainHwnd.store(hwnd);
        AppState::dragging.store(false);
        AppState::dragOffX.store(0);
        AppState::dragOffY.store(0);
        SetTimer(hwnd, 1, 100, nullptr);
        render(hwnd);
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_NCHITTEST:
        return HTCLIENT;

    case WM_TIMER:
        render(hwnd);
        return 0;

    case WM_WINDOWPOSCHANGING:
        if (AppState::dragging.load()) {
            auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
            pos->flags &= ~SWP_HIDEWINDOW;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    // ── 拖动逻辑 ──
    // 拖动状态：点击时记录"窗口初始位置"和"鼠标初始位置"，拖动中用差值计算新位置
    case WM_LBUTTONDOWN: {
        // 记录点击瞬间的鼠标屏幕坐标
        POINT pt = {};
        GetCursorPos(&pt);
        AppState::dragOffX.store(pt.x);
        AppState::dragOffY.store(pt.y);
        // 记录窗口初始位置（后面 WM_MOUSEMOVE 里用）
        RECT rc = {};
        GetWindowRect(hwnd, &rc);
        AppState::cfgMutex.lock();
        AppState::cfg.x = rc.left;
        AppState::cfg.y = rc.top;
        AppState::cfgMutex.unlock();
        AppState::dragging.store(true);
        SetCapture(hwnd);
        return 0;
    }

    case WM_LBUTTONUP: {
        bool wasDrag = AppState::dragging.exchange(false);
        ReleaseCapture();
        if (wasDrag) {
            // 保存当前位置
            RECT rc = {};
            GetWindowRect(hwnd, &rc);
            AppState::cfgMutex.lock();
            AppState::cfg.x = rc.left;
            AppState::cfg.y = rc.top;
            AppState::cfgMutex.unlock();
            saveConfig();
            render(hwnd);
            if (AppState::topMost.load()) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        return 0;
    }

    case WM_CAPTURECHANGED: {
        bool wasDrag = AppState::dragging.exchange(false);
        if (wasDrag) {
            RECT rc = {};
            GetWindowRect(hwnd, &rc);
            AppState::cfgMutex.lock();
            AppState::cfg.x = rc.left;
            AppState::cfg.y = rc.top;
            AppState::cfgMutex.unlock();
            saveConfig();
            render(hwnd);
            if (AppState::topMost.load()) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_MOUSEMOVE: {
        if (AppState::dragging.load()) {
            // 当前鼠标屏幕坐标（用 GetMessagePos 比 GetCursorPos 更稳定）
            POINT mp = {};
            DWORD pos = GetMessagePos();
            mp.x = (int)(short)LOWORD(pos);
            mp.y = (int)(short)HIWORD(pos);

            int ix = AppState::cfg.x;
            int iy = AppState::cfg.y;
            int ox = AppState::dragOffX.load();
            int oy = AppState::dragOffY.load();

            int raw_nx = ix + mp.x - ox;
            int raw_ny = iy + mp.y - oy;

            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            int cw, ch;
            {
                std::lock_guard<std::mutex> lock(AppState::cfgMutex);
                cw = AppState::cfg.w;
                ch = AppState::cfg.h;
            }
            int lo_x = -cw + 10, hi_x = sw - 10;
            int lo_y = -ch + 10, hi_y = sh - 10;
            int nx = raw_nx < lo_x ? lo_x : (raw_nx > hi_x ? hi_x : raw_nx);
            int ny = raw_ny < lo_y ? lo_y : (raw_ny > hi_y ? hi_y : raw_ny);
            SetWindowPos(hwnd, HWND_TOPMOST, nx, ny, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE);
            render(hwnd);
        }
        return 0;
    }

    // ── 右键菜单 ──
    case WM_RBUTTONUP: {
        HMENU hm = CreatePopupMenu();

        // 置顶选项
        AppendMenuW(hm, MF_STRING, ID_TOPMOST, L"\u7F6E\u9876\u663E\u793A"); // 置顶显示
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);

        // 窗口大小
        AppendMenuW(hm, MF_STRING, ID_CUSTOM_SZ, L"\u7A97\u53E3\u5927\u5C0F"); // 窗口大小
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);

        // 字体颜色
        AppendMenuW(hm, MF_STRING, ID_CUSTOM_FC, L"\u5B57\u4F53\u989C\u8272..."); // 字体颜色...
        // 背景颜色
        AppendMenuW(hm, MF_STRING, ID_CUSTOM_BG, L"\u80CC\u666F\u989C\u8272..."); // 背景颜色...
        // 透明度
        AppendMenuW(hm, MF_STRING, ID_CUSTOM_AL, L"\u900F\u660E\u5EA6..."); // 透明度...

        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);

        // 关于
        AppendMenuW(hm, MF_STRING, ID_ABOUT, L"\u5173\u4E8E"); // 关于
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);

        // 退出
        AppendMenuW(hm, MF_STRING, ID_EXIT, L"\u9000\u51FA"); // 退出

        // 置顶勾选
        if (AppState::topMost.load()) {
            CheckMenuItem(hm, ID_TOPMOST, MF_BYCOMMAND | MF_CHECKED);
        }

        POINT pt = {};
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd);

        UINT cmd = TrackPopupMenu(hm, TPM_RETURNCMD | TPM_NONOTIFY,
            pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hm);
        PostMessageW(hwnd, WM_NULL, 0, 0);

        // 处理菜单命令
        switch (cmd) {
        case ID_EXIT:
            DestroyWindow(hwnd);
            break;

        case ID_TOPMOST: {
            bool v = !AppState::topMost.load();
            AppState::topMost.store(v);
            HWND pos = v ? HWND_TOPMOST : HWND_NOTOPMOST;
            SetWindowPos(hwnd, pos, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        }

        case ID_CUSTOM_SZ: {
            int cw, ch;
            {
                std::lock_guard<std::mutex> lock(AppState::cfgMutex);
                cw = AppState::cfg.w;
                ch = AppState::cfg.h;
            }
            auto result = sizeDialog(hwnd, cw, ch);
            if (result) {
                setSize(hwnd, result->first, result->second);
            } else {
                // 检查是否有无效输入
                std::lock_guard<std::mutex> lock(AppState::szResultMutex);
                if (AppState::szResultW || AppState::szResultH) {
                    MessageBoxW(hwnd,
                        L"\u65E0\u6548\u7684\u7A97\u53E3\u5927\u5C0F\uFF01\n\u5BBD\u5EA6\u548C\u9AD8\u5EA6\u5FC5\u987B\u4E3A \u2265 20 \u7684\u6574\u6570",
                        L"\u9519\u8BEF", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case ID_CUSTOM_FC: {
            BYTE fr, fg, fb;
            {
                std::lock_guard<std::mutex> lock(AppState::cfgMutex);
                fr = AppState::cfg.fr;
                fg = AppState::cfg.fg;
                fb = AppState::cfg.fb;
            }
            std::wstring cur = std::to_wstring(fr) + L"," + std::to_wstring(fg) + L"," + std::to_wstring(fb);
            auto input = inputDialog(hwnd, L"\u5B57\u4F53\u989C\u8272",
                L"\u8BF7\u8F93\u5165\u989C\u8272 (R,G,B)\uFF0C\u6BCF\u4E2A\u503C 0-255", cur.c_str());
            if (input) {
                auto rgb = parseRgb(*input);
                if (rgb) {
                    setFontColor(hwnd, std::get<0>(*rgb), std::get<1>(*rgb), std::get<2>(*rgb));
                } else {
                    MessageBoxW(hwnd,
                        L"\u65E0\u6548\u7684\u989C\u8272\u6570\u503C\uFF01\n"
                        L"\u683C\u5F0F\uFF1AR,G,B\uFF08\u5982 0,220,0\uFF09\n"
                        L"\u6BCF\u4E2A\u503C\u8303\u56F4 0-255",
                        L"\u9519\u8BEF", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case ID_CUSTOM_BG: {
            BYTE br, bg_, bb;
            {
                std::lock_guard<std::mutex> lock(AppState::cfgMutex);
                br = AppState::cfg.br;
                bg_ = AppState::cfg.bg;
                bb = AppState::cfg.bb;
            }
            std::wstring cur = std::to_wstring(br) + L"," + std::to_wstring(bg_) + L"," + std::to_wstring(bb);
            auto input = inputDialog(hwnd, L"\u80CC\u666F\u989C\u8272",
                L"\u8BF7\u8F93\u5165\u989C\u8272 (R,G,B)\uFF0C\u6BCF\u4E2A\u503C 0-255", cur.c_str());
            if (input) {
                auto rgb = parseRgb(*input);
                if (rgb) {
                    setBgColor(hwnd, std::get<0>(*rgb), std::get<1>(*rgb), std::get<2>(*rgb));
                } else {
                    MessageBoxW(hwnd,
                        L"\u65E0\u6548\u7684\u989C\u8272\u6570\u503C\uFF01\n"
                        L"\u683C\u5F0F\uFF1AR,G,B\uFF08\u5982 50,50,50\uFF09\n"
                        L"\u6BCF\u4E2A\u503C\u8303\u56F4 0-255",
                        L"\u9519\u8BEF", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case ID_CUSTOM_AL: {
            BYTE ba;
            {
                std::lock_guard<std::mutex> lock(AppState::cfgMutex);
                ba = AppState::cfg.ba;
            }
            std::wstring cur = std::to_wstring(ba);
            auto input = inputDialog(hwnd, L"\u900F\u660E\u5EA6",
                L"\u8BF7\u8F93\u5165\u900F\u660E\u5EA6 (0-255)\uFF0C0=\u5168\u900F\u660E 255=\u4E0D\u900F\u660E", cur.c_str());
            if (input) {
                auto a = parseAlpha(*input);
                if (a) {
                    setAlpha(hwnd, *a);
                } else {
                    MessageBoxW(hwnd,
                        L"\u65E0\u6548\u7684\u900F\u660E\u5EA6\u6570\u503C\uFF01\n\u8BF7\u8F93\u5165 0-255 \u4E4B\u95F4\u7684\u6574\u6570",
                        L"\u9519\u8BEF", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }

        case ID_ABOUT:
            showAbout(hwnd);
            break;
        }
        return 0;
    }

    case WM_DESTROY: {
        // 退出前保存窗口位置
        RECT rc = {};
        GetWindowRect(hwnd, &rc);
        {
            std::lock_guard<std::mutex> lock(AppState::cfgMutex);
            AppState::cfg.x = rc.left;
            AppState::cfg.y = rc.top;
            saveConfig();
        }
        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ════════════════
// 程序入口
int APIENTRY WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int) {
    loadConfig();

    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    int w, h, x, y;
    {
        std::lock_guard<std::mutex> lock(AppState::cfgMutex);
        w = AppState::cfg.w;
        h = AppState::cfg.h;
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        if (AppState::cfg.x >= 0 && AppState::cfg.y >= 0) {
            x = AppState::cfg.x;
            y = AppState::cfg.y;
        } else {
            x = (sw - w) / 2;
            y = (sh - h) / 2;
        }
    }

    // 创建悬浮窗（置顶 + 分层 + 工具窗口 + 不抢焦点）
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        CLASS_NAME, L"BatteryOverlay",
        WS_POPUP | WS_VISIBLE,
        x, y, w, h,
        nullptr, nullptr, hinst, nullptr);

    if (!hwnd) return 1;

    render(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
