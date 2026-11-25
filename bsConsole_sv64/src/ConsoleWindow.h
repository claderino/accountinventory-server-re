// Private implementation of the reconstructed bsConsole_sv64.dll window object.
// The public contract is "../RankingDaemon/src/BloodLib/BsConsole.h" (frozen, not edited here).
#pragma once

#include "BsConsole.h"

namespace bsconsole {

// One scrollback line copied out of a daemon LogBuffer at enqueue time.  The daemon may free its
// record as soon as EnqueueLogRecord returns, so the window never keeps the record pointer.
struct LogLine {
    int level;
    wchar_t text[0x7B9];           // 0x7B8 characters + NUL (host limit)
};

// Snapshot used by the paint code so that the critical section is never held during GDI calls.
struct DrawLine {
    int level;
    wchar_t text[0x7B9];
};

class ConsoleWindow : public blood::IConsoleWindow {
public:
    explicit ConsoleWindow(HMODULE module);

    // 17 interface slots, in the exact order of the original vtable at RVA 0x184D0.
    bool Create(HINSTANCE hInstance, const wchar_t* title, const wchar_t* daemonName,
                void* callback1, void (*exitCallback)(void), bool* reloadEnabled,
                bool* shutdownEnabled, void* param9, void* hook) override;
    void Destroy() override;
    void ShowRun() override;
    HWND GetHwnd() override;
    void SetTitle(const wchar_t* title) override;
    void SetConnected(bool connected) override;
    void EnableConnect(bool enable) override;
    void EnableReload(bool enable) override;
    void EnableShutdown(bool enable) override;
    void Connect() override;
    void Disconnect() override;
    void SetStateText(const wchar_t* text) override;
    void SetBusyText(const wchar_t* text, UINT timeoutMs) override;
    void ShowMessage(const wchar_t* text, UINT timeoutMs) override;
    void EnqueueLogRecord(blood::LogBuffer* record) override;
    void SetCounters(int a, int b, int c, int d, int e, int f) override;
    blood::IConsoleWindow* ResetAndDestroy() override;

protected:
    // Like the contract: protected, non-virtual.  Objects are disposed of by Destroy() followed
    // by DestroyConsoleWindow(), never by `delete` through the interface.
    ~ConsoleWindow() {}

private:
    ConsoleWindow(const ConsoleWindow&);
    ConsoleWindow& operator=(const ConsoleWindow&);

    void LoadSettings();
    void SaveSettings() const;
    void SetDefaults();
    void RegisterClasses() const;

    void CreateChildren(HWND main);
    void LayoutMain();
    void LayoutLog();
    void LayoutTool();
    void LayoutControl();

    LRESULT OnMain(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnLogView(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnStatusBar(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnChild(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void PaintLogView(HWND hwnd);
    void UpdateScrollBars();
    void ClearLines();

    void ShowSettings();
    void SaveLog();
    void ApplyLogFont();
    void ApplyWindowFont();
    void SetStatusPart(int part, const wchar_t* text);
    void SetTimedStatus(int part, UINT timerId, const wchar_t* text, UINT timeoutMs);
    void ToggleFilter(int which);
    void GrowLinesLocked(int wanted);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static void CALLBACK DummyTimer(HWND, UINT, UINT_PTR, DWORD) {}

    HMODULE module_;
    HINSTANCE instance_;
    wchar_t title_[256];
    wchar_t explicitTitle_[256];
    wchar_t regKey_[512];
    void* callback1_;
    void (*exitCallback_)(void);
    bool* reloadEnabled_;
    bool* shutdownEnabled_;
    void* param9_;
    void* hook_;
    bool connected_;

    volatile HWND hwndMain_;
    HWND hwndLog_;
    HWND hwndControl_;
    HWND hwndView_;
    HWND hwndStatusPane_;
    HWND hwndTool_;
    HWND hwndStatusBar_;
    HWND btnConnect_;
    HWND btnSetting_;
    HWND btnReload_;
    HWND btnShutdown_;
    HWND hdlgSettings_;

    HFONT fontLog_;
    HFONT fontWindow_;
    LOGFONTW logFont_;
    LOGFONTW windowFont_;
    COLORREF customColors_[16];

    int geoX_, geoY_, geoW_, geoH_;
    COLORREF background_;
    COLORREF levelColor_[4];       // ERROR, DEBUG, INFO, TRACE
    int logCount_;
    bool filterError_;
    bool filterDebug_;
    bool filterTrace_;
    bool filterReverse_;
    wchar_t filterText_[0x400];
    bool connAuto_;
    int connPort_;
    wchar_t connHost_[0x100];

    CRITICAL_SECTION cs_;
    LogLine* lines_;               // circular scrollback, allocated lazily (can grow)
    int linesCap_;
    int oldest_;                   // logical line 0 lives at this physical slot
    int linesCount_;
    DrawLine* snapshot_;
    int snapshotCap_;
    int counters_[6];

    wchar_t statusText_[3][512];
    int statusEdges_[3];
};

} // namespace bsconsole
