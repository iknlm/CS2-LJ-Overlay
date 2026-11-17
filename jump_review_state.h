// Session annotations stay outside Parser/Tailer/CSV.
static void ensure_attempt_ids() {
    std::map<unsigned long long, bool> seen;
    for (auto& j : g.jumps) {
        if (!j.attemptId || seen[j.attemptId]) j.attemptId = g.nextAttemptId++;
        g.nextAttemptId = std::max(g.nextAttemptId, j.attemptId + 1);
        seen[j.attemptId] = true;
    }
}
static size_t attempt_index(unsigned long long id) {
    for (size_t i = 0; i < g.jumps.size(); ++i) if (g.jumps[i].attemptId == id) return i;
    return g.jumps.size();
}
static size_t analysis_index() {
    ensure_attempt_ids();
    return g.selectedAttempt ? attempt_index(g.selectedAttempt) : g.jumps.empty() ? 0 : g.jumps.size() - 1;
}
static const Jump* viewed_jump() {
    size_t i = analysis_index();
    return i < g.jumps.size() ? &g.jumps[i] : nullptr;
}
static void select_attempt(size_t index) {
    ensure_attempt_ids();
    if (index >= g.jumps.size()) return;
    auto id = g.jumps[index].attemptId;
    if (g.comparePick == 1) g.compareA = id;
    if (g.comparePick == 2) g.compareB = id;
    g.comparePick = 0; g.compareScroll = 0;
    g.selectedAttempt = id; g.menuSelected = (int)index;
    g.analysisCopyStatus.clear(); g.analysisScroll = 0;
}
static void follow_latest() {
    g.selectedAttempt = 0;
    g.historyPage = 0; g.historyEnd = 0;
    g.analysisCopyStatus.clear(); g.analysisScroll = 0;
}
static bool hud_interactive() { return g.settingsOpen || !g.locked; }
static const std::vector<std::string>& comparison_report(size_t ia, size_t ib) {
    const auto& a = g.jumps[ia]; const auto& b = g.jumps[ib];
    if (g.pairCacheA != a.attemptId || g.pairCacheB != b.attemptId ||
        g.pairDirectionA != a.direction || g.pairDirectionB != b.direction || g.pairReport.empty()) {
        g.pairReport = compare_jumps(a, b);
        g.pairCacheA = a.attemptId; g.pairCacheB = b.attemptId;
        g.pairDirectionA = a.direction; g.pairDirectionB = b.direction;
    }
    return g.pairReport;
}
static bool review_hit(POINT pt) {
    if (!hud_interactive()) return false;
    for (const RECT* r : {&g.analysisButton, &g.analysisCopy, &g.analysisLatest, &g.analysisBody,
                          &g.latestButton, &g.pairButton, &g.pairBody, &g.pairAButton, &g.pairBButton,
                          &g.directionButton, &g.historyBody})
        if (PtInRect(r, pt)) return true;
    return false;
}
