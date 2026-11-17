// Instrument actual GDI TextOutW while preserving the real WinAPI paint path.
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
struct DrawnText { std::wstring text; int x, y; HFONT font; COLORREF color; };
static std::vector<DrawnText> drawn;
static BOOL WINAPI ReviewTextOut(HDC dc, int x, int y, LPCWSTR text, int count) {
    drawn.push_back({std::wstring(text, count), x, y, (HFONT)GetCurrentObject(dc, OBJ_FONT), GetTextColor(dc)});
    return ::TextOutW(dc, x, y, text, count);
}
#define TextOutW ReviewTextOut
#undef NOMINMAX
#include "lj_overlay.cpp"
#undef TextOutW
#include <cassert>

static void click(RECT r) {
    assert(r.right > r.left && r.bottom > r.top);
    SendMessageW(g.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(r.left + 2, r.top + 2));
}
static bool has(const wchar_t* text, HFONT font = nullptr) {
    for (const auto& d : drawn) if (d.text.find(text) != std::wstring::npos && (!font || d.font == font)) return true;
    if (!font) {
        std::wstring connected;
        for (const auto& d : drawn) connected += d.text + L" ";
        return connected.find(text) != std::wstring::npos;
    }
    return false;
}
static RECT row_for(unsigned long long id) {
    for (const auto& hit : g.historyHits) if (hit.second == id) return hit.first;
    assert(false); return RECT{};
}
static unsigned long long timelinePixels = 0;
static unsigned long long snapshot(const char* path = nullptr) {
    drawn.clear();
    RECT r{}; GetClientRect(g.hwnd, &r);
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, r.right, r.bottom); ReleaseDC(nullptr, screen);
    HGDIOBJ old = SelectObject(dc, bitmap);
    assert(PrintWindow(g.hwnd, dc, PW_CLIENTONLY));
    SelectObject(dc, old);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = r.right; bi.bmiHeader.biHeight = r.bottom;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    std::vector<unsigned char> pixels((size_t)r.right * r.bottom * 4);
    assert(GetDIBits(dc, bitmap, 0, r.bottom, pixels.data(), &bi, DIB_RGB_COLORS));
    unsigned long long hash = 1469598103934665603ull;
    for (auto b : pixels) { hash ^= b; hash *= 1099511628211ull; }
    int graphTop = -1, graphBottom = -1;
    for (const auto& d : drawn) {
        if (d.text == L"КЛАВИШИ И МЫШЬ") graphTop = d.y;
        if (d.text == L"ИСТОРИЯ") graphBottom = d.y;
    }
    timelinePixels = 1469598103934665603ull;
    assert(graphTop >= 0 && graphBottom > graphTop);
    for (int y = graphTop; y < graphBottom; ++y) for (int x = 0; x < r.right * 4; ++x) {
        timelinePixels ^= pixels[(size_t)(r.bottom - 1 - y) * r.right * 4 + x]; timelinePixels *= 1099511628211ull;
    }
    if (path) {
        BITMAPFILEHEADER fh{}; fh.bfType = 0x4d42;
        fh.bfOffBits = sizeof fh + sizeof(BITMAPINFOHEADER); fh.bfSize = fh.bfOffBits + (DWORD)pixels.size();
        FILE* f = fopen(path, "wb"); assert(f);
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
        fwrite(pixels.data(), pixels.size(), 1, f); fclose(f);
        printf("Saved %s: %dx%d, pixel hash %llu\n", path, r.right, r.bottom, hash);
    }
    DeleteObject(bitmap); DeleteDC(dc);
    assert(!drawn.empty()); return hash;
}
static const char* godlike =
    "JS jumped 240.42000 units with a Long Jump\n"
    "VNL | 3 Strafes | 88.88% AvgSync | 249.99 Pre | 301.23 Max | 0.55 JumpEdge\n"
    "3.33% AvgBadAngles | 0.44% AvgOverlap | 0.22% AvgDeadAir | 70.07% AvgGainEff\n"
    "#. Sync Gain Loss Airtime BadAngles Overlap DeadAir GainEff Width\n"
    "1. 90 12 -0.1 33.33 0 0 0 60 16\n2. 85 10 -0.1 33.33 0 0 0 60 16\n3. 0 0 -2.5 33.34 0 0 0 0 16\n"
    "LEFT KEY | LLLLLLLL$$$$LLLL\nRIGHT KEY | RRRRRRRR\nLEFT MOUSE | MMMMMMMM\nRIGHT MOUSE | mmmmmmmm\n";
