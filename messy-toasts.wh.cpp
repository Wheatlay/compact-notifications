// ==WindhawkMod==
// @id              messy-toasts
// @name            Messy toasts
// @description     Replaces Windows toast banners with compact dark cards in the style of Messy
// @version         0.1
// @author          Wheatlay
// @github          https://github.com/Wheatlay
// @include         ShellExperienceHost.exe
// @include         explorer.exe
// @include         windhawk.exe
// @compilerOptions -lole32 -loleaut32 -ldwmapi -ld2d1 -ldwrite -lshcore -lgdi32
// ==/WindhawkMod==

// The tool-mod boilerplate and the banner detection idea come from the "Toast auto-dismiss" mod by nikitor32 (GPL-3.0),
// the way the banner window is told apart from other shell windows from "Notifications placement" by m417z (GPL-3.0),
// so this file is GPL-3.0 as well.

// ==WindhawkModReadme==
/*
# Messy toasts

Windows still creates its own toast banner, but the mod moves it off the screen and shows its own card instead.
Clicking the card does what clicking the banner would do; the × on hover closes it (it stays in the notification
center). A card lives exactly as long as the hidden banner: the time is set in Settings > Accessibility >
Visual effects > Dismiss notifications after this amount of time.

Not yet: app images and avatars, buttons and reply boxes (use the notification center for those).
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shellscalingapi.h>
#include <uiautomation.h>

#include <wchar.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// The banner window title (String resource \ActionCenter\AC_ToastCenter_Title). Only languages in use here.
const wchar_t* const kBannerTitles[] = {L"Nowe powiadomienie", L"New notification", L"Neue Benachrichtigung"};

bool IsBannerTitle(HWND hwnd) {
    WCHAR title[64];
    if (!InternalGetWindowText(hwnd, title, ARRAYSIZE(title))) {
        return false;
    }
    for (auto known : kBannerTitles) {
        if (wcscmp(title, known) == 0) {
            return true;
        }
    }
    return false;
}

bool IsCoreWindowClass(HWND hwnd) {
    WCHAR className[32];
    return GetClassNameW(hwnd, className, ARRAYSIZE(className)) && wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0;
}

bool ProcessIsShellExperienceHost(DWORD pid) {
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

// The banner window, cached by handle: a window never changes process or class.
bool IsBannerWindow(HWND hwnd) {
    static std::atomic<HWND> lastYes{nullptr};
    if (!hwnd || !IsCoreWindowClass(hwnd) || !IsBannerTitle(hwnd)) {
        return false;
    }
    if (lastYes.load() == hwnd) {
        return true;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!ProcessIsShellExperienceHost(pid)) {
        return false;
    }
    lastYes.store(hwnd);
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// Hiding: inside ShellExperienceHost and explorer, every move of the banner window goes off the screen.

using SetWindowPos_t = decltype(&SetWindowPos);
SetWindowPos_t SetWindowPos_Original;

int OffscreenX() {
    return GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 200;
}

BOOL WINAPI SetWindowPos_Hook(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    if ((uFlags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE) && IsBannerWindow(hWnd)) {
        if (uFlags & SWP_NOMOVE) {
            RECT rc{};
            GetWindowRect(hWnd, &rc);
            Y = rc.top;
            uFlags &= ~SWP_NOMOVE;
        }
        X = OffscreenX();
    }
    return SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

void HideExistingBanner() {
    HWND hwnd = nullptr;
    while ((hwnd = FindWindowExW(HWND_DESKTOP, hwnd, L"Windows.UI.Core.CoreWindow", nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId() && IsBannerWindow(hwnd)) {
            RECT rc{};
            GetWindowRect(hwnd, &rc);
            SetWindowPos_Original(hwnd, nullptr, OffscreenX(), rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Reading: a worker thread finds the banner and reads its toasts through UI Automation.

struct ToastInfo {
    std::wstring id;  // UIA runtime id
    std::wstring app;
    std::wstring title;
    std::wstring text;
};

enum class Action { Open, Dismiss };

std::mutex g_mutex;
std::vector<ToastInfo> g_snapshot;                       // guarded by g_mutex
std::vector<std::pair<std::wstring, Action>> g_commands;  // guarded by g_mutex

HANDLE g_stopEvent = nullptr;
HANDLE g_workerThread = nullptr;
HANDLE g_uiThread = nullptr;
DWORD g_uiThreadId = 0;
HWND g_window = nullptr;

constexpr UINT WM_APP_SNAPSHOT = WM_APP + 1;
constexpr DWORD kPollMs = 100;

HWND FindVisibleBanner() {
    HWND hwnd = nullptr;
    while ((hwnd = FindWindowExW(HWND_DESKTOP, hwnd, L"Windows.UI.Core.CoreWindow", nullptr)) != nullptr) {
        if (!IsWindowVisible(hwnd) || !IsBannerWindow(hwnd)) {
            continue;
        }
        // At rest the window shrinks to a sliver; a real banner is at least ~80 px tall.
        RECT rc;
        UINT dpi = GetDpiForWindow(hwnd);
        if (GetWindowRect(hwnd, &rc) && rc.bottom - rc.top >= MulDiv(80, dpi ? dpi : 96, 96)) {
            return hwnd;
        }
    }
    return nullptr;
}

template <typename T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p) {
            p->Release();
        }
    }
    T** put() {
        if (p) {
            p->Release();
            p = nullptr;
        }
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::wstring RuntimeId(IUIAutomationElement* element) {
    SAFEARRAY* array = nullptr;
    std::wstring id;
    if (SUCCEEDED(element->GetRuntimeId(&array)) && array) {
        LONG lo = 0, hi = -1;
        SafeArrayGetLBound(array, 1, &lo);
        SafeArrayGetUBound(array, 1, &hi);
        for (LONG i = lo; i <= hi; i++) {
            int value = 0;
            SafeArrayGetElement(array, &i, &value);
            id += std::to_wstring(value) + L".";
        }
        SafeArrayDestroy(array);
    }
    return id;
}

Com<IUIAutomationCondition>* NewIdCondition(IUIAutomation* uia, const wchar_t* automationId) {
    auto condition = new Com<IUIAutomationCondition>();
    VARIANT v{};
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(automationId);
    uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, v, condition->put());
    VariantClear(&v);
    return condition;
}

std::wstring ChildName(IUIAutomationElement* toast, IUIAutomationCondition* condition) {
    Com<IUIAutomationElement> child;
    std::wstring result;
    if (condition && SUCCEEDED(toast->FindFirst(TreeScope_Descendants, condition, child.put())) && child) {
        BSTR name = nullptr;
        if (SUCCEEDED(child->get_CurrentName(&name)) && name) {
            result = name;
            SysFreeString(name);
        }
    }
    return result;
}

void InvokeElement(IUIAutomationElement* element) {
    Com<IUIAutomationInvokePattern> invoke;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern),
                                               reinterpret_cast<void**>(invoke.put()))) &&
        invoke) {
        invoke->Invoke();
    }
}

DWORD WINAPI WorkerThread(LPVOID) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        return 0;
    }
    {
        Com<IUIAutomation> uia;
        if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                    reinterpret_cast<void**>(uia.put())))) {
            Wh_Log(L"UI Automation unavailable");
            CoUninitialize();
            return 0;
        }
        Com<IUIAutomationCondition> toastCondition;
        {
            VARIANT v{};
            v.vt = VT_BSTR;
            v.bstrVal = SysAllocString(L"FlexibleToastView");
            uia->CreatePropertyCondition(UIA_ClassNamePropertyId, v, toastCondition.put());
            VariantClear(&v);
        }
        Com<IUIAutomationCondition>* senderCondition = NewIdCondition(uia.p, L"SenderName");
        Com<IUIAutomationCondition>* titleCondition = NewIdCondition(uia.p, L"Title");
        Com<IUIAutomationCondition>* textCondition = NewIdCondition(uia.p, L"MessageText");
        Com<IUIAutomationCondition>* dismissCondition = NewIdCondition(uia.p, L"DismissButton");

        // Toasts already read: their texts don't change, and the element is needed to click or dismiss them.
        struct Known {
            ToastInfo info;
            IUIAutomationElement* element;
        };
        std::vector<Known> known;
        std::vector<ToastInfo> lastSent;

        while (WaitForSingleObject(g_stopEvent, kPollMs) == WAIT_TIMEOUT) {
            std::vector<std::pair<std::wstring, Action>> commands;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                commands.swap(g_commands);
            }
            for (auto& [id, action] : commands) {
                for (auto& k : known) {
                    if (k.info.id != id) {
                        continue;
                    }
                    if (action == Action::Open) {
                        InvokeElement(k.element);
                    } else {
                        Com<IUIAutomationElement> button;
                        if (SUCCEEDED(k.element->FindFirst(TreeScope_Descendants, dismissCondition->p, button.put())) &&
                            button) {
                            InvokeElement(button.p);
                        }
                    }
                }
            }

            std::vector<Known> current;
            if (HWND banner = FindVisibleBanner()) {
                Com<IUIAutomationElement> root;
                Com<IUIAutomationElementArray> toasts;
                if (SUCCEEDED(uia->ElementFromHandle(banner, root.put())) && root &&
                    SUCCEEDED(root->FindAll(TreeScope_Descendants, toastCondition.p, toasts.put())) && toasts) {
                    int count = 0;
                    toasts->get_Length(&count);
                    for (int i = 0; i < count; i++) {
                        IUIAutomationElement* element = nullptr;
                        if (FAILED(toasts->GetElement(i, &element)) || !element) {
                            continue;
                        }
                        std::wstring id = RuntimeId(element);
                        auto old = std::find_if(known.begin(), known.end(), [&](const Known& k) { return k.info.id == id; });
                        if (old != known.end()) {
                            current.push_back({old->info, element});
                            continue;
                        }
                        ToastInfo info{id, ChildName(element, senderCondition->p), ChildName(element, titleCondition->p),
                                       ChildName(element, textCondition->p)};
                        Wh_Log(L"Toast %s: %s / %s", info.app.c_str(), info.title.c_str(), info.text.c_str());
                        current.push_back({info, element});
                    }
                }
            }
            for (auto& k : known) {
                k.element->Release();
            }
            known.swap(current);

            std::vector<ToastInfo> infos;
            for (auto& k : known) {
                infos.push_back(k.info);
            }
            bool changed = infos.size() != lastSent.size() ||
                           !std::equal(infos.begin(), infos.end(), lastSent.begin(),
                                       [](const ToastInfo& a, const ToastInfo& b) { return a.id == b.id; });
            if (changed) {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_snapshot = infos;
                }
                lastSent = infos;
                if (g_window) {
                    PostMessage(g_window, WM_APP_SNAPSHOT, 0, 0);
                }
            }
        }
        for (auto& k : known) {
            k.element->Release();
        }
        delete senderCondition;
        delete titleCondition;
        delete textCondition;
        delete dismissCondition;
    }
    CoUninitialize();
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Drawing: one layered, never-activated window in the bottom right corner with a stack of cards (page/toast.html
// in Messy): newest at the bottom, slides in from the right, fades out.

constexpr float kWidth = 360;  // window width in DIPs; cards are kWidth - 2 * kPad wide
constexpr float kPad = 8;      // room around the stack (shadow) and gap between cards
constexpr float kCardPadX = 12, kCardPadY = 10, kAvatar = 40, kGap = 10, kRadius = 12;
constexpr float kFontSize = 13, kLine = kFontSize * 1.35f;
constexpr DWORD kInMs = 280, kOutMs = 400;

struct Card {
    ToastInfo info;
    IDWriteTextLayout* title = nullptr;
    IDWriteTextLayout* text = nullptr;
    float height = 0;
    ULONGLONG shownAt = 0;
    ULONGLONG goneAt = 0;  // 0 while the system banner still has the toast
    float top = 0;         // last drawn position, for hit testing
};

std::vector<Card> g_cards;
int g_hovered = -1;
bool g_hoverClose = false;

ID2D1Factory* g_d2d = nullptr;
IDWriteFactory* g_dwrite = nullptr;
IDWriteTextFormat* g_titleFormat = nullptr;
IDWriteTextFormat* g_textFormat = nullptr;
IDWriteTextFormat* g_letterFormat = nullptr;
ID2D1DCRenderTarget* g_target = nullptr;
HDC g_memDC = nullptr;
HBITMAP g_bitmap = nullptr;
SIZE g_bitmapSize{};

float Scale() {
    HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    return dpiX / 96.0f;
}

RECT WorkArea() {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfo(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return mi.rcWork;
}

float EaseOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

void ReleaseCard(Card& card) {
    if (card.title) {
        card.title->Release();
    }
    if (card.text) {
        card.text->Release();
    }
}

void LayoutCard(Card& card) {
    float textWidth = kWidth - 2 * kPad - 2 * kCardPadX - kAvatar - kGap;
    std::wstring title = card.info.title.empty() ? card.info.app : card.info.title;
    g_dwrite->CreateTextLayout(title.c_str(), (UINT32)title.size(), g_titleFormat, textWidth, kLine, &card.title);
    g_dwrite->CreateTextLayout(card.info.text.c_str(), (UINT32)card.info.text.size(), g_textFormat, textWidth,
                               2 * kLine, &card.text);
    DWRITE_TEXT_METRICS metrics{};
    float textHeight = 0;
    if (card.text && SUCCEEDED(card.text->GetMetrics(&metrics))) {
        textHeight = std::min(metrics.height, 2 * kLine);
    }
    card.height = std::max(kAvatar, kLine + textHeight) + 2 * kCardPadY;
}

void ApplySnapshot() {
    std::vector<ToastInfo> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        snapshot = g_snapshot;
    }
    ULONGLONG now = GetTickCount64();
    for (auto& card : g_cards) {
        bool alive = std::any_of(snapshot.begin(), snapshot.end(), [&](const ToastInfo& t) { return t.id == card.info.id; });
        if (!alive && !card.goneAt) {
            card.goneAt = now;
        }
    }
    for (auto& info : snapshot) {
        bool have = std::any_of(g_cards.begin(), g_cards.end(), [&](const Card& c) { return c.info.id == info.id; });
        if (!have) {
            Card card;
            card.info = info;
            card.shownAt = now;
            LayoutCard(card);
            g_cards.push_back(card);
        }
    }
}

ID2D1SolidColorBrush* Brush(UINT32 rgb, float alpha) {
    ID2D1SolidColorBrush* brush = nullptr;
    D2D1_COLOR_F color{((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, alpha};
    g_target->CreateSolidColorBrush(&color, nullptr, &brush);
    return brush;
}

void FillRounded(D2D1_RECT_F rect, float radius, UINT32 rgb, float alpha) {
    if (ID2D1SolidColorBrush* brush = Brush(rgb, alpha)) {
        D2D1_ROUNDED_RECT rounded{rect, radius, radius};
        g_target->FillRoundedRectangle(&rounded, brush);
        brush->Release();
    }
}

// Returns whether anything is still animating.
bool Render() {
    ULONGLONG now = GetTickCount64();
    g_cards.erase(std::remove_if(g_cards.begin(), g_cards.end(),
                                 [&](Card& c) {
                                     bool done = c.goneAt && now - c.goneAt >= kOutMs;
                                     if (done) {
                                         ReleaseCard(c);
                                     }
                                     return done;
                                 }),
                  g_cards.end());
    if (g_cards.empty()) {
        ShowWindow(g_window, SW_HIDE);
        g_hovered = -1;
        return false;
    }

    float scale = Scale();
    float heightDip = kPad;
    for (auto& card : g_cards) {
        heightDip += card.height + kPad;
    }
    SIZE size{(LONG)(kWidth * scale + 0.5f), (LONG)(heightDip * scale + 0.5f)};
    if (size.cx != g_bitmapSize.cx || size.cy != g_bitmapSize.cy) {
        if (g_bitmap) {
            DeleteObject(g_bitmap);
        }
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), size.cx, -size.cy, 1, 32, BI_RGB};
        void* bits = nullptr;
        g_bitmap = CreateDIBSection(g_memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        SelectObject(g_memDC, g_bitmap);
        g_bitmapSize = size;
    }
    RECT bindRect{0, 0, size.cx, size.cy};
    g_target->BindDC(g_memDC, &bindRect);
    g_target->SetDpi(96 * scale, 96 * scale);
    g_target->BeginDraw();
    D2D1_COLOR_F transparent{0, 0, 0, 0};
    g_target->Clear(&transparent);

    bool animating = false;
    float cardWidth = kWidth - 2 * kPad;
    float y = kPad;
    for (size_t i = 0; i < g_cards.size(); i++) {
        Card& card = g_cards[i];
        card.top = y;
        float in = EaseOut((now - card.shownAt) / (float)kInMs);
        float alpha = card.goneAt ? 1 - std::clamp((now - card.goneAt) / (float)kOutMs, 0.0f, 1.0f) : 1.0f;
        animating |= in < 1 || card.goneAt;
        float x = kPad + (1 - in) * cardWidth * 1.1f;
        D2D1_RECT_F rect{x, y, x + cardWidth, y + card.height};
        bool hovered = (int)i == g_hovered && !card.goneAt;

        // Soft shadow: a few widening, fading layers (box-shadow: 0 4px 14px rgba(0,0,0,.35)).
        for (int layer = 4; layer >= 1; layer--) {
            float grow = layer * 2.0f;
            FillRounded(D2D1_RECT_F{rect.left - grow, rect.top - grow + 4, rect.right + grow, rect.bottom + grow + 4},
                        kRadius + grow, 0x000000, 0.06f * alpha);
        }
        FillRounded(rect, kRadius, hovered ? 0x292a2b : 0x242526, alpha);
        if (ID2D1SolidColorBrush* border = Brush(0x808080, 0.25f * alpha)) {
            D2D1_RECT_F inner{rect.left + 0.5f, rect.top + 0.5f, rect.right - 0.5f, rect.bottom - 0.5f};
            D2D1_ROUNDED_RECT rounded{inner, kRadius, kRadius};
            g_target->DrawRoundedRectangle(&rounded, border, 1);
            border->Release();
        }

        // Avatar: a circle with the app's first letter until real images come.
        float cx = rect.left + kCardPadX + kAvatar / 2, cy = rect.top + card.height / 2;
        if (ID2D1SolidColorBrush* fill = Brush(0x808080, 0.3f * alpha)) {
            D2D1_ELLIPSE circle{{cx, cy}, kAvatar / 2, kAvatar / 2};
            g_target->FillEllipse(&circle, fill);
            fill->Release();
        }
        std::wstring source = card.info.app.empty() ? card.info.title : card.info.app;
        if (!source.empty()) {
            wchar_t letter[2] = {(wchar_t)towupper(source[0]), 0};
            if (ID2D1SolidColorBrush* ink = Brush(0xe4e6eb, alpha)) {
                D2D1_RECT_F box{cx - kAvatar / 2, cy - kAvatar / 2, cx + kAvatar / 2, cy + kAvatar / 2};
                g_target->DrawText(letter, 1, g_letterFormat, &box, ink);
                ink->Release();
            }
        }

        float textLeft = rect.left + kCardPadX + kAvatar + kGap;
        DWRITE_TEXT_METRICS metrics{};
        float textHeight = card.text && SUCCEEDED(card.text->GetMetrics(&metrics)) ? std::min(metrics.height, 2 * kLine) : 0;
        float textTop = rect.top + (card.height - kLine - textHeight) / 2;
        if (ID2D1SolidColorBrush* ink = Brush(0xe4e6eb, alpha)) {
            if (card.title) {
                g_target->DrawTextLayout(D2D1_POINT_2F{textLeft, textTop}, card.title, ink, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            ink->Release();
        }
        if (ID2D1SolidColorBrush* ink = Brush(0xe4e6eb, 0.8f * alpha)) {
            if (card.text) {
                g_target->DrawTextLayout(D2D1_POINT_2F{textLeft, textTop + kLine}, card.text, ink,
                                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            ink->Release();
        }

        // Close button on hover, top right.
        if (hovered) {
            float bx = rect.right - 14, by = rect.top + 14;
            if (g_hoverClose) {
                if (ID2D1SolidColorBrush* fill = Brush(0x808080, 0.3f)) {
                    D2D1_ELLIPSE circle{{bx, by}, 10, 10};
                    g_target->FillEllipse(&circle, fill);
                    fill->Release();
                }
            }
            if (ID2D1SolidColorBrush* ink = Brush(0xe4e6eb, 0.8f)) {
                g_target->DrawLine({bx - 4, by - 4}, {bx + 4, by + 4}, ink, 1.4f);
                g_target->DrawLine({bx + 4, by - 4}, {bx - 4, by + 4}, ink, 1.4f);
                ink->Release();
            }
        }
        y += card.height + kPad;
    }
    g_target->EndDraw();

    RECT work = WorkArea();
    POINT dst{work.right - size.cx, work.bottom - size.cy};
    POINT src{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_window, nullptr, &dst, &size, g_memDC, &src, 0, &blend, ULW_ALPHA);
    if (!IsWindowVisible(g_window)) {
        ShowWindow(g_window, SW_SHOWNOACTIVATE);
    }
    SetWindowPos(g_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return animating;
}

void Redraw() {
    if (Render()) {
        SetTimer(g_window, 1, 15, nullptr);
    } else {
        KillTimer(g_window, 1);
    }
}

// Which card (and whether its close button) is under a point in window pixels.
int HitTest(POINT pt, bool* onClose) {
    float scale = Scale();
    float x = pt.x / scale, y = pt.y / scale;
    for (size_t i = 0; i < g_cards.size(); i++) {
        const Card& card = g_cards[i];
        if (!card.goneAt && x >= kPad && x < kWidth - kPad && y >= card.top && y < card.top + card.height) {
            float bx = kWidth - kPad - 14, by = card.top + 14;
            *onClose = (x - bx) * (x - bx) + (y - by) * (y - by) <= 12 * 12;
            return (int)i;
        }
    }
    *onClose = false;
    return -1;
}

void SendCommand(const std::wstring& id, Action action) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_commands.push_back({id, action});
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_APP_SNAPSHOT:
            ApplySnapshot();
            Redraw();
            return 0;
        case WM_TIMER:
            Redraw();
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            bool onClose = false;
            int hovered = HitTest(POINT{(short)LOWORD(lParam), (short)HIWORD(lParam)}, &onClose);
            if (hovered != g_hovered || onClose != g_hoverClose) {
                g_hovered = hovered;
                g_hoverClose = onClose;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                Redraw();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_hovered = -1;
            g_hoverClose = false;
            Redraw();
            return 0;
        case WM_LBUTTONUP: {
            bool onClose = false;
            int index = HitTest(POINT{(short)LOWORD(lParam), (short)HIWORD(lParam)}, &onClose);
            if (index >= 0) {
                Card& card = g_cards[index];
                SendCommand(card.info.id, onClose ? Action::Dismiss : Action::Open);
                card.goneAt = GetTickCount64();
                Redraw();
            }
            return 0;
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool CreateResources() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                 reinterpret_cast<void**>(&g_d2d))) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&g_dwrite)))) {
        return false;
    }
    g_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL, kFontSize, L"", &g_titleFormat);
    g_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL, kFontSize, L"", &g_textFormat);
    g_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL, 17, L"", &g_letterFormat);
    if (!g_titleFormat || !g_textFormat || !g_letterFormat) {
        return false;
    }
    // Name on one line, text on at most two, both cut with an ellipsis (text-overflow, -webkit-line-clamp).
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    for (IDWriteTextFormat* format : {g_titleFormat, g_textFormat}) {
        IDWriteInlineObject* ellipsis = nullptr;
        g_dwrite->CreateEllipsisTrimmingSign(format, &ellipsis);
        format->SetTrimming(&trimming, ellipsis);
        if (ellipsis) {
            ellipsis->Release();
        }
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, kLine, kLine * 0.8f);
    }
    g_titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    g_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
    g_letterFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_letterFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    D2D1_RENDER_TARGET_PROPERTIES props{D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                        {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED},
                                        0,
                                        0,
                                        D2D1_RENDER_TARGET_USAGE_NONE,
                                        D2D1_FEATURE_LEVEL_DEFAULT};
    if (FAILED(g_d2d->CreateDCRenderTarget(&props, &g_target))) {
        return false;
    }
    g_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    g_memDC = CreateCompatibleDC(nullptr);
    return true;
}

void ReleaseResources() {
    for (auto& card : g_cards) {
        ReleaseCard(card);
    }
    g_cards.clear();
    if (g_bitmap) {
        DeleteObject(g_bitmap);
    }
    if (g_memDC) {
        DeleteDC(g_memDC);
    }
    for (IUnknown* object : std::initializer_list<IUnknown*>{g_target, g_letterFormat, g_textFormat, g_titleFormat,
                                                             g_dwrite, g_d2d}) {
        if (object) {
            object->Release();
        }
    }
}

DWORD WINAPI UiThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (CreateResources()) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"MessyToasts";
        RegisterClassW(&wc);
        g_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                                   L"Messy toasts", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0) > 0) {
            DispatchMessage(&msg);
        }
        DestroyWindow(g_window);
        g_window = nullptr;
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
    } else {
        Wh_Log(L"Direct2D/DirectWrite unavailable");
    }
    ReleaseResources();
    CoUninitialize();
    return 0;
}

BOOL WhTool_ModInit() {
    g_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    g_uiThread = CreateThread(nullptr, 0, UiThread, nullptr, 0, &g_uiThreadId);
    g_workerThread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    return g_stopEvent && g_uiThread && g_workerThread;
}

void WhTool_ModSettingsChanged() {}

void WhTool_ModUninit() {
    SetEvent(g_stopEvent);
    WaitForSingleObject(g_workerThread, INFINITE);
    PostThreadMessage(g_uiThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(g_uiThread, INFINITE);
}

////////////////////////////////////////////////////////////////////////////////
// Process roles: ShellExperienceHost and explorer only hide the banner; the drawing runs in a dedicated windhawk.exe
// process (tool mod), so a crash there never takes down the shell.
// https://github.com/ramensoftware/windhawk/wiki/Mods-as-tools:-Running-mods-in-a-dedicated-process

enum class Role { None, Shell, ToolLauncher, Tool };
Role g_role = Role::None;
HANDLE g_toolModProcessMutex;

void WINAPI EntryPoint_Hook() {
    ExitThread(0);
}

BOOL InitTool() {
    DWORD sessionId;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) && sessionId == 0) {
        return FALSE;
    }
    bool isExcluded = false, isToolModProcess = false, isCurrentToolModProcess = false;
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (!argv) {
        return FALSE;
    }
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-service") == 0 || wcscmp(argv[i], L"-service-start") == 0 ||
            wcscmp(argv[i], L"-service-stop") == 0) {
            isExcluded = true;
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
        g_role = Role::Tool;
        return TRUE;
    }
    if (isToolModProcess) {
        return FALSE;
    }
    g_role = Role::ToolLauncher;
    return TRUE;
}

BOOL Wh_ModInit() {
    WCHAR path[MAX_PATH];
    if (!GetModuleFileName(nullptr, path, ARRAYSIZE(path))) {
        return FALSE;
    }
    const WCHAR* base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    if (_wcsicmp(base, L"ShellExperienceHost.exe") == 0 || _wcsicmp(base, L"explorer.exe") == 0) {
        g_role = Role::Shell;
        Wh_SetFunctionHook((void*)SetWindowPos, (void*)SetWindowPos_Hook, (void**)&SetWindowPos_Original);
        return TRUE;
    }
    return InitTool();
}

void Wh_ModAfterInit() {
    if (g_role == Role::Shell) {
        HideExistingBanner();
        return;
    }
    if (g_role != Role::ToolLauncher) {
        return;
    }
    WCHAR currentProcessPath[MAX_PATH];
    switch (GetModuleFileName(nullptr, currentProcessPath, ARRAYSIZE(currentProcessPath))) {
        case 0:
        case ARRAYSIZE(currentProcessPath):
            return;
    }
    WCHAR commandLine[MAX_PATH + 2 + (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) - 1];
    swprintf(commandLine, ARRAYSIZE(commandLine), L"\"%s\" -tool-mod \"%s\"", currentProcessPath, WH_MOD_ID);
    HMODULE kernelModule = GetModuleHandle(L"kernelbase.dll");
    if (!kernelModule && !(kernelModule = GetModuleHandle(L"kernel32.dll"))) {
        return;
    }
    using CreateProcessInternalW_t = BOOL(WINAPI*)(HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
                                                   LPSECURITY_ATTRIBUTES, WINBOOL, DWORD, LPVOID, LPCWSTR,
                                                   LPSTARTUPINFOW, LPPROCESS_INFORMATION, PHANDLE);
    auto pCreateProcessInternalW = (CreateProcessInternalW_t)GetProcAddress(kernelModule, "CreateProcessInternalW");
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
    if (g_role == Role::Tool) {
        WhTool_ModSettingsChanged();
    }
}

void Wh_ModUninit() {
    if (g_role == Role::Tool) {
        WhTool_ModUninit();
        ExitProcess(0);
    }
    // Shell: the hook goes away with the mod and Windows places the next banner on the screen again.
}
