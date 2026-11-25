// Reconstruction of the NCSoft "BnS Console" window behind bsConsole_sv64.dll.
//
// Everything observable from the daemon is implemented here: the 17 interface slots, lazy HWND
// creation, the custom log view, the tool/status bars, the settings dialogs and registry storage.
// Log records arrive from arbitrary daemon threads; they are copied under a critical section and
// only PostMessageW(0x402) crosses the thread boundary.  All GDI/window work happens on the thread
// that called ShowRun().
#include "ConsoleWindow.h"

#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <wctype.h>

SOURCE_FILE("ConsoleWindow.cpp");

namespace bsconsole {

// Custom messages / timers / command ids (original values from report sections 3.1, 6).
namespace {
const UINT kMsgRefresh        = 0x402;
const UINT kMsgConnect        = 0x404;
const UINT kSbSetParts        = 0x40A;
const UINT kSbSetText         = 0x40B;
const UINT kTimerState        = 0x3E9;
const UINT kTimerBusy         = 0x3EA;
const UINT kTimerMessage      = 0x3EB;

const WORD kIdConnect         = 0xC9;
const WORD kIdSetting         = 0xCA;
const WORD kIdReload          = 0xCB;
const WORD kIdShutdown        = 0xCC;
const WORD kIdScrollTop       = 0xCD;
const WORD kIdClear           = 0xCE;
const WORD kIdConnectCmd      = 0xCF;
const WORD kIdFilterError     = 0xD0;
const WORD kIdFilterDebug     = 0xD1;
const WORD kIdFilterTrace     = 0xD2;
const WORD kIdReverse         = 0xD3;   // UNVERIFIED: original id in 0xC9..0xD2 range unknown
const WORD kIdSaveLog         = 0xD4;   // UNVERIFIED: original save-log command id unknown
} // namespace

// ---------------------------------------------------------------------------------------------
// Private mirror of blood::LogBuffer (report section 3.5).  We never keep the pointer: level and
// text are copied into our own ring while the critical section is held.
// ---------------------------------------------------------------------------------------------
#pragma pack(push, 8)
struct RecordIfaceVtbl {
    unsigned long long (*slot0)(void*);
    unsigned long long (*slot1)(void*);
    int (*slot2)(void*);
    unsigned long long (*slot3)(void*);
    unsigned long long (*slot4)(void*);
};
struct RecordIface {
    RecordIfaceVtbl* vtbl;
    int level;
    int enabled;
};
struct RecordMirror {
    void* vtbl;                       // +0x00
    int size;                         // +0x08
    int refcount;                     // +0x0C
    unsigned char pad10[0x18 - 0x10];
    RecordIface* iface;               // +0x18
    unsigned char pad20[0x78 - 0x20];
    int level;                        // +0x78
    unsigned char pad7C[0x8C - 0x7C];
    bool enabled;                     // +0x8C
    unsigned char pad8D;              // +0x8D (natural padding)
    wchar_t text[0x7B8];              // +0x8E
};
#pragma pack(pop)

static_assert(offsetof(RecordMirror, iface) == 0x18, "LogBuffer interface offset");
static_assert(offsetof(RecordMirror, level) == 0x78, "LogBuffer level offset");
static_assert(offsetof(RecordMirror, enabled) == 0x8C, "LogBuffer enabled offset");
static_assert(offsetof(RecordMirror, text) == 0x8E, "LogBuffer text offset");

// ---------------------------------------------------------------------------------------------
// Registry helpers - HKCU\Software\Bloodlust\<title>; only values that exist are loaded.
// ---------------------------------------------------------------------------------------------
namespace {

bool QueryDword(HKEY key, const wchar_t* name, DWORD* out) {
    DWORD type = 0, size = sizeof(DWORD);
    return RegQueryValueExW(key, name, NULL, &type, reinterpret_cast<BYTE*>(out), &size) == ERROR_SUCCESS
        && type == REG_DWORD && size == sizeof(DWORD);
}

bool QueryBinary(HKEY key, const wchar_t* name, void* buf, DWORD bytes) {
    DWORD type = 0, size = bytes;
    return RegQueryValueExW(key, name, NULL, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS
        && type == REG_BINARY && size == bytes;
}

bool QueryString(HKEY key, const wchar_t* name, wchar_t* buf, DWORD count) {
    DWORD type = 0, size = count * sizeof(wchar_t);
    if (RegQueryValueExW(key, name, NULL, &type, reinterpret_cast<BYTE*>(buf), &size) != ERROR_SUCCESS
        || type != REG_SZ) return false;
    buf[(size / sizeof(wchar_t)) < count ? size / sizeof(wchar_t) : count - 1] = 0;
    return true;
}

void StoreDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

void StoreBinary(HKEY key, const wchar_t* name, const void* buf, DWORD bytes) {
    RegSetValueExW(key, name, 0, REG_BINARY, reinterpret_cast<const BYTE*>(buf), bytes);
}

void StoreString(HKEY key, const wchar_t* name, const wchar_t* text) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(text),
                   (DWORD)((wcslen(text) + 1) * sizeof(wchar_t)));
}

} // namespace