static const char* meh =
    "JS jumped 217.50000 units with a Long Jump\n"
    "VNL | 2 Strafes | 44.44% AvgSync | 248.12 Pre | 280.56 Max | 4.44 JumpEdge\n"
    "33.33% AvgBadAngles | 14.44% AvgOverlap | 10.22% AvgDeadAir | 20.07% AvgGainEff\n"
    "#. Sync Gain Loss Airtime BadAngles Overlap DeadAir GainEff Width\n"
    "1. 40 4 -1 50 30 20 10 10 20\n2. 45 5 -1 50 30 10 10 20 20\n"
    "LEFT KEY | LL\nRIGHT KEY | RR\nLEFT MOUSE | MM\nRIGHT MOUSE | mm\n";
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    g.inst = GetModuleHandleW(nullptr); g.measureDC = CreateCompatibleDC(nullptr); g.scale = 1; g.dpi = 96;
    wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd); g.exeDir = cwd;
    g.iniPath = g.exeDir + L"\\review_gui_test.ini"; g.sessionFile = g.exeDir + L"\\review_gui_test.csv";
    g.jumpsDir = g.exeDir + L"\\review_test_sessions";
    g.tail.path = g.exeDir + L"\\review_gui_feed.log"; g.tail.pos = 0;
    g.classicMenu = false; apply_theme(); rebuild_fonts();
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = g.inst; wc.lpszClassName = L"LJOverlayWnd";
    assert(RegisterClassW(&wc));
    g.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Review GUI test", WS_POPUP,
        20, 20, 800, 600, nullptr, nullptr, g.inst, nullptr); assert(g.hwnd);
    SetLayeredWindowAttributes(g.hwnd, 0, 255, LWA_ALPHA);
    std::string data = std::string(godlike) + meh;
    assert(write_file(g.tail.path, data.data(), data.size())); on_log_timer();
    g.parser.curT -= std::chrono::seconds(4); on_log_timer();
    assert(g.jumps.size() == 2 && !g.analyses[0].empty() && !g.analyses[1].empty());
    unsigned long long oldId = g.jumps[0].attemptId, mehId = g.jumps[1].attemptId;
    assert(oldId && mehId > oldId && !g.analysisOpen && !g.pairOpen);
    ShowWindow(g.hwnd, SW_SHOWNOACTIVATE); UpdateWindow(g.hwnd);
    auto latestPixels = snapshot("review_modern_latest.bmp");
    auto latestTimeline = timelinePixels;
    assert(has(L"217.50", g.fBig) && has(L"Meh", g.fType));
    assert(has(L"44.4%", g.fTile) && has(L"248.12", g.fTile) && has(L"280.56", g.fTile));
    assert(has(L"40.0%", g.fTbl) && has(L"4.00", g.fTbl));
    click(row_for(oldId));
    auto chosenPixels = snapshot("review_modern_godlike.bmp");
    assert(chosenPixels != latestPixels);
    assert(timelinePixels != latestTimeline);
    assert(g.selectedAttempt == oldId && viewed_jump()->attemptId == oldId);
    assert(has(L"240.42", g.fBig) && has(L"Godlike", g.fType) && !has(L"217.50", g.fBig));
    assert(has(L"88.9%", g.fTile) && has(L"249.99", g.fTile) && has(L"301.23", g.fTile));
    assert(has(L"90.0%", g.fTbl) && has(L"12.00", g.fTbl));
    assert(viewed_jump()->keys[0].second == "LLLLLLLL$$$$LLLL");
    click(g.analysisButton); snapshot();
    assert(has(L"Последний стрейф #3") && has(L"коррекция перед приземлением"));
    click(g.directionButton); assert(viewed_jump()->direction == DIR_STRAIGHT);
    click(g.directionButton); assert(viewed_jump()->direction == DIR_RIGHT); snapshot("review_modern_diagonal.bmp");
    assert(has(L"Диагональ вправо") && has(L"выбранной диагонали"));
    click(g.directionButton); assert(viewed_jump()->direction == DIR_LEFT);
    g.classicMenu = true; apply_theme(); rebuild_fonts(); relayout(); snapshot("review_cs16_diagonal.bmp");
    assert(has(L"Диагональ влево") && clr::accent == HEX(0xe3c86a));
    click(g.pairButton); assert(g.pairOpen && !g.analysisOpen); // Only one detailed panel at a time.
    click(g.pairAButton); click(row_for(oldId));
    assert(g.compareA == oldId && !g.comparePick);
    click(g.pairBButton); click(row_for(mehId)); assert(g.compareB == mehId);
    snapshot("review_cs16_compare.bmp");
    assert(has(L"240.42000u") && has(L"217.50000u"));
    std::wstring allCompared;
    for (int i = 0; i < 15; ++i) {
        snapshot(); for (const auto& d : drawn) allCompared += d.text + L"\n";
        POINT pt{g.pairBody.left + 2, g.pairBody.top + 2}; review_scroll(true, pt);
    }
    for (const wchar_t* term : {L"Distance:", L"Sync:", L"Pre Speed:", L"Max Speed:", L"JumpEdge:", L"Gain скорости", L"Loss скорости", L"при разном количестве"})
        assert(allCompared.find(term) != std::wstring::npos);
    assert(allCompared.find(L"#1:") == std::wstring::npos); // No false alignment of 3 vs 2.
    click(g.pairButton); click(row_for(oldId));
    auto oldCsv = build_csv(g.jumps);
    // Direction/selection/compare are annotations only; no CSV mutation.
    assert(oldCsv.find("Диагональ") == std::string::npos);
    data += "JS jumped 239.96712 units with a Long Jump\nVNL | 2 Strafes | 50% AvgSync | 250 Pre\n";
    assert(write_file(g.tail.path, data.data(), data.size())); on_log_timer(); snapshot();
    assert(g.selectedAttempt == oldId && has(L"240.42", g.fBig));
    assert(g.compareA == oldId && g.compareB == mehId && g.jumps[0].direction == DIR_LEFT);
    int mark = g.jumps[0].mark;
    command("good"); assert(g.jumps.back().mark == 1 && g.jumps[0].mark == mark);
    command("bad"); assert(g.jumps.back().mark == 2 && g.jumps[0].mark == mark);
    command("clear"); assert(g.jumps.back().mark == 0);
    menu_toggle(); menu_select(8); menu_select(3); menu_select(8);
    assert(g.jumps[0].mark == 1 && g.jumps.back().mark == 0);
    menu_toggle(); click(g.latestButton); snapshot("review_cs16_near.bmp");
    assert(g.selectedAttempt == 0 && has(L"239.97", g.fBig) && has(L"До Godlike: 0.03288u"));
    SendMessageW(g.hwnd, WM_KEYDOWN, '7', 0); assert(!g.settingsOpen && g.jumps.back().mark == 0);

    std::string sameCount = godlike;
    sameCount.replace(sameCount.find("240.42000"), 9, "244.80000");
    sameCount.replace(sameCount.find("2. 85 10 -0.1"), std::string("2. 85 10 -0.1").size(), "2. 45 5 -0.2");
    data += sameCount; assert(write_file(g.tail.path, data.data(), data.size())); on_log_timer();
    g.parser.curT -= std::chrono::seconds(4); on_log_timer();
    auto otherId = g.jumps.back().attemptId;
    g.classicMenu = false; apply_theme(); rebuild_fonts(); relayout();
    click(g.pairButton); click(g.pairAButton); click(row_for(oldId));
    click(g.pairBButton); click(row_for(otherId));
    snapshot("review_modern_compare_strafes.bmp");
    allCompared.clear();
    for (int i = 0; i < 20; ++i) {
        snapshot(); for (const auto& d : drawn) allCompared += d.text + L" ";
        POINT at{g.pairBody.left + 2, g.pairBody.top + 2}; review_scroll(true, at);
    }
    assert(allCompared.find(L"#2: Sync 85→45;") != std::wstring::npos);
    assert(allCompared.find(L"Gain 10→5;") != std::wstring::npos);
    snapshot("review_modern_strafe_diff.bmp");
    auto* cached = g.pairReport.data(); snapshot(); assert(g.pairReport.data() == cached);
    click(g.pairButton); click(row_for(oldId));

    command("lock"); assert(g.locked);
    assert(GetWindowLongPtrW(g.hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
    POINT pt{g.historyHits[0].first.left + 2, g.historyHits[0].first.top + 2}; ClientToScreen(g.hwnd, &pt);
    assert(SendMessageW(g.hwnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y)) == HTTRANSPARENT);
    auto before = g.selectedAttempt; click(row_for(oldId)); assert(g.selectedAttempt == before);
    menu_toggle(); assert(g.settingsOpen);
    pt = POINT{g.historyHits[0].first.left + 2, g.historyHits[0].first.top + 2}; ClientToScreen(g.hwnd, &pt);
    assert(SendMessageW(g.hwnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y)) == HTCLIENT);
    click(row_for(oldId)); assert(g.selectedAttempt == oldId);
    menu_toggle(); command("lock"); assert(!g.locked);
    // Multiple history pages, snapshot stays anchored when more jumps arrive.
    for (int i = 0; i < 8; ++i) { data += meh; }
    assert(write_file(g.tail.path, data.data(), data.size())); on_log_timer();
    g.histRows = 3; relayout();
    POINT cursor{}; GetCursorPos(&cursor);
    pt = POINT{g.historyBody.left + 2, g.historyBody.top + 2}; ClientToScreen(g.hwnd, &pt); SetCursorPos(pt.x, pt.y);
    SendMessageW(g.hwnd, WM_KEYDOWN, VK_NEXT, 0); assert(g.historyPage == 1 && g.selectedAttempt == oldId);
    unsigned long long anchored = g.historyHits.front().second;
    data += meh; assert(write_file(g.tail.path, data.data(), data.size())); on_log_timer();
    assert(g.historyHits.front().second == anchored && g.selectedAttempt == oldId);
    SendMessageW(g.hwnd, WM_KEYDOWN, VK_PRIOR, 0); assert(g.historyPage == 0);
    pt = POINT{g.historyBody.left + 2, g.historyBody.top + 2}; ClientToScreen(g.hwnd, &pt);
    SendMessageW(g.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
    assert(g.historyPage == 1);
    SetCursorPos(cursor.x, cursor.y);
    click(g.latestButton); assert(g.historyPage == 0 && g.selectedAttempt == 0);
    unsigned long long lastId = g.jumps.back().attemptId;
    int ownWindows = 0;
    EnumWindows([](HWND h, LPARAM data) -> BOOL {
        DWORD id = 0; GetWindowThreadProcessId(h, &id);
        wchar_t cls[80]; GetClassNameW(h, cls, 80);
        if (id == GetCurrentProcessId() && wcscmp(cls, L"LJOverlayWnd") == 0) ++*(int*)data;
        return TRUE;
    }, (LPARAM)&ownWindows);
    assert(ownWindows == 1);
    new_session(); data = meh; assert(write_file(g.tail.path, data.data(), data.size()));
    g.tail.pos = 0; g.tail.buf.clear(); on_log_timer(); assert(g.jumps[0].attemptId > lastId);
    DestroyWindow(g.hwnd); DeleteFileW(g.iniPath.c_str()); DeleteFileW(g.sessionFile.c_str()); DeleteFileW(g.tail.path.c_str());
    puts("PASS actual GDI/PrintWindow pixels and text/fonts: Godlike after Meh changes main distance/rank/tiles/table/timeline source/analysis; sticky IDs on NEW; latest and Near Rank; right/left directions; pair via history; unequal strafe counts; both themes; F6/F7/F8 latest and separate old mark; locked click-through, INS editing, PgUp/PgDn/wheel; IDs never reused across session clear");
    return 0;
}
