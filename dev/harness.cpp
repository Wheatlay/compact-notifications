// Runs the tool part of a mod (reading toasts, drawing cards) as a plain program, outside Windhawk, with the log on
// stdout. The installed mod still hides the system banner. Build and run: powershell -File dev/harness.ps1
#include <windows.h>

#include <stdio.h>

#define WH_MOD_ID L"local@messy-toasts-harness"
#define Wh_Log(format, ...)                                                    \
    do {                                                                       \
        wprintf(L"%hs:%d " format L"\n", __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        fflush(stdout);                                                        \
    } while (0)

inline BOOL Wh_SetFunctionHook(void*, void*, void**) {
    return TRUE;
}

// No settings outside Windhawk: the mod keeps its built-in defaults (LoadSettings isn't called).
inline int Wh_GetIntSetting(PCWSTR, ...) {
    return 0;
}
inline PCWSTR Wh_GetStringSetting(PCWSTR, ...) {
    return L"";
}
inline void Wh_FreeStringSetting(PCWSTR) {}

#include MOD_SOURCE

int wmain(int argc, wchar_t** argv) {
    int seconds = argc > 1 ? _wtoi(argv[1]) : 30;
    if (!WhTool_ModInit()) {
        Wh_Log(L"WhTool_ModInit failed");
        return 1;
    }
    Sleep(seconds * 1000);
    WhTool_ModUninit();
    return 0;
}
