#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include <windows.h>
#include <string>
#include <optional>
#include <atomic>

// NTP 同步状态
enum class NtpStatus {
    Idle,           // 未开始
    Syncing,        // 同步中
    Success,        // 成功
    Failed          // 失败
};

// NTP 同步结果
struct NtpResult {
    NtpStatus status = NtpStatus::Idle;
    time_t ntpTime = 0;           // NTP 时间（秒时间戳）
    DWORD delay = 0;              // NTP 请求延迟（毫秒）
    int timezoneBias = 0;        // 时区偏移（分钟）
    std::wstring timezoneName;    // 时区名称
    LONGLONG steadyCount = 0;    // 授时成功时的性能计数器值
    double offsetSec = 0.0;      // 时间偏移量（秒，正=本地快，负=本地慢）
};

// 初始化 Winsock
bool initWinsock();

// 清理 Winsock
void cleanupWinsock();

// 获取系统时区信息
void getSystemTimeZone(int& bias, std::wstring& name);

// NTP 授时（同步版本，内部使用）
std::optional<NtpResult> syncNtpTime(const char* server = "ntp3.aliyun.com",
                                       int timeoutMs = 5000);

// 启动异步 NTP 授时
void startNtpSyncAsync(HWND notifyWnd);

// 获取 NTP 同步结果
NtpResult getNtpResult();

// 停止 NTP 同步线程
void stopNtpSync();

// 显示气泡提示
void showNtpBalloon(HWND hwnd, const std::wstring& title,
                     const std::wstring& text, DWORD timeoutMs);

#endif // NTP_SYNC_H