// ---------------------------------------------------------------------------------------------
ConsoleWindow::ConsoleWindow(HMODULE module)
    : module_(module), instance_(NULL), callback1_(NULL), exitCallback_(NULL),
      reloadEnabled_(NULL), shutdownEnabled_(NULL), param9_(NULL), hook_(NULL),
      connected_(false), hwndMain_(NULL), hwndLog_(NULL), hwndControl_(NULL),
      hwndView_(NULL), hwndStatusPane_(NULL), hwndTool_(NULL), hwndStatusBar_(NULL),
      btnConnect_(NULL), btnSetting_(NULL), btnReload_(NULL), btnShutdown_(NULL),
      hdlgSettings_(NULL), fontLog_(NULL), fontWindow_(NULL),
      geoX_(CW_USEDEFAULT), geoY_(CW_USEDEFAULT), geoW_(0x400), geoH_(0x1E0),
      background_(0), levelColor_{0, 0, 0, 0}, logCount_(100000),
      filterError_(true), filterDebug_(true), filterTrace_(true), filterReverse_(false),
      connAuto_(false), connPort_(27518), lines_(NULL), linesCap_(0), oldest_(0),
      linesCount_(0), snapshot_(NULL), snapshotCap_(0), counters_{0, 0, 0, 0, 0, 0} {
    title_[0] = 0;
    explicitTitle_[0] = 0;
    regKey_[0] = 0;
    filterText_[0] = 0;
    connHost_[0] = 0;
    memset(&logFont_, 0, sizeof(logFont_));
    memset(&windowFont_, 0, sizeof(windowFont_));
    memset(customColors_, 0, sizeof(customColors_));
    memset(statusText_, 0, sizeof(statusText_));
    statusEdges_[0] = 0; statusEdges_[1] = 0; statusEdges_[2] = 0;
    InitializeCriticalSection(&cs_);
}

void ConsoleWindow::SetDefaults() {
    geoX_ = CW_USEDEFAULT; geoY_ = CW_USEDEFAULT; geoW_ = 0x400; geoH_ = 0x1E0;
    background_ = GetSysColor(COLOR_WINDOW);
    levelColor_[0] = 0x000000FF;   // ERROR - red (COLORREF 0x00BBGGRR)
    levelColor_[1] = 0x00000000;   // DEBUG - black
    levelColor_[2] = 0x00FF0000;   // INFO  - blue
    levelColor_[3] = 0x00808080;   // TRACE - gray

    HDC dc = GetDC(NULL);
    int lpy = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(NULL, dc);
    int height = -MulDiv(10, lpy, 72);

    memset(&logFont_, 0, sizeof(logFont_));
    logFont_.lfHeight = height;
    logFont_.lfWeight = FW_NORMAL;
    logFont_.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(logFont_.lfFaceName, LF_FACESIZE, L"Courier New");
    windowFont_ = logFont_;

    logCount_ = 100000;
    filterError_ = filterDebug_ = filterTrace_ = true;
    filterReverse_ = false;
    filterText_[0] = 0;
    connAuto_ = false;
    connPort_ = 27518;
    wcscpy_s(connHost_, L"blgame");
}

void ConsoleWindow::LoadSettings() {
    HKEY key = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, regKey_, 0, NULL, 0, 0x2001F, NULL, &key, NULL)
        != ERROR_SUCCESS) return;

    DWORD v = 0;
    if (QueryDword(key, L"GEO_X", &v)) geoX_ = (int)v;
    if (QueryDword(key, L"GEO_Y", &v)) geoY_ = (int)v;
    if (QueryDword(key, L"GEO_WIDTH", &v)) geoW_ = (int)v;
    if (QueryDword(key, L"GEO_HEIGHT", &v)) geoH_ = (int)v;
    if (QueryDword(key, L"LOGVIEW_COLOR", &v)) background_ = (COLORREF)v;
    if (QueryDword(key, L"FONT_COLOR_ERROR", &v)) levelColor_[0] = (COLORREF)v;
    if (QueryDword(key, L"FONT_COLOR_DEBUG", &v)) levelColor_[1] = (COLORREF)v;
    if (QueryDword(key, L"FONT_COLOR_INFO", &v)) levelColor_[2] = (COLORREF)v;
    if (QueryDword(key, L"FONT_COLOR_TRACE", &v)) levelColor_[3] = (COLORREF)v;
    QueryBinary(key, L"LOG_FONT", &logFont_, sizeof(logFont_));
    QueryBinary(key, L"WINDOW_FONT", &windowFont_, sizeof(windowFont_));
    if (QueryDword(key, L"LOG_COUNT", &v) && v > 0) logCount_ = (int)v;
    if (QueryDword(key, L"FILTER_ERROR", &v)) filterError_ = v != 0;
    if (QueryDword(key, L"FILTER_DEBUG", &v)) filterDebug_ = v != 0;
    if (QueryDword(key, L"FILTER_TRACE", &v)) filterTrace_ = v != 0;
    if (QueryDword(key, L"FILTER_REVERSE", &v)) filterReverse_ = v != 0;
    QueryString(key, L"FILTER_TEXT", filterText_, 0x400);
    if (QueryDword(key, L"CONN_AUTO", &v)) connAuto_ = v != 0;
    if (QueryDword(key, L"CONN_PORT", &v)) connPort_ = (int)v;
    QueryString(key, L"CONN_HOST", connHost_, 0x100);

    RegCloseKey(key);
}

void ConsoleWindow::SaveSettings() const {
    HKEY key = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, regKey_, 0, NULL, 0, 0x2001F, NULL, &key, NULL)
        != ERROR_SUCCESS) return;

    StoreDword(key, L"GEO_X", (DWORD)geoX_);
    StoreDword(key, L"GEO_Y", (DWORD)geoY_);
    StoreDword(key, L"GEO_WIDTH", (DWORD)geoW_);
    StoreDword(key, L"GEO_HEIGHT", (DWORD)geoH_);
    StoreDword(key, L"LOGVIEW_COLOR", (DWORD)background_);
    StoreDword(key, L"FONT_COLOR_ERROR", (DWORD)levelColor_[0]);
    StoreDword(key, L"FONT_COLOR_DEBUG", (DWORD)levelColor_[1]);
    StoreDword(key, L"FONT_COLOR_INFO", (DWORD)levelColor_[2]);
    StoreDword(key, L"FONT_COLOR_TRACE", (DWORD)levelColor_[3]);
    StoreBinary(key, L"LOG_FONT", &logFont_, sizeof(logFont_));
    StoreBinary(key, L"WINDOW_FONT", &windowFont_, sizeof(windowFont_));
    StoreDword(key, L"LOG_COUNT", (DWORD)logCount_);
    StoreDword(key, L"FILTER_ERROR", filterError_ ? 1 : 0);
    StoreDword(key, L"FILTER_DEBUG", filterDebug_ ? 1 : 0);
    StoreDword(key, L"FILTER_TRACE", filterTrace_ ? 1 : 0);
    StoreDword(key, L"FILTER_REVERSE", filterReverse_ ? 1 : 0);
    StoreString(key, L"FILTER_TEXT", filterText_);
    // The daemon passes param9 == NULL, so the original never persists the connection settings.
    if (param9_) {
        StoreDword(key, L"CONN_AUTO", connAuto_ ? 1 : 0);
        StoreDword(key, L"CONN_PORT", (DWORD)connPort_);
        StoreString(key, L"CONN_HOST", connHost_);
    }
    RegCloseKey(key);
}

