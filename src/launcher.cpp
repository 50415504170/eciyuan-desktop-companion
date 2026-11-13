#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <gdiplus.h>
#include <cmath>

static HWND pathEdit, statusText, enableButton, inspectButton, restoreButton, pauseButton, browseButton;
static HANDLE operation;
static std::wstring runtime;
static HFONT bodyFont;
static HBRUSH fieldBrush;
static float uiScale = 1.0f;

static bool Resource(int identifier, const BYTE** data, DWORD* size) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC entry = FindResourceW(module, MAKEINTRESOURCEW(identifier), RT_RCDATA);
    if (!entry) return false;
    *size = SizeofResource(module, entry);
    *data = (const BYTE*)LockResource(LoadResource(module, entry));
    return *data && *size;
}

static bool Extract(int identifier, const wchar_t* name) {
    const BYTE* data;
    DWORD size;
    if (!Resource(identifier, &data, &size)) return false;
    std::wstring path = runtime + L"\\" + name;
    HANDLE existing = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (existing != INVALID_HANDLE_VALUE) {
        DWORD length = GetFileSize(existing, nullptr), read = 0;
        std::vector<BYTE> previous(length == size ? length : 0);
        bool matches = length == size && ReadFile(existing, previous.data(), length, &read, nullptr) &&
            read == size && memcmp(previous.data(), data, size) == 0;
        CloseHandle(existing);
        if (matches) return true;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    bool saved = WriteFile(file, data, size, &written, nullptr) && written == size;
    CloseHandle(file);
    return saved;
}

static std::wstring RuntimePath(bool privileged) {
    wchar_t local[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, privileged ? CSIDL_PROGRAM_FILES : CSIDL_LOCAL_APPDATA,
        nullptr, SHGFP_TYPE_CURRENT, local))) return L"";
    return std::wstring(local) + L"\\EciyuanDesktopCompanion\\0.1.0";
}

static bool Prepare(bool privileged) {
    runtime = RuntimePath(privileged);
    if (runtime.empty()) return false;
    std::wstring parent = runtime.substr(0, runtime.find_last_of(L'\\'));
    if (!CreateDirectoryW(parent.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    if (!CreateDirectoryW(runtime.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    return Extract(101, L"capture.dll") && Extract(102, L"Valorant.exe") && Extract(103, L"manage.ps1");
}

static int Worker(const wchar_t* action, const wchar_t* path) {
    bool privileged = wcscmp(action, L"Inspect") != 0;
    if (privileged && !IsUserAnAdmin()) return 1;
    if (!Prepare(privileged)) return 2;
    DeleteFileW((runtime + L"\\result.txt").c_str());
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    std::wstring executable = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring command = L"\"" + executable + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" +
        runtime + L"\\manage.ps1\" -Action " + action + L" -EciyuanExe \"" + path + L"\"";
    std::vector<wchar_t> writable(command.begin(), command.end());
    writable.push_back(0);
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(executable.c_str(), writable.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, runtime.c_str(), &startup, &process)) return 3;
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return (int)code;
}

static std::wstring ReadReport() {
    std::wstring path = runtime + L"\\result.txt";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return L"操作完成，但没有返回状态。请检查鹅次元。";
    char buffer[8192] = {};
    DWORD read;
    bool success = ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
    CloseHandle(file);
    if (!success || !read) return L"无法读取操作状态。";
    int length = MultiByteToWideChar(CP_UTF8, 0, buffer, read, nullptr, 0);
    std::wstring text(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buffer, read, &text[0], length);
    if (!text.empty() && text[0] == 0xfeff) text.erase(0, 1);
    return text;
}

static void SetStatus(const wchar_t* text);

static void StartOperation(HWND parent, const wchar_t* action) {
    if (operation) return;
    wchar_t path[MAX_PATH];
    GetWindowTextW(pathEdit, path, MAX_PATH);
    if (!*path) {
        SetStatus(L"请先选择鹅次元安装目录中的 Eciyuan.exe。 ");
        return;
    }
    runtime = RuntimePath(wcscmp(action, L"Inspect") != 0);
    if (runtime.empty()) {
        SetStatus(L"无法定位运行目录。");
        return;
    }
    wchar_t executable[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return;
    std::wstring parameters = std::wstring(L"--worker ") + action + L" \"" + path + L"\"";
    SHELLEXECUTEINFOW launch = {sizeof(launch)};
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    launch.hwnd = parent;
    launch.lpVerb = wcscmp(action, L"Inspect") ? L"runas" : L"open";
    launch.lpFile = executable;
    launch.lpParameters = parameters.c_str();
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch)) {
        SetStatus(L"操作未启动（可能取消了管理员提示）。");
        return;
    }
    operation = launch.hProcess;
    EnableWindow(enableButton, FALSE);
    EnableWindow(inspectButton, FALSE);
    EnableWindow(restoreButton, FALSE);
    EnableWindow(pauseButton, FALSE);
    EnableWindow(browseButton, FALSE);
    EnableWindow(pathEdit, FALSE);
    SetStatus(wcscmp(action, L"Inspect") == 0 ? L"正在校验程序文件…" : L"正在处理，请稍候…");
    InvalidateRect(parent, nullptr, FALSE);
}

#include "launcher_ui.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int count;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 1;
    if (count == 4 && !wcscmp(arguments[1], L"--worker")) {
        if (wcscmp(arguments[2], L"Inspect") && wcscmp(arguments[2], L"Enable") && wcscmp(arguments[2], L"Restore")) {
            LocalFree(arguments);
            return 1;
        }
        int code = Worker(arguments[2], arguments[3]);
        LocalFree(arguments);
        return code;
    }
    bool selfTest = count == 2 && !wcscmp(arguments[1], L"--self-test");
    LocalFree(arguments);
    if (selfTest) {
        const BYTE* data;
        DWORD size;
        for (int identifier = 101; identifier <= 103; ++identifier) {
            if (!Resource(identifier, &data, &size) || size < 100) return 1;
            if (identifier < 103 && (data[0] != 'M' || data[1] != 'Z')) return 2;
        }
        return 0;
    }
    InitializeVisuals();
    WNDCLASSW definition = {};
    definition.lpfnWndProc = LauncherProc;
    definition.hInstance = instance;
    definition.lpszClassName = L"EciyuanDesktopCompanionLauncherV3";
    definition.hbrBackground = nullptr;
    definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&definition)) return 10;
    HWND window = CreateWindowExW(0, definition.lpszClassName, L"全屏陪玩 · Eciyuan Desktop Companion",
        WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        Pixels(620), Pixels(566), nullptr, nullptr, instance, nullptr);
    if (!window) return (int)GetLastError();
    ShowWindow(window, SW_SHOW);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    GdiplusShutdown(graphicsToken);
    return 0;
}
