#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include "ntp_sync.h"

#pragma comment(lib, "ws2_32.lib")

// NTP 数据包结构（48 字节）
#pragma pack(push, 1)
struct NTPPacket {
    BYTE leapVersionMode;  // Leap Indicator + Version + Mode
    BYTE stratum;           // 层级
    BYTE poll;              // 轮询间隔
    BYTE precision;         // 精度
    DWORD rootDelay;        // 根延迟
    DWORD rootDispersion;   // 根离散
    DWORD refId;            // 参考 ID
    DWORD refTimestamp[2];  // 参考时间戳
    DWORD origTimestamp[2]; // 原始时间戳
    DWORD recvTimestamp[2]; // 接收时间戳
    DWORD transTimestamp[2];// 传输时间戳
};
#pragma pack(pop)

// NTP 时间转换为 Unix 时间戳
static time_t ntpToUnix(DWORD ntpSeconds) {
    // NTP 时间戳从 1900-01-01，Unix 从 1970-01-01
    // 差值：2208988800 秒
    return (time_t)(ntpSeconds - 2208988800ULL);
}

// 全局状态
static std::atomic<bool> g_ntpRunning{false};
static std::atomic<HWND> g_notifyWnd{nullptr};
static NtpResult g_result;
static std::mutex g_resultMutex;

bool initWinsock() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

void cleanupWinsock() {
    WSACleanup();
}

void getSystemTimeZone(int& bias, std::wstring& name) {
    TIME_ZONE_INFORMATION tz;
    DWORD result = GetTimeZoneInformation(&tz);
    if (result != TIME_ZONE_ID_INVALID) {
        bias = tz.Bias;
        name = std::wstring(tz.StandardName);
    }
}

std::optional<NtpResult> syncNtpTime(const char* server, int timeoutMs) {
    SOCKET sock = INVALID_SOCKET;
    NtpResult result;
    result.status = NtpStatus::Syncing;

    // 创建 UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        return {};
    }

    // 设置超时
    DWORD timeout = timeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    // 解析服务器地址
    struct addrinfo hints = {}, *addr = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(server, "123", &hints, &addr) != 0) {
        closesocket(sock);
        return {};
    }

    // 构造 NTP 请求包
    NTPPacket packet = {};
    packet.leapVersionMode = (0 << 6) | (4 << 3) | 3; // LI=0, VN=4, Mode=3 (Client)

    // 发送请求
    int sent = sendto(sock, (const char*)&packet, sizeof(packet), 0,
               addr->ai_addr, (int)addr->ai_addrlen);
    if (sent == SOCKET_ERROR) {
        freeaddrinfo(addr);
        closesocket(sock);
        return {};
    }

    // 接收响应
    char response[48];
    struct sockaddr_in from;
    int fromLen = sizeof(from);
    DWORD startTime = GetTickCount();

    int recvLen = recvfrom(sock, response, sizeof(response), 0,
                          (struct sockaddr*)&from, &fromLen);
    DWORD endTime = GetTickCount();

    freeaddrinfo(addr);
    closesocket(sock);

    if (recvLen == SOCKET_ERROR || recvLen < 48) {
        return {};
    }

    // 解析 NTP 响应
    NTPPacket* ntpResp = (NTPPacket*)response;
    DWORD ntpSeconds = ntohl(ntpResp->transTimestamp[0]);

    // 获取系统时区
    getSystemTimeZone(result.timezoneBias, result.timezoneName);

    // 计算延迟
    result.delay = endTime - startTime;
    result.ntpTime = ntpToUnix(ntpSeconds);
    result.status = NtpStatus::Success;

    return result;
}

void ntpSyncThread(HWND notifyWnd) {
    g_ntpRunning.store(true);

    NtpResult result;
    result.status = NtpStatus::Syncing;

    // 尝试最多 2 次
    for (int i = 0; i < 2; i++) {
        auto optResult = syncNtpTime();
        if (optResult.has_value()) {
            result = *optResult;
            break;
        }
        if (i < 1) Sleep(1000); // 重试前等待 1 秒
    }

    if (result.status != NtpStatus::Success) {
        result.status = NtpStatus::Failed;
        result.delay = 0;
        result.ntpTime = 0;
    }

    // 保存结果
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = result;
    }

    // 通知主窗口
    if (notifyWnd && IsWindow(notifyWnd)) {
        PostMessageW(notifyWnd, WM_USER + 100, 0, 0);
    }

    g_ntpRunning.store(false);
}

void startNtpSyncAsync(HWND notifyWnd) {
    if (g_ntpRunning.load()) return;

    g_notifyWnd.store(notifyWnd);

    // 初始化 Winsock
    if (!initWinsock()) {
        NtpResult result;
        result.status = NtpStatus::Failed;
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            g_result = result;
        }
        if (notifyWnd && IsWindow(notifyWnd)) {
            PostMessageW(notifyWnd, WM_USER + 100, 0, 0);
        }
        return;
    }

    // 启动后台线程
    std::thread(ntpSyncThread, notifyWnd).detach();
}

NtpResult getNtpResult() {
    std::lock_guard<std::mutex> lock(g_resultMutex);
    return g_result;
}

void stopNtpSync() {
    // 简单实现：等待线程结束
    while (g_ntpRunning.load()) {
        Sleep(100);
    }
    cleanupWinsock();
}

void showNtpBalloon(HWND hwnd, const std::wstring& title,
                     const std::wstring& text, DWORD timeoutMs) {
    // 创建托盘图标（如果还没有）
    static bool trayAdded = false;
    static NOTIFYICONDATAW nid = {};

    HICON hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(101));

    if (!trayAdded) {
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;
        nid.hIcon = hIcon;
        wcscpy_s(nid.szTip, L"Battery Overlay");
        nid.dwInfoFlags = NIIF_INFO;
        nid.uTimeout = timeoutMs;

        Shell_NotifyIconW(NIM_ADD, &nid);
        trayAdded = true;
    } else {
        nid.hIcon = hIcon;
    }

    // 更新气泡内容
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, title.c_str());
    wcscpy_s(nid.szInfo, text.c_str());
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout = timeoutMs;

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
