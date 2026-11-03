// ==WindhawkMod==
// @id              toast-probe
// @name            Toast probe
// @description     Logs the UI Automation tree of every toast banner (research tool, changes nothing)
// @version         0.1
// @author          Wheatlay
// @github          https://github.com/Wheatlay
// @include         windhawk.exe
// @compilerOptions -lole32 -loleaut32 -ldwmapi
// ==/WindhawkMod==

// Banner detection and the tool-mod boilerplate come from the "Toast auto-dismiss" mod by nikitor32 (GPL-3.0),
// so this file is GPL-3.0 as well.

#include <windows.h>
#include <dwmapi.h>
#include <uiautomation.h>

#include <string.h>

WINUSERAPI UINT WINAPI GetDpiForWindow(HWND);

constexpr DWORD kPollIntervalMs = 250;
constexpr int kMaxDepth = 14;

HANDLE g_stopEvent = nullptr;
HANDLE g_threadHandle = nullptr;

bool IsShellExperienceHost(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    bool result = false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        WCHAR path[MAX_PATH] = {};
        DWORD size = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(h, 0, path, &size)) {
            const WCHAR* base = wcsrchr(path, L'\\');
            result = _wcsicmp(base ? base + 1 : path, L"ShellExperienceHost.exe") == 0;
        }
        CloseHandle(h);
    }
    return result;
}

// Same rules as Toast auto-dismiss: a short, narrow CoreWindow of ShellExperienceHost in the bottom right corner.
bool GetToastRectIfBanner(HWND hwnd, RECT* out) {
    if (!IsWindowVisible(hwnd)) {
        return false;
    }
    WCHAR className[128] = {};
    if (!GetClassNameW(hwnd, className, ARRAYSIZE(className)) ||
        _wcsicmp(className, L"Windows.UI.Core.CoreWindow") != 0) {
        return false;
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        return false;
    }
    if (!IsShellExperienceHost(hwnd)) {
        return false;
    }
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) {
        return false;
    }
    UINT dpi = GetDpiForWindow(hwnd);
    if (!dpi) {
        dpi = USER_DEFAULT_SCREEN_DPI;
    }
    auto px = [dpi](int value) { return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI); };
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        return false;
    }
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int maxHeight = px(700);
    int halfMonitorHeight = (mi.rcMonitor.bottom - mi.rcMonitor.top) / 2;
    if (maxHeight > halfMonitorHeight) {
        maxHeight = halfMonitorHeight;
    }
    if (height < px(80) || height > maxHeight || width < px(150) || width > px(600)) {
        return false;
    }
    int overhang = rc.right - mi.rcMonitor.right;
    if (overhang < -px(8) || overhang > px(8)) {
        return false;
    }
    if (rc.bottom < mi.rcWork.bottom - px(500) || rc.bottom > mi.rcMonitor.bottom + px(8)) {
        return false;
    }
    *out = rc;
    return true;
}

HWND FindToastBanner(RECT* out) {
    HWND hwnd = nullptr;
    while ((hwnd = FindWindowExW(HWND_DESKTOP, hwnd, nullptr, nullptr)) != nullptr) {
        if (GetToastRectIfBanner(hwnd, out)) {
            return hwnd;
        }
    }
    return nullptr;
}

void LogElement(IUIAutomationElement* element, int depth) {
    CONTROLTYPEID type = 0;
    element->get_CurrentControlType(&type);
    BSTR name = nullptr, id = nullptr, cls = nullptr;
    element->get_CurrentName(&name);
    element->get_CurrentAutomationId(&id);
    element->get_CurrentClassName(&cls);
    RECT rc{};
    element->get_CurrentBoundingRectangle(&rc);
    BOOL invokable = FALSE;
    VARIANT v{};
    if (SUCCEEDED(element->GetCurrentPropertyValue(UIA_IsInvokePatternAvailablePropertyId, &v)) && v.vt == VT_BOOL) {
        invokable = v.boolVal != VARIANT_FALSE;
    }
    VariantClear(&v);
    Wh_Log(L"%*s[%d] type=%d id=\"%s\" class=\"%s\" invoke=%d rect=%d,%d %dx%d name=\"%s\"", depth * 2, L"", depth,
           type, id ? id : L"", cls ? cls : L"", invokable, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
           name ? name : L"");
    SysFreeString(name);
    SysFreeString(id);
    SysFreeString(cls);
}

