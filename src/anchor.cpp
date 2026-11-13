#include <windows.h>

static LRESULT CALLBACK AnchorProc(HWND window, UINT message, WPARAM word, LPARAM parameter) {
    if (message == WM_CLOSE || message == WM_APP + 1) {
        ShowWindow(window, SW_HIDE);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC context = BeginPaint(window, &paint);
        RECT area;
        GetClientRect(window, &area);
        area.left += 16;
        area.top += 12;
        DrawTextW(context, L"此显示器正在共享给鹅次元\n游戏、浏览器和通知可能上传到云端。\n关闭此窗口暂停画面，保留陪玩入口。", -1, &area, DT_LEFT | DT_WORDBREAK);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, word, parameter);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HWND existing = FindWindowW(L"EciyuanDesktopCompanionAnchor", nullptr);
    if (existing) {
        ShowWindow(existing, SW_SHOWNORMAL);
        SetForegroundWindow(existing);
        return 0;
    }
    WNDCLASSW definition = {};
    definition.lpfnWndProc = AnchorProc;
    definition.hInstance = instance;
    definition.lpszClassName = L"EciyuanDesktopCompanionAnchor";
    definition.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&definition)) return 1;
    HWND window = CreateWindowExW(0, definition.lpszClassName, L"全屏陪玩 · 正在共享", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        30, 30, 480, 140, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
