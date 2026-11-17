#include "lj_overlay.cpp"
#include <cassert>
static std::wstring dir;
static void put(const std::wstring& path, const std::string& data) { assert(write_file(path, data.data(), data.size())); }
static void append(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, 0, nullptr);
    assert(f != INVALID_HANDLE_VALUE); DWORD n; assert(WriteFile(f, data.data(), (DWORD)data.size(), &n, nullptr) && n == data.size()); CloseHandle(f);
}
static std::string block(const std::string& time, const std::string& distance = "240.42000") {
    return time + "JS jumped " + distance + " units with a Long Jump\r\n"
        "VNL | 2 Strafes | 88.88% AvgSync | 250 Pre | 301.23 Max | 0.55 JumpEdge\r\n"
        "3.33% AvgBadAngles | 0.44% AvgOverlap | 0.22% AvgDeadAir | 70.07% AvgGainEff\r\n"
        "1.25 Deviation | 300 Airpath | 0.10 AvgLoss | 16.5 AvgWidth | 2 LandingEdge | +1.1 W\r\n"
        "#. Sync Gain Loss Airtime BadAngles Overlap DeadAir GainEff Width\r\n"
        "1. 90 12 -0.1 50 3 0 0 60 16.0°\r\n"
        "2. 85 10 -0.1 50 3 0 0 -nan 16.5°\r\n"
        "LEFT KEY | LLLLLL\r\nRIGHT KEY | RRRRRR\r\nLEFT MOUSE | MMMMMM\r\nRIGHT MOUSE | mmmmmm\r\n";
}
static void drain() { for (int n = 0; n < 200; ++n) { on_log_timer(); if (!g.tail.readingHistory) return; } assert(false); }
static void reset(const std::wstring& path) {
    new_session(); g.tail.select(path); g.autoLog = false; g.sessionFile = dir + L"\\test.csv";
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    wchar_t cwd[32768]; GetCurrentDirectoryW(32768, cwd); g.exeDir = cwd;
    dir = g.exeDir + L"\\console_test_data"; CreateDirectoryW(dir.c_str(), nullptr);
    g.iniPath = dir + L"\\test.ini"; g.jumpsDir = dir;
    std::wstring path = dir + L"\\console.log";
    std::string first = block("11/17 00:00:01 "), second = block("11/17 00:00:02 ", "239.96712");
    std::string history = "\xEF\xBB\xBF" + first + second + first + "LJMARK BAD\nLJMARK HIDE\n";
    put(path, history); reset(path); drain();
    assert(g.jumps.size() == 2 && g.jumps[0].dist == 240.42 && g.jumps[1].dist == 239.96712);
    assert(g.jumps[0].time == "00:00:01" && g.jumps[1].time == "00:00:02");
    assert(g.jumps[0].strafes.size() == 2 && g.jumps[0].keys.size() == 4);
    assert(g.jumps[0].g("Pre") == 250 && g.jumps[0].g("Max") == 301.23 && g.jumps[0].g("JumpEdge") == 0.55);
    for (const char* key : {"AvgSync","AvgBadAngles","AvgOverlap","AvgDeadAir","Deviation","Airpath","AvgGainEff","AvgLoss","AvgWidth","LandingEdge","W"}) assert(std::isfinite(g.jumps[0].g(key)));
    assert(std::isnan(g.jumps[0].strafes[1].g("GainEff")) && !g.hidden && g.jumps[1].mark == 0);
    assert(console_state() == "LIVE");
    g.jumps[0].mark = 1; select_attempt(0); auto id = g.selectedAttempt;
    auto csv = build_csv(g.jumps); auto cursor = g.tail.pos;
    g.tail.pos = 0; g.tail.buf.clear(); g.tail.replayUntil = -1; drain();
    assert(g.tail.pos == cursor && g.jumps.size() == 2 && g.selectedAttempt == id && build_csv(g.jumps) == csv);
    g.tail = Tailer{}; g.tail.path = path; drain(); assert(g.jumps.size() == 2 && build_csv(g.jumps) == csv);
    puts("PASS full initial history, chronology/timestamps, all metrics, -nan, duplicate timestamped blocks, replay/reinitialized Tailer, marks/selection/CSV retained, historical commands ignored");

    append(path, "JS jumped 244."); on_log_timer(); assert(g.jumps.size() == 2 && !g.tail.buf.empty());
    append(path, "80000 units with a Long Jump\nVNL | 2 Strafes | 250 Pre\n#. Sync Gain Loss Width\n1. 80 10 -0.2 16.0\xC2");
    on_log_timer(); assert(g.jumps.size() == 3 && g.jumps.back().strafes.empty());
    append(path, "\xB0\n"); on_log_timer(); assert(g.jumps.back().strafes.size() == 1 && g.jumps.back().strafes[0].g("Width") == 16);
    g.parser.curT -= std::chrono::seconds(10); on_log_timer(); assert(!g.analyses.back().empty());
    append(path, "2. 85 12 -0.1 17.0\n1. 80 10 -0.2 16.0°\nLEFT KEY | LLL\nLEFT KEY | LLL\n");
    on_log_timer(); assert(g.jumps.size() == 3 && g.jumps.back().strafes.size() == 2 && g.jumps.back().keys.size() == 1);
    assert(g.analyses.back().empty() && g.selectedAttempt == id);
    g.parser.curT -= std::chrono::seconds(4); on_log_timer(); assert(!g.analyses.back().empty());
    // Untimestamped equal-looking attempts at different positions are NOT guessed duplicates.
    std::string equal = block(""); append(path, equal + equal); on_log_timer(); assert(g.jumps.size() == 5);
    puts("PASS live partial header, split UTF-8/newline, late block continuation, row/key upserts, analysis invalidation, distinct equal-valued attempts");

    std::string replacement = block("11/17 00:01:01 ", "249.90000");
    put(path, ""); on_log_timer(); assert(g.tail.pos == 0 && g.tail.buf.empty());
    put(path, replacement); on_log_timer(); assert(g.jumps.size() == 6);
    DeleteFileW(path.c_str()); on_log_timer(); assert(console_state() == "LOG ERROR" && g.statusLog.find("-condebug") != std::string::npos);
    assert(g.statusLog.find(U8(path)) != std::string::npos);
    put(path, block("11/17 00:01:02 ", "245.20000")); on_log_timer(); assert(g.jumps.size() == 7);
    // Same inode, truncate then regrow beyond the previous cursor between polls.
    HANDLE rewrite = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, TRUNCATE_EXISTING, 0, nullptr);
    assert(rewrite != INVALID_HANDLE_VALUE);
    replacement = block("11/17 00:01:03 ", "246.20000") + std::string(4096, 'x') + "\n";
    DWORD wrote; assert(WriteFile(rewrite, replacement.data(), (DWORD)replacement.size(), &wrote, nullptr)); CloseHandle(rewrite);
    on_log_timer(); assert(g.tail.streamReset && g.jumps.size() == 8);
    std::wstring next = dir + L"\\next.log"; put(next, block("11/17 00:01:04 ", "247.20000"));
    assert(MoveFileExW(next.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)); on_log_timer(); assert(g.tail.streamReset && g.jumps.size() == 9);
    puts("PASS clear/truncate/deletion/recreation, same-inode truncate/regrow and replacement identity, preserved existing history");

    HANDLE writer = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(writer != INVALID_HANDLE_VALUE);
    replacement = block("11/17 00:01:05 ", "248.20000"); assert(WriteFile(writer, replacement.data(), (DWORD)replacement.size(), &wrote, nullptr));
    FlushFileBuffers(writer); on_log_timer(); assert(g.tail.connected && g.jumps.size() == 10); CloseHandle(writer);
    HANDLE denied = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr); assert(denied != INVALID_HANDLE_VALUE);
    on_log_timer(); assert(console_state() == "LOG ERROR" && g.tail.error == ERROR_SHARING_VIOLATION);
    CloseHandle(denied); on_log_timer(); assert(console_state() == "LIVE" && g.jumps.size() == 10);
    auto total = g.jumps.size(); reset(path); drain(); assert(g.jumps.size() == 2); // Only the current rotated file, once after a cold restart.
    assert(total == 10);
    puts("PASS concurrent writer remains open, sharing error/recovery, cold restart imports current file once");

    reset(path); put(path, "JS | LJ: 241.2 | 6 Strafes (58.5%) | 250.0 / 290.1 Speed | 4.2 JumpEdge | +1.1 W\n"); drain();
    assert(g.jumps.size() == 1 && g.jumps[0].rank() == R_UNKNOWN && g.jumps[0].g("AvgSync") == 58.5);
    assert(g.jumps[0].g("Strafes") == 6 && g.jumps[0].g("Pre") == 250 && g.jumps[0].g("Max") == 290.1);
    assert(g.jumps[0].g("JumpEdge") == 4.2 && g.jumps[0].g("W") == 1.1);
    reset(path); put(path, first + "JS | LJ: 240.4 | 2 Strafes (88.9%) | 250.0 / 301.2 Speed\n"); drain();
    assert(g.jumps.size() == 1 && g.jumps[0].distanceText == "240.42000");
    g.parser.curT -= std::chrono::seconds(4);
    append(path, "JS | LJ: 240.4 | 2 Strafes (88.9%) | 250.0 / 301.2 Speed\n"); on_log_timer();
    assert(g.jumps.size() == 2); // A later compact attempt is not an echo of an old block.
    reset(path); g.parser.player = "JS";
    put(path, "JS | LJ: 241.2 | 6 Strafes (58.5%) | 250.0 / 290.1 Speed\n"); drain();
    assert(g.jumps.empty()); g.parser.player.clear(); // JS is a marker, not a known player name.
    puts("PASS compact JS input, unknown mode stays unknown, compact echo does not duplicate/degrade detailed record");
    reset(path);
    put(path, "JS jumped 239.96712 units with a Long Jump\nVNL | 2 Strafes | 250 Pre\n#. Sync Gain Loss\n1. -nan 10 -0.1\n2. 85 12 -0.2\n"); drain();
    assert(g.jumps.size() == 1 && g.jumps[0].strafes.size() == 2);
    assert(std::isnan(g.jumps[0].strafes[0].g("Sync")) && g.jumps[0].strafes[0].g("Gain") == 10);
    puts("PASS missing first Sync metric preserves available Gain/Loss without inventing a zero");

    std::string large = first;
    for (int n = 0; n < 150000; ++n) large += "unrelated console line padding to exceed the old eight megabyte limit safely\n";
    large += second;
    put(path, large); reset(path);
    on_log_timer(); assert(g.tail.readingHistory && console_state() == "READING HISTORY" && g.tail.pos <= 256 * 1024);
    drain(); assert(g.jumps.size() == 2 && g.tail.pos == (long long)large.size());
    auto read = g.tail.bytesRead; for (int i = 0; i < 10; ++i) on_log_timer(); assert(g.tail.bytesRead == read);
    assert(read == large.size());
    printf("PASS large history %zu bytes, byte zero through EOF in bounded polls, first/last jumps recovered; idle polls do not reread content\n", large.size());
    std::string kinds;
    int number = 0;
    for (const char* type : {"Long Jump","Bunnyhop","Multi Bunnyhop","Weird Jump","Ladder Jump","Jumpbug"})
        for (const char* mode : {"VNL","CKZ","KZT"}) {
            kinds += sfmt("11/17 02:00:%02d JS jumped 300.00000 units with a %s\n%s | 2 Strafes | 250 Pre | 310 Max\n", number++, type, mode);
        }
    put(path, kinds); reset(path); drain(); assert(g.jumps.size() == 18);
    for (const auto& j : g.jumps) assert(j.rank() != R_UNKNOWN);
    append(path, "JS jumped 239.96712 units with a Long Jump\nUNKNOWN | 2 Strafes | 250 Pre\n"); on_log_timer();
    assert(g.jumps.size() == 19 && g.jumps.back().rank() == R_UNKNOWN);
    puts("PASS file import of all six ranked types x VNL/CKZ/KZT; unknown mode is not inherited");
    std::wstring archive = dir + L"\\" + W(g.jumps.front().stamp) + L".csv";
    put(archive, "PREVIOUS SESSION ARCHIVE\n");
    g.sessionFile.clear(); save_csv();
    assert(g.sessionFile != archive);
    std::string preserved; assert(read_file(archive, preserved) && preserved == "PREVIOUS SESSION ARCHIVE\n");
    puts("PASS same-second session import selects an unused CSV name and preserves the existing archive");
    puts("PASS all console reader/import integration tests");
}
