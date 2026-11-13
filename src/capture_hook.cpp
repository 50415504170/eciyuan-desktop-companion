#include <windows.h>
#include <unknwn.h>
#include <stdint.h>
#include <string.h>

static HWND WINAPI SharedWindow(int, HWND) {
    HWND window = FindWindowW(L"EciyuanDesktopCompanionAnchor", nullptr);
    return window && IsWindowVisible(window) ? window : nullptr;
}

static HRESULT STDMETHODCALLTYPE CaptureMonitor(void* factory, HWND window, REFIID iid, void** result) {
    typedef HRESULT (STDMETHODCALLTYPE *CreateMonitor)(void*, HMONITOR, REFIID, void**);
    void** table = *(void***)factory;
    return ((CreateMonitor)table[4])(factory,
        MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), iid, result);
}

static bool WriteCode(BYTE* destination, const BYTE* source, size_t size) {
    DWORD protection;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &protection)) return false;
    memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    DWORD unused;
    VirtualProtect(destination, size, protection, &unused);
    return true;
}

static bool InstallHook() {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* headers = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (headers->OptionalHeader.SizeOfImage < 0xc680000) return false;
    BYTE* lookup = base + 0x4456d0;
    BYTE* callsite = base + 0x54ae47;
    const BYTE lookupOriginal[] = {0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
    const BYTE callOriginal[] = {0x49,0x8b,0x06,0x4c,0x8b,0x78,0x18};
    if (memcmp(lookup, lookupOriginal, sizeof(lookupOriginal)) ||
        memcmp(callsite, callOriginal, sizeof(callOriginal))) return false;
    BYTE* trampoline = nullptr;
    uintptr_t aligned = (uintptr_t)callsite & ~(uintptr_t)0xffff;
    for (uintptr_t distance = 0x10000; distance < 0x70000000; distance += 0x10000) {
        trampoline = (BYTE*)VirtualAlloc((void*)(aligned + distance), 4096,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (trampoline) break;
    }
    if (!trampoline) return false;
    BYTE jump[] = {0xff,0x25,0,0,0,0,0,0,0,0,0,0,0,0};
    void* capture = (void*)&CaptureMonitor;
    memcpy(jump + 6, &capture, sizeof(capture));
    memcpy(trampoline, jump, sizeof(jump));
    DWORD protection;
    if (!VirtualProtect(trampoline, 4096, PAGE_EXECUTE_READ, &protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, sizeof(jump));
    BYTE load[] = {0x4c,0x8d,0x3d,0,0,0,0};
    LONG relative = (LONG)(trampoline - (callsite + sizeof(load)));
    memcpy(load + 3, &relative, sizeof(relative));
    void* find = (void*)&SharedWindow;
    memcpy(jump + 6, &find, sizeof(find));
    if (!WriteCode(callsite, load, sizeof(load))) return false;
    if (!WriteCode(lookup, jump, sizeof(jump))) {
        WriteCode(callsite, callOriginal, sizeof(callOriginal));
        return false;
    }
    return true;
}

static DWORD WINAPI Initialize(LPVOID parameter) {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW((HMODULE)parameter, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return 1;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash || slash - path + 16 >= MAX_PATH) return 1;
    wcscpy(slash + 1, L"zlib1_real.dll");
    if (!LoadLibraryW(path)) return 1;
    return InstallHook() ? 0 : 1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, Initialize, instance, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
