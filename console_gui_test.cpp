#define main previous_review_gui_main
#include "review_gui_test.cpp"
#undef main
static void clipboard_path(const std::wstring& value) {
    bool opened = false;
    for (int n = 0; n < 100 && !opened; ++n) { opened = OpenClipboard(g.hwnd) != FALSE; if (!opened) Sleep(10); }
    assert(opened); assert(EmptyClipboard());
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t)); assert(mem);
    auto data = GlobalLock(mem); assert(data); memcpy(data, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(mem); assert(SetClipboardData(CF_UNICODETEXT, mem)); CloseClipboard();
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    g.inst = GetModuleHandleW(nullptr); g.measureDC = CreateCompatibleDC(nullptr); g.scale = 1; g.dpi = 96;
    wchar_t cwd[32768]; GetCurrentDirectoryW(32768, cwd); g.exeDir = cwd;
    std::wstring dir = g.exeDir + L"\\console_gui_data"; CreateDirectoryW(dir.c_str(), nullptr);
    g.iniPath = dir + L"\\test.ini"; g.sessionFile = dir + L"\\test.csv"; g.jumpsDir = dir;
    std::wstring source = dir + L"\\консоль7.log", missing = dir + L"\\missing.log";
    DeleteFileW(missing.c_str());
    g.tail.select(missing);
    g.classicMenu = false; apply_theme(); rebuild_fonts();
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = g.inst; wc.lpszClassName = L"LJOverlayWnd";
    assert(RegisterClassW(&wc));
    g.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Console source test", WS_POPUP,
        20, 20, 800, 600, nullptr, nullptr, g.inst, nullptr); assert(g.hwnd);
    SetLayeredWindowAttributes(g.hwnd, 0, 255, LWA_ALPHA); ShowWindow(g.hwnd, SW_SHOWNOACTIVATE);
    on_log_timer(); snapshot("console_modern_error.bmp");
    assert(has(L"LOG ERROR") && has(L"-condebug") && !has(L"Жду прыжок") && !has(L"ЖДУ ПРЫЖОК", g.fPill));
    menu_toggle(); menu_select(9); menu_select(10); assert(g.menuPage == MENU_LOG);
    menu_select(2); assert(g.logEditing);
    while (!g.logDraft.empty()) SendMessageW(g.hwnd, WM_CHAR, VK_BACK, 0);
    for (wchar_t ch : source) {
        if (ch >= '0' && ch <= '9') SendMessageW(g.hwnd, WM_KEYDOWN, ch, 0);
        SendMessageW(g.hwnd, WM_CHAR, ch, 0);
    }
    assert(g.logDraft == source && g.menuPage == MENU_LOG);
    std::string data = godlike;
    for (int n = 0; n < 12000; ++n) data += "unrelated console padding line to keep history reading for multiple polls\n";
    data += meh;
    assert(write_file(source, data.data(), data.size()));
    SendMessageW(g.hwnd, WM_KEYDOWN, VK_RETURN, 0);
    assert(!g.logEditing && g.tail.path == source && ini_get(L"overlay", L"log", L"") == source);
    assert(console_state() == "LOG CONNECTED"); snapshot("console_modern_connected.bmp"); assert(has(L"LOG CONNECTED"));
    on_log_timer(); assert(g.tail.readingHistory); snapshot("console_modern_reading.bmp"); assert(has(L"READING HISTORY"));
    for (int n = 0; n < 20 && g.tail.readingHistory; ++n) on_log_timer();
    assert(g.jumps.size() == 2 && console_state() == "LIVE");
    menu_toggle(); snapshot("console_modern_live.bmp"); assert(has(L"LIVE") && has(L"217.50", g.fBig));
    g.classicMenu = true; apply_theme(); rebuild_fonts(); relayout(); snapshot("console_cs16_live.bmp"); assert(has(L"LIVE"));
    select_attempt(0); auto chosen = g.selectedAttempt;
    menu_toggle(); menu_select(9); menu_select(10); menu_select(2);
    g.logDraft = missing; SendMessageW(g.hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
    assert(!g.logEditing && g.tail.path == source);
    std::wstring second = dir + L"\\second.log"; std::string other = "11/17 01:02:03 " + std::string(meh);
    assert(write_file(second, other.data(), other.size()));
    clipboard_path(second); menu_select(1); on_log_timer();
    assert(g.tail.path == second && g.jumps.size() == 3 && g.selectedAttempt == chosen);
    clipboard_path(L"bad\npath"); menu_select(1);
    assert(g.tail.path == second && !g.logEditError.empty());
    clipboard_path(missing); menu_select(1); on_log_timer(); snapshot("console_cs16_error_settings.bmp");
    assert(has(L"LOG ERROR") && has(L"-condebug"));
    assert(g.jumps.size() == 3 && g.selectedAttempt == chosen);
    int windows = 0;
    EnumWindows([](HWND h, LPARAM data) -> BOOL {
        DWORD id; GetWindowThreadProcessId(h, &id);
        if (id == GetCurrentProcessId() && IsWindowVisible(h)) ++*(int*)data;
        return TRUE;
    }, (LPARAM)&windows);
    assert(windows == 1); // No file picker, edit control, message box or CONTROL window.
    int children = 0;
    EnumChildWindows(g.hwnd, [](HWND, LPARAM data) -> BOOL { ++*(int*)data; return TRUE; }, (LPARAM)&children);
    assert(children == 0);
    DestroyWindow(g.hwnd);
    puts("PASS actual GDI statuses LOG ERROR/LOG CONNECTED/READING HISTORY/LIVE; both themes; expected source + condebug guide; inline Unicode path typing including digits, Enter/Escape, INI persistence, clipboard selection, invalid input; source switch retains selected history; exactly one window");
}