// ---------------------------------------------------------------------------------------------
// Interface slots 0..16
// ---------------------------------------------------------------------------------------------
bool ConsoleWindow::Create(HINSTANCE hInstance, const wchar_t* title, const wchar_t* daemonName,
                           void* callback1, void (*exitCallback)(void), bool* reloadEnabled,
                           bool* shutdownEnabled, void* param9, void* hook) {
    instance_ = hInstance;
    wcscpy_s(title_, 256, title ? title : L"BnS Console");
    wcscpy_s(explicitTitle_, 256, title ? title : L"BnS Console");
    swprintf_s(regKey_, 512, L"Software\\Bloodlust\\%s", title ? title : L"BnS Console");
    callback1_ = callback1;
    exitCallback_ = exitCallback;
    reloadEnabled_ = reloadEnabled;
    shutdownEnabled_ = shutdownEnabled;
    param9_ = param9;
    hook_ = hook;                 // UNVERIFIED: stored, never dereferenced (host passes a `ret` stub)
    (void)daemonName;             // UNVERIFIED: original discards slot-0 arg 4

    SetDefaults();
    LoadSettings();
    return true;                  // the original returns 1 even when its inner allocation failed
}

void ConsoleWindow::Destroy() {
    SaveSettings();

    if (hwndMain_ && IsWindow(hwndMain_)) DestroyWindow(hwndMain_);
    hwndMain_ = NULL;
    hwndLog_ = hwndControl_ = hwndView_ = hwndStatusPane_ = NULL;
    hwndTool_ = hwndStatusBar_ = NULL;
    btnConnect_ = btnSetting_ = btnReload_ = btnShutdown_ = NULL;

    EnterCriticalSection(&cs_);
    if (lines_) { HeapFree(GetProcessHeap(), 0, lines_); lines_ = NULL; }
    if (snapshot_) { HeapFree(GetProcessHeap(), 0, snapshot_); snapshot_ = NULL; }
    linesCap_ = snapshotCap_ = linesCount_ = oldest_ = 0;
    LeaveCriticalSection(&cs_);

    if (fontLog_) { DeleteObject(fontLog_); fontLog_ = NULL; }
    if (fontWindow_) { DeleteObject(fontWindow_); fontWindow_ = NULL; }
    DeleteCriticalSection(&cs_);
}

void ConsoleWindow::ShowRun() {
    RegisterClasses();

    HWND main = CreateWindowExW(0, L"BnS Console:Main",
                                explicitTitle_[0] ? explicitTitle_ : L"BnS Console",
                                0x02CF0000, geoX_, geoY_, geoW_, geoH_,
                                NULL, NULL, module_, this);
    if (!main) return;
    hwndMain_ = main;             // published before ShowWindow so other threads can post to it
    ShowWindow(main, SW_SHOW);
    UpdateWindow(main);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        // The original has an accelerator table from resource 40000; this reconstruction has no
        // resources, so only dialog and standard key translation are performed.
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    hwndMain_ = NULL;
}

HWND ConsoleWindow::GetHwnd() {
    return hwndMain_;
}

void ConsoleWindow::SetTitle(const wchar_t* title) {
    wcsncpy_s(explicitTitle_, 256, title ? title : L"BnS Console", 255);
    if (hwndMain_ && IsWindow(hwndMain_)) SetWindowTextW(hwndMain_, explicitTitle_);
}

void ConsoleWindow::SetConnected(bool connected) {
    connected_ = connected;
    if (btnConnect_ && IsWindow(btnConnect_))
        SetWindowTextW(btnConnect_, connected ? L"Close" : L"Connect");
    SetTimedStatus(1, kTimerBusy, connected ? L"connected." : L"closed.", 3000);
}

void ConsoleWindow::EnableConnect(bool enable) {
    if (btnConnect_ && IsWindow(btnConnect_)) EnableWindow(btnConnect_, enable ? TRUE : FALSE);
    SetTimedStatus(1, kTimerBusy, enable ? L"connected." : L"connecting...", 3000);
}

void ConsoleWindow::EnableReload(bool enable) {
    if (btnReload_ && IsWindow(btnReload_)) EnableWindow(btnReload_, enable ? TRUE : FALSE);
    SetTimedStatus(1, kTimerBusy, enable ? L"reloading done." : L"reloading...", 3000);
}

void ConsoleWindow::EnableShutdown(bool enable) {
    if (btnShutdown_ && IsWindow(btnShutdown_)) EnableWindow(btnShutdown_, enable ? TRUE : FALSE);
    SetTimedStatus(1, kTimerBusy, enable ? L"shutdown done." : L"shutdowning...", 3000);
}

void ConsoleWindow::Connect() {
    if (hwndTool_ && connected_ && btnConnect_)
        SendMessageW(hwndMain_, WM_COMMAND, kIdConnectCmd, (LPARAM)btnConnect_);
}

void ConsoleWindow::Disconnect() {
    if (hwndTool_ && !connected_ && btnConnect_)
        SendMessageW(hwndMain_, WM_COMMAND, kIdConnectCmd, (LPARAM)btnConnect_);
}

void ConsoleWindow::SetStateText(const wchar_t* text) {
    SetStatusPart(0, text);
}

void ConsoleWindow::SetBusyText(const wchar_t* text, UINT timeoutMs) {
    SetTimedStatus(1, kTimerBusy, text, timeoutMs);
}

