#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>

static HWND pathEdit, statusText, enableButton, inspectButton, restoreButton;
static HANDLE operation;
static std::wstring runtime;

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

static void StartOperation(HWND parent, const wchar_t* action) {
    if (operation) return;
    wchar_t path[MAX_PATH];
    GetWindowTextW(pathEdit, path, MAX_PATH);
    if (!*path) {
        SetWindowTextW(statusText, L"请先选择鹅次元安装目录中的 Eciyuan.exe。 ");
        return;
    }
    runtime = RuntimePath(wcscmp(action, L"Inspect") != 0);
    if (runtime.empty()) {
        SetWindowTextW(statusText, L"无法定位运行目录。");
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
        SetWindowTextW(statusText, L"操作未启动（可能取消了管理员提示）。");
        return;
    }
    operation = launch.hProcess;
    EnableWindow(enableButton, FALSE);
    EnableWindow(inspectButton, FALSE);
    EnableWindow(restoreButton, FALSE);
    SetWindowTextW(statusText, L"正在处理，请稍候。开启和还原操作会重启鹅次元、断开当前通话。");
}

static HWND Control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
                    int x, int y, int width, int height, int identifier = 0) {
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, parent, (HMENU)(intptr_t)identifier, nullptr, nullptr);
    SendMessageW(control, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return control;
}

static LRESULT CALLBACK LauncherProc(HWND window, UINT message, WPARAM word, LPARAM parameter) {
    if (message == WM_CREATE) {
        Control(window, L"STATIC", L"全屏陪玩  0.1.0 · 实验版", 0, 24, 20, 620, 28);
        Control(window, L"STATIC", L"支持鹅次元 0.1.15.624 / Windows x64。共享小窗口所在的单个显示器。", 0, 24, 55, 620, 34);
        pathEdit = Control(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 24, 98, 500, 28);
        Control(window, L"BUTTON", L"选择程序…", BS_PUSHBUTTON | WS_TABSTOP, 540, 96, 112, 32, 10);
        inspectButton = Control(window, L"BUTTON", L"检测兼容性", BS_PUSHBUTTON | WS_TABSTOP, 24, 147, 140, 42, 11);
        enableButton = Control(window, L"BUTTON", L"开启 / 重启陪玩", BS_PUSHBUTTON | WS_TABSTOP, 184, 147, 150, 42, 12);
        Control(window, L"BUTTON", L"停止共享画面", BS_PUSHBUTTON | WS_TABSTOP, 354, 147, 140, 42, 13);
        restoreButton = Control(window, L"BUTTON", L"还原并重启", BS_PUSHBUTTON | WS_TABSTOP, 514, 147, 140, 42, 14);
        Control(window, L"STATIC", L"开启和还原会重启鹅次元，中断当前通话。停止共享只隐藏采集窗口。\n共享期间，聊天、通知、浏览器和游戏画面均可能上传至鹅次元云端。\n本工具不提供 AI 服务。模型识别、语音、费用及抽帧频率由鹅次元决定。", 0, 24, 214, 630, 78);
        statusText = Control(window, L"STATIC", L"就绪。请先检测兼容性。", SS_LEFT, 24, 316, 630, 66);
        wchar_t candidate[MAX_PATH];
        for (wchar_t drive = L'C'; drive <= L'Z'; ++drive) {
            swprintf(candidate, MAX_PATH, L"%c:\\Eciyuan\\0.1.15.624\\Eciyuan.exe", drive);
            if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
                SetWindowTextW(pathEdit, candidate);
                break;
            }
        }
        SetTimer(window, 1, 500, nullptr);
        return 0;
    }
    if (message == WM_COMMAND) {
        switch (LOWORD(word)) {
        case 10: {
            wchar_t file[MAX_PATH] = L"";
            OPENFILENAMEW picker = {sizeof(picker)};
            picker.hwndOwner = window;
            picker.lpstrFilter = L"鹅次元主程序\0Eciyuan.exe\0";
            picker.lpstrFile = file;
            picker.nMaxFile = MAX_PATH;
            picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&picker)) SetWindowTextW(pathEdit, file);
            break;
        }
        case 11: StartOperation(window, L"Inspect"); break;
        case 12: StartOperation(window, L"Enable"); break;
        case 13: {
            HWND anchor = FindWindowW(L"EciyuanDesktopCompanionAnchor", nullptr);
            if (anchor) PostMessageW(anchor, WM_APP + 1, 0, 0);
            SetWindowTextW(statusText, L"已请求暂停共享画面。保留入口进程，不主动挂断；鹅次元通常约 1 秒后停止取帧。");
            break;
        }
        case 14: StartOperation(window, L"Restore"); break;
        }
        return 0;
    }
    if (message == WM_TIMER && operation && WaitForSingleObject(operation, 0) == WAIT_OBJECT_0) {
        CloseHandle(operation);
        operation = nullptr;
        EnableWindow(enableButton, TRUE);
        EnableWindow(inspectButton, TRUE);
        EnableWindow(restoreButton, TRUE);
        SetWindowTextW(statusText, ReadReport().c_str());
        return 0;
    }
    if (message == WM_DESTROY) {
        if (operation) CloseHandle(operation);
        PostQuitMessage(0);
    }
    return DefWindowProcW(window, message, word, parameter);
}

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
    WNDCLASSW definition = {};
    definition.lpfnWndProc = LauncherProc;
    definition.hInstance = instance;
    definition.lpszClassName = L"EciyuanDesktopCompanionLauncher";
    definition.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&definition)) return 1;
    HWND window = CreateWindowExW(0, definition.lpszClassName, L"全屏陪玩 · Eciyuan Desktop Companion",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        700, 440, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, SW_SHOW);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
