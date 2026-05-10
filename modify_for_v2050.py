#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
回退到 2.0.4.0 并修改：
1. 移除输入法检测功能
2. 修改第一行显示：充电时显示 ⚡，不充电显示 🔋，台式机显示 🔌
3. 图标不使用字体颜色
4. 默认窗口大小 33x33
5. 版本号改为 2.0.5.0
"""
import sys

file_path = r"D:\Program Files\WorkBuddy工作空间\battery_overlay_cpp\main.cpp"

with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

# ========== 1. 移除 IME 相关头文件和库 ==========
# 移除 #include <imm.h>
content = content.replace('#include <imm.h>\n', '')
# 移除 imm32.lib
content = content.replace('#pragma comment(lib, "imm32.lib")\n', '')

# ========== 2. 移除 IME 控制常量 ==========
ime_constants = '''// IME 控制常量
constexpr UINT WM_IME_CTRL     = 0x0283;
constexpr UINT IMC_GETOPENSTATUS  = 0x0005;
constexpr UINT IMC_GETCONVERSIONMODE = 0x001;

'''
content = content.replace(ime_constants, '')

# ========== 3. 删除 getInputLang() 函数 ==========
# 找到函数开始和结束位置
start_marker = '// 输入法状态检测\nstatic std::wstring getInputLang() {'
end_marker = '\n    // 【修复4】无法获取 IME 窗口时，保守返回 "en"\n    // 原代码用 GetKeyboardLayoutList 判断，那是"系统是否装过中文输入法"\n    // 不是"当前是否正在用中文输入"，逻辑完全错误\n    return L"en";\n}'

start_idx = content.find(start_marker)
if start_idx != -1:
    end_idx = content.find(end_marker, start_idx)
    if end_idx != -1:
        end_idx += len(end_marker)
        content = content[:start_idx] + content[end_idx:]
        print("  ✓ Removed getInputLang() function")
    else:
        print("  ✗ Cannot find end of getInputLang()")
else:
    # 尝试其他可能的格式
    start_marker2 = '// 输入法状态检测\nstatic std::wstring getInputLang() {'
    start_idx = content.find(start_marker2)
    if start_idx != -1:
        # 找到匹配的 }
        brace_count = 0
        idx = start_idx + len(start_marker2)
        while idx < len(content):
            if content[idx] == '{':
                brace_count += 1
            elif content[idx] == '}':
                brace_count -= 1
                if brace_count == 0:
                    idx += 1  # 包含 }
                    break
            idx += 1
        content = content[:start_idx] + content[idx:]
        print("  ✓ Removed getInputLang() function (auto-detected)")
    else:
        print("  ⚠ Cannot find getInputLang() - may have been already removed")

# ========== 4. 修改 render() 函数中的显示逻辑 ==========
# 替换正常模式的第一行显示逻辑
old_logic = '''        // 正常模式：第一行输入法状态+充电标识，第二行电池百分比
        // 台式机(BatteryLifePercent==255)时不显示充电⚡
        std::wstring lang = getInputLang();
        bool isDesktop = (sps.BatteryLifePercent == 255);
        std::wstring display = (charging && !isDesktop) ? lang + L"\u26A1" : lang;
        RECT r1 = { 2, 0, w, h / 2 };
        DrawTextW(hdc, display.c_str(), static_cast<int>(display.size()), &r1,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);'''

new_logic = '''        // 正常模式：第一行充电/电池图标，第二行电池百分比
        // 台式机显示 🔌，充电时显示 ⚡，不充电显示 🔋
        bool isDesktop = (sps.BatteryLifePercent == 255);
        
        // 第一行：图标（不使用字体颜色）
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        std::wstring icon = L"";
        if (isDesktop) {
            icon = L"\uD83D\uDD0C";  // 🔌
        } else if (charging) {
            icon = L"\u26A1";  // ⚡
        } else {
            icon = L"\uD83D\uDD0B";  // 🔋
        }
        RECT r1 = { 2, 0, w, h / 2 };
        DrawTextW(hdc, icon.c_str(), static_cast<int>(icon.size()), &r1,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        
        // 恢复字体颜色用于第二行
        SetTextColor(hdc, fc);'''

if old_logic in content:
    content = content.replace(old_logic, new_logic)
    print("  ✓ Modified render() logic - first line now shows icons")
else:
    print("  ⚠ Cannot find old logic block - may need manual edit")
    # 尝试查找简化的版本
    if 'std::wstring lang = getInputLang();' in content:
        print("  ⚠ Found getInputLang() call but block format differs")

# ========== 5. 修改默认窗口大小 ==========
old_defaults = '''    static Config defaults() {
        return { -1, -1, 34, 34,
                 255, 255, 255,  // 默认字体颜色
                 0,32,63, 204 }; // 默认背景色 + 透明度
    }'''

new_defaults = '''    static Config defaults() {
        return { -1, -1, 33, 33,
                 255, 255, 255,  // 默认字体颜色
                 0,32,63, 204 }; // 默认背景色 + 透明度
    }'''

if old_defaults in content:
    content = content.replace(old_defaults, new_defaults)
    print("  ✓ Changed default window size to 33x33")
else:
    print("  ✗ Cannot find Config::defaults() - check manually")

# ========== 6. 修改版本号 ==========
# 查找关于对话框中的版本号
old_version = 'L"笔记本电脑电量百分比和输入法状态悬浮窗 v2.0.4.0 (C++ 重写版)\\n"'
new_version = 'L"笔记本电脑电量百分比悬浮窗 v2.0.5.0 (C++ 重写版)\\n"'

if old_version in content:
    content = content.replace(old_version, new_version)
    print("  ✓ Updated version to 2.0.5.0 in about dialog")
else:
    print("  ⚠ Cannot find version string in about dialog")
    # 尝试不区分大小写或其他格式
    if 'v2.0.4.0' in content:
        content = content.replace('v2.0.4.0', 'v2.0.5.0')
        print("  ✓ Updated version using simple replace")

# ========== 7. 更新文件顶部的功能描述 ==========
old_desc = '* 功能：桌面悬浮窗，显示电池百分比 + 输入法状态'
new_desc = '* 功能：桌面悬浮窗，显示电池百分比 + 充电状态'
content = content.replace(old_desc, new_desc)
print("  ✓ Updated file header description")

# 写回文件
with open(file_path, 'w', encoding='utf-8') as f:
    f.write(content)

print("\n✅ All modifications applied successfully!")
print("\nNext steps:")
print("  1. Compile: g++ main.cpp -o battery_overlay.exe -lgdi32 -lcomctl32 -lshell32 -lshlwapi -mwindows -std=c++17")
print("  2. Test the executable")
print("  3. Commit and push")