void ConsoleWindow::ShowMessage(const wchar_t* text, UINT timeoutMs) {
    SetTimedStatus(2, kTimerMessage, text, timeoutMs);
}

void ConsoleWindow::SetCounters(int a, int b, int c, int d, int e, int f) {
    EnterCriticalSection(&cs_);
    counters_[0] = a; counters_[1] = b; counters_[2] = c;
    counters_[3] = d; counters_[4] = e; counters_[5] = f;
    LeaveCriticalSection(&cs_);
    if (hwndMain_) PostMessageW(hwndMain_, kMsgRefresh, 0, 0);
}

void ConsoleWindow::EnqueueLogRecord(blood::LogBuffer* record) {
    if (!record) return;
    const RecordMirror* r = reinterpret_cast<const RecordMirror*>(record);
    const int level = r->level;
    // The original rejects records with (level-1) >= 5 or a false enabled flag (report 2.2/5).
    if (level < 1 || level > 5 || !r->enabled) return;

    wchar_t copy[0x7B9];
    size_t n = 0;
    while (n < 0x7B8 && r->text[n]) ++n;
    memcpy(copy, r->text, n * sizeof(wchar_t));
    copy[n] = 0;

    EnterCriticalSection(&cs_);
    if (linesCap_ == 0) GrowLinesLocked(logCount_ < 256 ? logCount_ : 256);
    if (linesCount_ == linesCap_) {
        if (linesCap_ < logCount_) {
            int next = linesCap_ * 2;
            GrowLinesLocked(next > logCount_ ? logCount_ : next);
        } else {
            // Drop the oldest line: circular scrollback.
            oldest_ = (oldest_ + 1) % linesCap_;
            --linesCount_;
        }
    }
    int slot = (oldest_ + linesCount_) % linesCap_;
    lines_[slot].level = level;
    memcpy(lines_[slot].text, copy, (n + 1) * sizeof(wchar_t));
    ++linesCount_;
    LeaveCriticalSection(&cs_);

    // Only a notification crosses threads - never the record pointer or its text.
    if (hwndMain_) PostMessageW(hwndMain_, kMsgRefresh, 0, 0);
}

blood::IConsoleWindow* ConsoleWindow::ResetAndDestroy() {
    // The original rewrites its vtable pointer and tail-calls slot 1; behaviourally identical.
    Destroy();
    return this;
}

// ---------------------------------------------------------------------------------------------
// Window classes, creation and layout
// ---------------------------------------------------------------------------------------------
void ConsoleWindow::RegisterClasses() const {
    static const wchar_t* const kClasses[] = {
        L"BnS Console:Main", L"BnS Console:Log", L"BnS Console:LogControl",
        L"BnS Console:LogView", L"BnS Console:Status", L"BnS Console:Tool",
        L"msctls_statusbar32"
    };
    for (size_t i = 0; i < sizeof(kClasses) / sizeof(kClasses[0]); ++i) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &ConsoleWindow::WndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 8;             // original stores the C++ object here
        wc.hInstance = module_;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kClasses[i];
        // If another module already registered the name, its class remains; the daemon only ever
        // loads one console DLL, so this is not observable in normal use.
        RegisterClassExW(&wc);
    }
}

static HWND MakeChild(HWND parent, const wchar_t* cls, int id, DWORD style, ConsoleWindow* self,
                      HMODULE module) {
    return CreateWindowExW(0, cls, L"", style, 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, module, self);
}

