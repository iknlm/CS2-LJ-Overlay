#include "lj_overlay.cpp"
#include <cassert>
static void click(const RECT& r) {
    SendMessageW(g.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(r.left + 2, r.top + 2));
}
int main() {
    g.inst = GetModuleHandleW(nullptr); g.measureDC = CreateCompatibleDC(nullptr);
    wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
    g.exeDir = cwd; g.iniPath = g.exeDir + L"\\analysis_test.ini";
    g.sessionFile = g.exeDir + L"\\analysis_test.csv";
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = g.inst; wc.lpszClassName = L"LJAnalysisTest";
    assert(RegisterClassW(&wc));
    g.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Analysis test", WS_POPUP,
                            10, 10, 800, 600, nullptr, nullptr, g.inst, nullptr);
    assert(g.hwnd && !g.analysisOpen);
    std::string cmd;
    g.parser.feed("JS jumped 239.96712 units with a Long Jump", cmd);
    g.parser.feed("VNL | 2 Strafes | 250 Pre", cmd);
    g.analyses.resize(1);
    ensure_attempt_ids(); // Direct Parser fixtures need the same IDs as the production log pump.
    auto csv = build_csv(g.jumps);
    for (bool classic : {false, true}) {
        g.classicMenu = classic; apply_theme(); rebuild_fonts(); relayout();
        HWND same = g.hwnd;
        click(g.analysisButton); assert(g.analysisOpen);
        assert(g.analyses[0].empty()); // Painting does not generate a partial report.
        click(g.analysisButton); assert(!g.analysisOpen && g.hwnd == same);
    }
    g.parser.curT -= std::chrono::seconds(4);
    on_log_timer(); assert(g.analyses.size() == 1 && !g.analyses[0].empty());
    auto first = g.analyses[0]; auto* stored = g.analyses[0].data();
    on_log_timer(); on_log_timer();
    assert(g.analyses[0] == first && g.analyses[0].data() == stored);
    assert(build_csv(g.jumps) == csv);
    // New block closes previous block; no report on every UPD.
    g.tail.path = g.exeDir + L"\\analysis_feed.log"; g.tail.pos = 0;
    std::string data = "JS jumped 235.12345 units with a Long Jump\nVNL | 3 Strafes | 250 Pre\n";
    assert(write_file(g.tail.path, data.data(), data.size()));
    on_log_timer(); assert(g.jumps.size() == 2 && g.analyses[1].empty());
    data += "#. Sync Gain Loss GainEff Overlap DeadAir BadAngles\n1. 85% 10 0 60% 0% 0% 0%\n";
    assert(write_file(g.tail.path, data.data(), data.size()));
    on_log_timer(); assert(g.jumps[1].strafes.size() == 1 && g.analyses[1].empty());
    select_attempt(1); copy_analysis(); assert(g.analysisCopyStatus == "Отчёт ещё не готов");
    data += "2. 40% 8 -3 20% 25% 20% 40%\n3. 85% 10 0 60% 0% 0% 0%\nLEFT KEY | LLL\n";
    assert(write_file(g.tail.path, data.data(), data.size()));
    on_log_timer(); assert(g.jumps[1].strafes.size() == 3 && g.analyses[1].empty());
    copy_analysis(); assert(g.analysisCopyStatus == "Отчёт ещё не готов");
    // Even a complete table is not final while the block can still receive UPD.
    data += "JS jumped 219.07756 units with a Long Jump\nVNL | 2 Strafes | 250 Pre\n";
    assert(write_file(g.tail.path, data.data(), data.size()));
    on_log_timer(); assert(g.jumps.size() == 3 && !g.analyses[1].empty() && g.analyses[2].empty());
    assert(analysis_index() == 1); // Arrival of NEW must not change pinned selection.
    for (bool classic : {false, true}) {
        g.classicMenu = classic; apply_theme(); rebuild_fonts(); relayout();
        assert(g.analyses[1] == analyze_jump(g.jumps[1]));
    }
    menu_toggle(); menu_select(8); menu_select(3); // Choose oldest, not latest.
    assert(g.selectedAttempt == g.jumps[0].attemptId && !g.analysisOpen);
    menu_select(10); assert(!g.settingsOpen);
    relayout(); click(g.analysisCopy);
    assert(g.analysisCopyStatus == "Скопировано");
    assert(OpenClipboard(g.hwnd));
    HANDLE mem = GetClipboardData(CF_UNICODETEXT); assert(mem);
    auto wide = (const wchar_t*)GlobalLock(mem); assert(wide);
    std::wstring copied = wide; GlobalUnlock(mem); CloseClipboard();
    assert(copied.find(L"239.96712u") != std::wstring::npos && copied.find(L"0.03288u") != std::wstring::npos);
    assert(copied.find(L"235.12345") == std::wstring::npos && copied.find(L"219.07756") == std::wstring::npos);
    assert(copied.find(L"Недостаточно данных") != std::wstring::npos && copied.find(L"Проверь") == std::wstring::npos);
    menu_toggle(); menu_select(8); menu_select(2); menu_select(10);
    assert(analysis_index() == 1);
    relayout(); click(g.analysisCopy); assert(g.analysisCopyStatus == "Скопировано");
    assert(OpenClipboard(g.hwnd)); mem = GetClipboardData(CF_UNICODETEXT);
    wide = (const wchar_t*)GlobalLock(mem); assert(wide); copied = wide; GlobalUnlock(mem); CloseClipboard();
    assert(copied.find(L"235.12345u") != std::wstring::npos && copied.find(L"Sync 40%") != std::wstring::npos);
    assert(copied.find(L"219.07756") == std::wstring::npos && copied.find(L"239.96712") == std::wstring::npos);
    int olderMark = g.jumps[0].mark;
    command("good"); assert(g.jumps.back().mark == 1 && g.jumps[0].mark == olderMark);
    command("bad"); assert(g.jumps.back().mark == 2);
    command("clear"); assert(g.jumps.back().mark == 0);
    // Scrolling remains available for the selected older report in both themes.
    assert(g.analysisLineCount >= 3 && g.analysisScroll == 0);
    POINT wheel{g.analysisBody.left + 2, g.analysisBody.top + 2}; ClientToScreen(g.hwnd, &wheel);
    SendMessageW(g.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), MAKELPARAM(wheel.x, wheel.y));
    assert(g.analysisScroll == std::min(3, std::max(0, g.analysisLineCount - 7)));
    click(g.analysisCopy); assert(g.analysisCopyStatus == "Скопировано");
    click(g.analysisLatest); assert(g.selectedAttempt == 0 && analysis_index() == 2);
    click(g.analysisCopy); assert(g.analysisCopyStatus == "Отчёт ещё не готов");
    g.parser.curT -= std::chrono::seconds(4); on_log_timer();
    assert(!g.analyses[2].empty());
    std::string last;
    for (const auto& line : g.analyses[2]) last += line;
    assert(last.find("Недостаточно данных") != std::string::npos);
    assert(last.find("До ") == std::string::npos && last.find("Проверь") == std::string::npos);
    g.locked = true; relayout();
    POINT pt{g.analysisButton.left + 2, g.analysisButton.top + 2}; ClientToScreen(g.hwnd, &pt);
    assert(SendMessageW(g.hwnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y)) == HTTRANSPARENT);
    new_session(); assert(g.analyses.empty() && g.selectedAttempt == 0);
    DestroyWindow(g.hwnd);
    DeleteFileW(g.iniPath.c_str()); DeleteFileW(g.sessionFile.c_str()); DeleteFileW(g.tail.path.c_str());
    puts("PASS Modern/CS16 collapse/render; partial AND complete table wait for block end; NEW finalizes prior; pinned selected report on incoming NEW; Unicode clipboard matches selected attempt; no conclusions for incomplete blocks; scrolling; cached once; CSV unchanged; F6/F7/F8 latest; locked hit test; session reset");
}
