#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修改 render() 函数：
1. 移除 getInputLang() 调用
2. 第一行显示充电/电池图标，不使用字体颜色
3. 修改默认窗口大小为 33x33
"""
import sys

file_path = r"D:\Program Files\WorkBuddy工作空间\battery_overlay_cpp\main.cpp"

with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

# ========== 1. 修改渲染逻辑 ==========
old_render = '''        // 正常模式：第一行输入法状态+充电标识，第二行电池百分比
        // 台式机(BatteryLifePercent==255)时不显示充电⚡
        std::wstring lang = getInputLang();
        bool isDesktop = (sps.BatteryLifePercent == 255);
        std::wstring display = (charging && !isDesktop) ? lang + L"\\u26A1" : lang;
        RECT r1 = { 2, 0, w, h / 2 };
        DrawTextW(hdc, display.c_str(), static_cast<int>(display.size()), &r1,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);'''

new_render = '''        // 正常模式：第一行充电/电池图标，第二行电池百分比
        bool isDesktop = (sps.BatteryLifePercent == 255);
        
        // 第一行：图标（不使用字体颜色，使用系统默认色）
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        std::wstring icon = L"";
        if (isDesktop) {
            icon = L"\\U0001F50C";  // 🔌
        } else if (charging) {
            icon = L"\\u26A1";  // ⚡
        } else {
            icon = L"\\U0001F50B";  // 🔋
        }
        RECT r1 = { 2, 0, w, h / 2 };
        DrawTextW(hdc, icon.c_str(), static_cast<int>(icon.size()), &r1,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        
        // 恢复字体颜色用于第二行
        SetTextColor(hdc, fc);'''

if old_render in content:
    content = content.replace(old_render, new_render)
    print("  ✓ Modified render() logic - first line shows icons")
else:
    print("  ✗ Cannot find old render logic")
    # 尝试查找并手动修改
    # 查找 "std::wstring lang = getInputLang();"
    if 'std::wstring lang = getInputLang();' in content:
        print("  → Found getInputLang() call, will replace manually")
        # 使用更灵活的方式
        import re
        # 匹配整个块并替换
        pattern = r'// 正常模式：第一行输入法状态\+充电标识.*?DrawTextW\(hdc, display\.c_str\(\).*?DT_VCENTER\);'
        replacement = '''// 正常模式：第一行充电/电池图标，第二行电池百分比
        bool isDesktop = (sps.BatteryLifePercent == 255);
        
        // 第一行：图标（不使用字体颜色，使用系统默认色）
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        std::wstring icon = L"";
        if (isDesktop) {
            icon = L"\\U0001F50C";  // 🔌
        } else if (charging) {
            icon = L"\\u26A1";  // ⚡
        } else {
            icon = L"\\U0001F50B";  // 🔋
        }
        RECT r1 = { 2, 0, w, h / 2 };
        DrawTextW(hdc, icon.c_str(), static_cast<int>(icon.size()), &r1,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        
        // 恢复字体颜色用于第二行
        SetTextColor(hdc, fc);'''
        content = re.sub(pattern, replacement, content, flags=re.DOTALL)
        print("  ✓ Modified render() logic using regex")

# ========== 2. 修改默认窗口大小 ==========
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
    print("  ✗ Cannot find Config::defaults()")
    # 可能是格式问题，尝试查找 "return { -1, -1,"
    if 'return { -1, -1,' in content:
        print("  → Found Config::defaults(), manual edit needed")

# 写回文件
with open(file_path, 'w', encoding='utf-8') as f:
    f.write(content)

print("\n✅ All modifications applied!")
print("\nNext: Compile and test the code")