void Dump(IUIAutomationTreeWalker* walker, IUIAutomationElement* element, int depth) {
    LogElement(element, depth);
    if (depth >= kMaxDepth) {
        return;
    }
    IUIAutomationElement* child = nullptr;
    walker->GetFirstChildElement(element, &child);
    while (child) {
        Dump(walker, child, depth + 1);
        IUIAutomationElement* next = nullptr;
        walker->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

DWORD WINAPI PollThread(LPVOID) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        return 0;
    }
    IUIAutomation* uia = nullptr;
    IUIAutomationTreeWalker* walker = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                   reinterpret_cast<void**>(&uia))) &&
        SUCCEEDED(uia->get_RawViewWalker(&walker))) {
        HWND lastHwnd = nullptr;
        int lastHeight = 0;
        while (WaitForSingleObject(g_stopEvent, kPollIntervalMs) == WAIT_TIMEOUT) {
            RECT rc;
            HWND hwnd = FindToastBanner(&rc);
            int height = hwnd ? rc.bottom - rc.top : 0;
            // A new banner, or another toast stacked on the visible one. Wait a moment so the content is laid out.
            if (hwnd && (hwnd != lastHwnd || height > lastHeight)) {
                Sleep(400);
                Wh_Log(L"==== banner %08X %d,%d %dx%d", (DWORD)(ULONG_PTR)hwnd, rc.left, rc.top, rc.right - rc.left,
                       height);
                IUIAutomationElement* root = nullptr;
                if (SUCCEEDED(uia->ElementFromHandle(hwnd, &root)) && root) {
                    Dump(walker, root, 0);
                    root->Release();
                }
                Wh_Log(L"==== end");
            }
            lastHwnd = hwnd;
            lastHeight = height;
        }
    }
    if (walker) {
        walker->Release();
    }
    if (uia) {
        uia->Release();
    }
    CoUninitialize();
    return 0;
}

BOOL WhTool_ModInit() {
    g_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        return FALSE;
    }
    g_threadHandle = CreateThread(nullptr, 0, PollThread, nullptr, 0, nullptr);
    return g_threadHandle != nullptr;
}

void WhTool_ModSettingsChanged() {}

void WhTool_ModUninit() {
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    if (g_threadHandle) {
        WaitForSingleObject(g_threadHandle, INFINITE);
        CloseHandle(g_threadHandle);
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Windhawk tool mod boilerplate: the mod runs in its own windhawk.exe process.
// https://github.com/ramensoftware/windhawk/wiki/Mods-as-tools:-Running-mods-in-a-dedicated-process

bool g_isToolModProcessLauncher;
HANDLE g_toolModProcessMutex;

void WINAPI EntryPoint_Hook() {
    ExitThread(0);
}

BOOL Wh_ModInit() {
    DWORD sessionId;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) && sessionId == 0) {
        return FALSE;
    }
    bool isExcluded = false;
    bool isToolModProcess = false;
    bool isCurrentToolModProcess = false;
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (!argv) {
        return FALSE;
    }
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-service") == 0 || wcscmp(argv[i], L"-service-start") == 0 ||
            wcscmp(argv[i], L"-service-stop") == 0) {
            isExcluded = true;
            break;
        }
    }
    for (int i = 1; i < argc - 1; i++) {
        if (wcscmp(argv[i], L"-tool-mod") == 0) {
            isToolModProcess = true;
            isCurrentToolModProcess = wcscmp(argv[i + 1], WH_MOD_ID) == 0;
            break;
        }
    }
    LocalFree(argv);
    if (isExcluded) {
        return FALSE;
    }
    if (isCurrentToolModProcess) {
        g_toolModProcessMutex = CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
        if (!g_toolModProcessMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            ExitProcess(1);
        }
        if (!WhTool_ModInit()) {
            ExitProcess(1);
        }
        IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)GetModuleHandle(nullptr);
        IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((BYTE*)dosHeader + dosHeader->e_lfanew);
        void* entryPoint = (BYTE*)dosHeader + ntHeaders->OptionalHeader.AddressOfEntryPoint;
        Wh_SetFunctionHook(entryPoint, (void*)EntryPoint_Hook, nullptr);
        return TRUE;
    }
    if (isToolModProcess) {
        return FALSE;
    }
    g_isToolModProcessLauncher = true;
    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_isToolModProcessLauncher) {
        return;
    }
    WCHAR currentProcessPath[MAX_PATH];
    switch (GetModuleFileName(nullptr, currentProcessPath, ARRAYSIZE(currentProcessPath))) {
        case 0:
        case ARRAYSIZE(currentProcessPath):
            return;
    }
    WCHAR commandLine[MAX_PATH + 2 + (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) - 1];
    swprintf_s(commandLine, L"\"%s\" -tool-mod \"%s\"", currentProcessPath, WH_MOD_ID);
    HMODULE kernelModule = GetModuleHandle(L"kernelbase.dll");
    if (!kernelModule) {
        kernelModule = GetModuleHandle(L"kernel32.dll");
        if (!kernelModule) {
            return;
        }
    }
    using CreateProcessInternalW_t = BOOL(WINAPI*)(HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
                                                   LPSECURITY_ATTRIBUTES, WINBOOL, DWORD, LPVOID, LPCWSTR,
                                                   LPSTARTUPINFOW, LPPROCESS_INFORMATION, PHANDLE);
    auto pCreateProcessInternalW =
        (CreateProcessInternalW_t)GetProcAddress(kernelModule, "CreateProcessInternalW");
    if (!pCreateProcessInternalW) {
        return;
    }
    STARTUPINFO si{.cb = sizeof(STARTUPINFO), .dwFlags = STARTF_FORCEOFFFEEDBACK};
    PROCESS_INFORMATION pi;
    if (pCreateProcessInternalW(nullptr, currentProcessPath, commandLine, nullptr, nullptr, FALSE,
                                NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi, nullptr)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void Wh_ModSettingsChanged() {
    if (!g_isToolModProcessLauncher) {
        WhTool_ModSettingsChanged();
    }
}

void Wh_ModUninit() {
    if (g_isToolModProcessLauncher) {
        return;
    }
    WhTool_ModUninit();
    ExitProcess(0);
}