void ConsoleWindow::CreateChildren(HWND main) {
    hwndLog_ = MakeChild(main, L"BnS Console:Log", 0x100, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                         this, module_);
    hwndControl_ = MakeChild(hwndLog_, L"BnS Console:LogControl", 0x101,
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, this, module_);
    hwndView_ = MakeChild(hwndLog_, L"BnS Console:LogView", 0x102,
                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS,
                          this, module_);
    hwndStatusPane_ = MakeChild(hwndLog_, L"BnS Console:Status", 0x103,
                                WS_CHILD | WS_VISIBLE, this, module_);
    hwndTool_ = MakeChild(main, L"BnS Console:Tool", 0x104,
                          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, this, module_);
    hwndStatusBar_ = MakeChild(main, L"msctls_statusbar32", 0x105,
                               WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, this, module_);

    btnConnect_  = CreateWindowExW(0, L"Button", L"Connect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   0, 0, 0, 0, hwndTool_, (HMENU)(INT_PTR)kIdConnect, module_, NULL);
    btnSetting_  = CreateWindowExW(0, L"Button", L"Setting", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   0, 0, 0, 0, hwndTool_, (HMENU)(INT_PTR)kIdSetting, module_, NULL);
    btnReload_   = CreateWindowExW(0, L"Button", L"Reload", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   0, 0, 0, 0, hwndTool_, (HMENU)(INT_PTR)kIdReload, module_, NULL);
    btnShutdown_ = CreateWindowExW(0, L"Button", L"Shutdown", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   0, 0, 0, 0, hwndTool_, (HMENU)(INT_PTR)kIdShutdown, module_, NULL);

    struct { const wchar_t* text; WORD id; } ctl[] = {
        { L"scroll(&a)", kIdScrollTop }, { L"Clear", kIdClear },
        { L"&ERROR", kIdFilterError }, { L"&DEBUG", kIdFilterDebug },
        { L"&TRACE", kIdFilterTrace }, { L"re&verse", kIdReverse },
        { L"save log", kIdSaveLog }
    };
    for (size_t i = 0; i < sizeof(ctl) / sizeof(ctl[0]); ++i)
        CreateWindowExW(0, L"Button", ctl[i].text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        0, 0, 0, 0, hwndControl_, (HMENU)(INT_PTR)ctl[i].id, module_, NULL);

    ApplyLogFont();
    ApplyWindowFont();
    LayoutMain();
    UpdateScrollBars();
}

void ConsoleWindow::LayoutMain() {
    if (!hwndMain_ || !IsWindow(hwndMain_)) return;
    RECT rc; GetClientRect(hwndMain_, &rc);
    int w = rc.right, h = rc.bottom;
    const int statusH = 24, toolH = 36;
    if (hwndStatusBar_) MoveWindow(hwndStatusBar_, 0, h - statusH, w, statusH, TRUE);
    if (hwndTool_) MoveWindow(hwndTool_, 0, h - statusH - toolH, w, toolH, TRUE);
    if (hwndLog_) MoveWindow(hwndLog_, 0, 0, w, h - statusH - toolH, TRUE);
    if (hwndStatusBar_) {
        int edges[3] = { w / 3, 2 * w / 3, w };
        SendMessageW(hwndStatusBar_, kSbSetParts, 3, (LPARAM)edges);
    }
    LayoutTool();
}

void ConsoleWindow::LayoutLog() {
    if (!hwndLog_ || !IsWindow(hwndLog_)) return;
    RECT rc; GetClientRect(hwndLog_, &rc);
    if (hwndControl_) MoveWindow(hwndControl_, 0, 0, rc.right, 24, TRUE);
    if (hwndView_) MoveWindow(hwndView_, 0, 24, rc.right, rc.bottom > 24 ? rc.bottom - 24 : 0, TRUE);
    // The original has an extra status pane; keep it zero-sized in this reconstruction.
    if (hwndStatusPane_) MoveWindow(hwndStatusPane_, 0, rc.bottom, rc.right, 0, TRUE);
    LayoutControl();
    UpdateScrollBars();
}

void ConsoleWindow::LayoutTool() {
    HWND buttons[4] = { btnConnect_, btnSetting_, btnReload_, btnShutdown_ };
    for (int i = 0; i < 4; ++i)
        if (buttons[i]) MoveWindow(buttons[i], 4 + i * 88, 4, 84, 28, TRUE);
}

void ConsoleWindow::LayoutControl() {
    if (!hwndControl_ || !IsWindow(hwndControl_)) return;
    RECT rc; GetClientRect(hwndControl_, &rc);
    HWND child = GetWindow(hwndControl_, GW_CHILD);
    int i = 0;
    while (child) {
        MoveWindow(child, 4 + i * 88, 0, 84, 22, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
        ++i;
    }
}

// ---------------------------------------------------------------------------------------------
// Fonts, status bar and scrollback helpers
// ---------------------------------------------------------------------------------------------
void ConsoleWindow::ApplyLogFont() {
    if (fontLog_) DeleteObject(fontLog_);
    fontLog_ = CreateFontIndirectW(&logFont_);
    if (hwndView_ && fontLog_) SendMessageW(hwndView_, WM_SETFONT, (WPARAM)fontLog_, TRUE);
}

void ConsoleWindow::ApplyWindowFont() {
    if (fontWindow_) DeleteObject(fontWindow_);
    fontWindow_ = CreateFontIndirectW(&windowFont_);
    HWND ws[6] = { hwndMain_, hwndLog_, hwndControl_, hwndView_, hwndTool_, hwndStatusBar_ };
    for (int i = 0; i < 6; ++i)
        if (ws[i] && fontWindow_) SendMessageW(ws[i], WM_SETFONT, (WPARAM)fontWindow_, TRUE);
    if (hwndView_) { InvalidateRect(hwndView_, NULL, TRUE); UpdateScrollBars(); }
}

void ConsoleWindow::SetStatusPart(int part, const wchar_t* text) {
    if (hwndStatusBar_ && IsWindow(hwndStatusBar_))
        SendMessageW(hwndStatusBar_, kSbSetText, (WPARAM)part, (LPARAM)(text ? text : L""));
}

void ConsoleWindow::SetTimedStatus(int part, UINT timerId, const wchar_t* text, UINT timeoutMs) {
    SetStatusPart(part, text);
    if (timeoutMs > 0 && hwndMain_ && IsWindow(hwndMain_))
        SetTimer(hwndMain_, timerId, timeoutMs, NULL);
}

void ConsoleWindow::GrowLinesLocked(int wanted) {
    if (wanted <= linesCap_) return;
    if (wanted < 1) wanted = 1;
    LogLine* fresh = (LogLine*)HeapAlloc(GetProcessHeap(), 0, sizeof(LogLine) * (size_t)wanted);
    if (!fresh) return;
    for (int i = 0; i < linesCount_; ++i)
        fresh[i] = lines_[(oldest_ + i) % linesCap_];
    if (lines_) HeapFree(GetProcessHeap(), 0, lines_);
    lines_ = fresh;
    linesCap_ = wanted;
    oldest_ = 0;
}

void ConsoleWindow::ClearLines() {
    EnterCriticalSection(&cs_);
    linesCount_ = 0;
    oldest_ = 0;
    LeaveCriticalSection(&cs_);
    if (hwndView_) { InvalidateRect(hwndView_, NULL, TRUE); UpdateScrollBars(); }
}

void ConsoleWindow::ToggleFilter(int which) {
    switch (which) {
        case 0: filterError_ = !filterError_; break;
        case 1: filterDebug_ = !filterDebug_; break;
        case 2: filterTrace_ = !filterTrace_; break;
        case 3: filterReverse_ = !filterReverse_; break;
    }
    if (hwndView_) { InvalidateRect(hwndView_, NULL, TRUE); UpdateScrollBars(); }
}

void ConsoleWindow::UpdateScrollBars() {
    if (!hwndView_ || !IsWindow(hwndView_)) return;
    int count = 0;
    EnterCriticalSection(&cs_); count = linesCount_; LeaveCriticalSection(&cs_);

    HDC dc = GetDC(hwndView_);
    int lineHeight = 16;
    if (dc) {
        TEXTMETRICW tm;
        if (GetTextMetricsW(dc, &tm)) lineHeight = tm.tmHeight + 2;
        ReleaseDC(hwndView_, dc);
    }
    RECT rc; GetClientRect(hwndView_, &rc);
    int page = (lineHeight > 0) ? (rc.bottom / lineHeight) : 1;
    if (page < 1) page = 1;

    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = count > 0 ? count - 1 : 0;
    si.nPage = (UINT)page;
    SetScrollInfo(hwndView_, SB_VERT, &si, TRUE);

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = 1024;
    si.nPage = 80;
    SetScrollInfo(hwndView_, SB_HORZ, &si, TRUE);
}

// ---------------------------------------------------------------------------------------------
// Painting - double buffered, one DrawTextW per visible line (report section 4.2)
// ---------------------------------------------------------------------------------------------
void ConsoleWindow::PaintLogView(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (!hdc) return;

    RECT rc; GetClientRect(hwnd, &rc);
    int width = rc.right, height = rc.bottom;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, width > 0 ? width : 1, height > 0 ? height : 1);
    if (!mem || !bmp) {
        HBRUSH fb = CreateSolidBrush(background_);
        FillRect(hdc, &rc, fb);
        DeleteObject(fb);
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    HGDIOBJ oldFont = (fontLog_ && mem) ? SelectObject(mem, fontLog_) : NULL;
    HBRUSH brush = CreateSolidBrush(background_);
    SetBkMode(mem, TRANSPARENT);
    FillRect(mem, &rc, brush);

    TEXTMETRICW tm;
    int lineHeight = 16;
    if (GetTextMetricsW(mem, &tm)) lineHeight = tm.tmHeight + 2;
    if (lineHeight < 1) lineHeight = 16;

    SCROLLINFO siV, siH;
    memset(&siV, 0, sizeof(siV)); siV.cbSize = sizeof(siV); siV.fMask = SIF_POS;
    memset(&siH, 0, sizeof(siH)); siH.cbSize = sizeof(siH); siH.fMask = SIF_POS;
    GetScrollInfo(hwnd, SB_VERT, &siV);
    GetScrollInfo(hwnd, SB_HORZ, &siH);
    int first = siV.nPos > 0 ? siV.nPos : 0;
    int hskip = siH.nPos > 0 ? siH.nPos : 0;

    int take = 0, total = 0, cap = 0;
    EnterCriticalSection(&cs_);
    total = linesCount_;
    cap = linesCap_;
    int want = (height / lineHeight) + 2;
    if (want < 1) want = 1;
    if (want > total - first) want = total - first;
    if (want > 0) {
        if (want > snapshotCap_) {
            DrawLine* fresh = (DrawLine*)HeapAlloc(GetProcessHeap(), 0, sizeof(DrawLine) * (size_t)want);
            if (fresh) {
                if (snapshot_) HeapFree(GetProcessHeap(), 0, snapshot_);
                snapshot_ = fresh;
                snapshotCap_ = want;
            }
        }
        take = want < snapshotCap_ ? want : snapshotCap_;
        for (int i = 0; i < take; ++i) {
            int logical = first + i;
            int physical = filterReverse_ ? (oldest_ + linesCount_ - 1 - logical + cap) % cap
                                          : (oldest_ + logical + cap) % cap;
            if (logical < 0 || logical >= total || cap == 0 || physical < 0 || physical >= cap) {
                snapshot_[i].level = 0;
                snapshot_[i].text[0] = 0;
                continue;
            }
            snapshot_[i].level = lines_[physical].level;
            memcpy(snapshot_[i].text, lines_[physical].text, sizeof(snapshot_[i].text));
        }
    }
    LeaveCriticalSection(&cs_);

    int y = 0;
    for (int i = 0; i < take; ++i) {
        const DrawLine& line = snapshot_[i];
        if (line.text[0] == 0) { y += lineHeight; continue; }

        bool visible = true;
        // Accepted levels are 1..5; the first three map to the ERROR/DEBUG/TRACE filter toggles.
        if (line.level == 1 && !filterError_) visible = false;
        if (line.level == 2 && !filterDebug_) visible = false;
        if (line.level == 3 && !filterTrace_) visible = false;
        if (visible && filterText_[0]) {
            // UNVERIFIED: the original uses PCRE; this reconstruction does a case-insensitive
            // plain substring match, sufficient for the filter strings the daemon may supply.
            const wchar_t* hay = line.text;
            const wchar_t* needle = filterText_;
            visible = false;
            while (*hay) {
                const wchar_t* h = hay, *n = needle;
                while (*h && *n && towlower(*h) == towlower(*n)) { ++h; ++n; }
                if (!*n) { visible = true; break; }
                ++hay;
            }
        }
        if (!visible) { y += lineHeight; continue; }
        if (y >= height) break;

        size_t len = wcslen(line.text);
        const wchar_t* shown = line.text;
        if (hskip > 0 && hskip < (int)len) shown = line.text + hskip;

        RECT row = { 0, y, width, y + lineHeight };
        int idx = line.level - 1;
        if (idx < 0) idx = 0;
        if (idx > 3) idx = 3;
        SetTextColor(mem, levelColor_[idx]);
        DrawTextW(mem, shown, -1, &row, DT_SINGLELINE | DT_NOPREFIX);
        y += lineHeight;
    }

    if (mem && bmp) BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
    DeleteObject(brush);
    if (oldFont) SelectObject(mem, oldFont);
    if (oldBmp) SelectObject(mem, oldBmp);
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    EndPaint(hwnd, &ps);
    KillTimer(hwnd, 0xB);
}

// ---------------------------------------------------------------------------------------------
// WndProc dispatch
// ---------------------------------------------------------------------------------------------
LRESULT CALLBACK ConsoleWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ConsoleWindow* self = NULL;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<ConsoleWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        // Children are created from the main window's WM_CREATE, so hwndMain_ must be known
        // before that message is dispatched.
        wchar_t cls[64];
        if (GetClassNameW(hwnd, cls, 64) && wcscmp(cls, L"BnS Console:Main") == 0)
            self->hwndMain_ = hwnd;
    } else {
        self = reinterpret_cast<ConsoleWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    if (hwnd == self->hwndStatusBar_) return self->OnStatusBar(hwnd, msg, wp, lp);
    if (hwnd == self->hwndView_) return self->OnLogView(hwnd, msg, wp, lp);
    if (hwnd == self->hwndMain_) return self->OnMain(hwnd, msg, wp, lp);
    if (hwnd == self->hwndLog_ || hwnd == self->hwndControl_ || hwnd == self->hwndTool_)
        return self->OnChild(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ConsoleWindow::OnMain(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            hwndMain_ = hwnd;      // children are created here, before CreateWindowExW returns
            CreateChildren(hwnd);
            return 0;
        case WM_SIZE:
            LayoutMain();
            return 0;
        case WM_CLOSE:
            // The host exit callback sets the daemon's shutdown event (report section 4.2).
            if (exitCallback_) exitCallback_();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_TIMER:
            if (wp >= kTimerState && wp <= kTimerMessage) {
                KillTimer(hwnd, wp);
                SetStatusPart((int)(wp - kTimerState), L"");
                return 0;
            }
            break;
        case kMsgRefresh:
            if (hwndView_) { InvalidateRect(hwndView_, NULL, FALSE); UpdateScrollBars(); }
            return 0;
        case kMsgConnect:
            if (btnConnect_) SetFocus(btnConnect_);
            return 0;
        case WM_COMMAND: {
            WORD id = LOWORD(wp);
            switch (id) {
                case kIdConnect:
                case kIdConnectCmd: {
                    connected_ = !connected_;
                    if (btnConnect_) SetWindowTextW(btnConnect_, connected_ ? L"Close" : L"Connect");
                    SetTimedStatus(1, kTimerBusy, connected_ ? L"connected." : L"closed.", 3000);
                    return 0;
                }
                case kIdSetting:
                    ShowSettings();
                    return 0;
                case kIdReload:
                    if (!reloadEnabled_ || *reloadEnabled_) {
                        SetTimedStatus(1, kTimerBusy, L"reloading...", 3000);
                        SetTimedStatus(1, kTimerBusy, L"reloading done.", 3000);
                    }
                    return 0;
                case kIdShutdown:
                    if (!shutdownEnabled_ || *shutdownEnabled_) {
                        SetTimedStatus(1, kTimerBusy, L"shutdowning...", 3000);
                        if (exitCallback_) exitCallback_();
                    }
                    return 0;
                case kIdClear:
                    ClearLines();
                    return 0;
                case kIdScrollTop: {
                    if (hwndView_) {
                        SCROLLINFO si; memset(&si, 0, sizeof(si));
                        si.cbSize = sizeof(si); si.fMask = SIF_POS; si.nPos = 0;
                        SetScrollInfo(hwndView_, SB_VERT, &si, TRUE);
                        InvalidateRect(hwndView_, NULL, FALSE);
                    }
                    return 0;
                }
                case kIdFilterError: ToggleFilter(0); return 0;
                case kIdFilterDebug: ToggleFilter(1); return 0;
                case kIdFilterTrace: ToggleFilter(2); return 0;
                case kIdReverse:     ToggleFilter(3); return 0;
                case kIdSaveLog:     SaveLog(); return 0;
            }
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ConsoleWindow::OnLogView(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (msg) {
        case WM_PAINT:
            PaintLogView(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;              // background is painted in the double buffer
        case WM_VSCROLL: case WM_HSCROLL: {
            int bar = (msg == WM_VSCROLL) ? SB_VERT : SB_HORZ;
            SCROLLINFO si; memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, bar, &si);
            int old = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP:   si.nPos -= 1; break;
                case SB_LINEDOWN: si.nPos += 1; break;
                case SB_PAGEUP:   si.nPos -= si.nPage; break;
                case SB_PAGEDOWN: si.nPos += si.nPage; break;
                case SB_THUMBTRACK: si.nPos = HIWORD(wp); break;
                case SB_TOP:    si.nPos = si.nMin; break;
                case SB_BOTTOM: si.nPos = si.nMax; break;
            }
            if (si.nPos < si.nMin) si.nPos = si.nMin;
            if (si.nPos > si.nMax) si.nPos = si.nMax;
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, bar, &si, TRUE);
            if (si.nPos != old) InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            SCROLLINFO si; memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            si.nPos -= delta / 120;
            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ConsoleWindow::OnStatusBar(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kSbSetParts) {
        int n = (int)wp < 3 ? (int)wp : 3;
        const int* edges = reinterpret_cast<const int*>(lp);
        if (edges) for (int i = 0; i < n; ++i) statusEdges_[i] = edges[i];
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    if (msg == kSbSetText) {
        int part = (int)wp;
        if (part < 0 || part > 2) part = 0;
        wcsncpy_s(statusText_[part], 512, (const wchar_t*)lp, 511);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc) {
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            FillRect(dc, &rc, brush);
            DeleteObject(brush);
            int left = 0;
            for (int i = 0; i < 3; ++i) {
                int right = statusEdges_[i] > left && statusEdges_[i] <= rc.right ? statusEdges_[i] : rc.right;
                RECT part = { left + 3, 0, right - 3, rc.bottom };
                DrawTextW(dc, statusText_[i], -1, &part, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                if (i < 2 && right < rc.right) {
                    MoveToEx(dc, right, 0, NULL);
                    LineTo(dc, right, rc.bottom);
                }
                left = right;
                if (i == 2) break;
            }
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ConsoleWindow::OnChild(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND && hwndMain_) {
        SendMessageW(hwndMain_, WM_COMMAND, wp, lp);
        return 0;
    }
    if (msg == WM_SIZE) {
        if (hwnd == hwndLog_) LayoutLog();
        else if (hwnd == hwndTool_) LayoutTool();
        else if (hwnd == hwndControl_) LayoutControl();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------------------------
// Settings dialog ("Console Setting") - modeless child windows in the #32770 dialog frame.
// ---------------------------------------------------------------------------------------------
LRESULT CALLBACK ConsoleWindow::SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ConsoleWindow* self = NULL;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<ConsoleWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<ConsoleWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_CREATE: {
            struct { const wchar_t* text; int id; int x, y, w; } items[] = {
                { L"Backgroud", 100, 10,  12, 110 },
                { L"ERROR",     101, 10,  44, 110 },
                { L"DEBUG",     102, 10,  76, 110 },
                { L"TRACE",     103, 10, 108, 110 },
                { L"Log Font...",    104, 130,  12, 130 },
                { L"Window Font...", 105, 130,  44, 130 },
                { L"Restore Default",107, 130, 108, 130 },
                { L"Cancel",        108, 130, 140, 130 },
            };
            for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i)
                CreateWindowExW(0, L"Button", items[i].text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                items[i].x, items[i].y, items[i].w, 26, hwnd,
                                (HMENU)(INT_PTR)items[i].id, self->module_, NULL);
            CreateWindowExW(0, L"Static", L"Buffer", WS_CHILD | WS_VISIBLE,
                            280, 14, 60, 20, hwnd, NULL, self->module_, NULL);
            wchar_t buf[32];
            swprintf_s(buf, 32, L"%d", self->logCount_);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", buf, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            280, 40, 110, 24, hwnd, (HMENU)(INT_PTR)106, self->module_, NULL);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id >= 100 && id <= 103) {
                COLORREF* target = (id == 100) ? &self->background_ : &self->levelColor_[id - 101];
                CHOOSECOLORW cc; memset(&cc, 0, sizeof(cc));
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd;
                cc.rgbResult = *target;
                cc.lpCustColors = self->customColors_;
                cc.Flags = 3;                 // CC_RGBINIT | CC_FULLOPEN
                if (ChooseColorW(&cc)) {
                    *target = cc.rgbResult;
                    if (id == 100 && self->hwndView_) InvalidateRect(self->hwndView_, NULL, TRUE);
                }
                return 0;
            }
            if (id == 104 || id == 105) {
                LOGFONTW* font = (id == 104) ? &self->logFont_ : &self->windowFont_;
                CHOOSEFONTW cf; memset(&cf, 0, sizeof(cf));
                cf.lStructSize = sizeof(cf);
                cf.hwndOwner = hwnd;
                cf.lpLogFont = font;
                cf.Flags = (id == 104) ? 0x41 : 0x2041;
                if (ChooseFontW(&cf)) {
                    if (id == 104) self->ApplyLogFont(); else self->ApplyWindowFont();
                }
                return 0;
            }
            if (id == 107) {                  // Restore Default
                self->SetDefaults();
                self->ApplyLogFont();
                self->ApplyWindowFont();
                HWND edit = GetDlgItem(hwnd, 106);
                if (edit) { wchar_t b[32]; swprintf_s(b, 32, L"%d", self->logCount_); SetWindowTextW(edit, b); }
                if (self->hwndView_) InvalidateRect(self->hwndView_, NULL, TRUE);
                return 0;
            }
            if (id == 106) {                  // buffer size changed
                HWND edit = (HWND)lp;
                wchar_t b[32];
                if (edit && GetWindowTextW(edit, b, 32)) {
                    int n = _wtoi(b);
                    if (n >= 1 && n <= 10000000) self->logCount_ = n;
                }
                return 0;
            }
            if (id == 108) { DestroyWindow(hwnd); return 0; }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ConsoleWindow::ShowSettings() {
    if (hdlgSettings_ && IsWindow(hdlgSettings_)) { SetForegroundWindow(hdlgSettings_); return; }
    HWND parent = hwndMain_;
    if (parent) EnableWindow(parent, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"#32770",
                               L"Console Setting", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, 410, 200, parent, NULL, module_, this);
    if (!dlg) { if (parent) EnableWindow(parent, TRUE); return; }
    hdlgSettings_ = dlg;
    ShowWindow(dlg, SW_SHOW);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    hdlgSettings_ = NULL;
    if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
}

// ---------------------------------------------------------------------------------------------
// Save log as CP_ACP, one "%s\n" line per record (report section 4.4 / 5).
// ---------------------------------------------------------------------------------------------
void ConsoleWindow::SaveLog() {
    wchar_t path[MAX_PATH];
    memset(path, 0, sizeof(path));
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndMain_;
    ofn.lpstrFilter = L"Log Files (*.log)\0*.log\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = L".";
    ofn.Flags = 0x80000A;           // OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY
    if (!GetSaveFileNameW(&ofn)) return;

    SetTimedStatus(2, kTimerMessage, L"saving...", 3000);

    EnterCriticalSection(&cs_);
    int count = linesCount_;
    LogLine* copy = (LogLine*)HeapAlloc(GetProcessHeap(), 0, sizeof(LogLine) * (size_t)(count > 0 ? count : 1));
    for (int i = 0; i < count; ++i) copy[i] = lines_[(oldest_ + i) % linesCap_];
    LeaveCriticalSection(&cs_);

    if (count < 1) {
        SetTimedStatus(2, kTimerMessage, L"nothing to save.", 10000);
        if (copy) HeapFree(GetProcessHeap(), 0, copy);
        return;
    }

    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"wt") != 0 || !f) {
        wchar_t msg[512];
        swprintf_s(msg, 512, L"[%d] Cannot open file '%s'", GetLastError(), path);
        SetTimedStatus(2, kTimerMessage, msg, 10000);
        if (copy) HeapFree(GetProcessHeap(), 0, copy);
        return;
    }
    char ansi[4000];
    for (int i = 0; i < count; ++i) {
        ansi[0] = 0;
        WideCharToMultiByte(CP_ACP, 0, copy[i].text, -1, ansi, (int)sizeof(ansi), NULL, NULL);
        fprintf(f, "%s\n", ansi);
    }
    fclose(f);
    if (copy) HeapFree(GetProcessHeap(), 0, copy);
    SetTimedStatus(2, kTimerMessage, L"save done.", 3000);
}

} // namespace bsconsole
