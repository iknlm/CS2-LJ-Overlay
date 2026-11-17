// Decimal arithmetic on the original log text; no binary floating comparison.
static std::string decimal_value(std::string s) {
    s = trim(s);
    if (s.empty() || s.size() > 128) return "";
    bool dot = false; int digits = 0;
    for (char c : s) {
        if (c == '.' && !dot) dot = true;
        else if (c >= '0' && c <= '9') ++digits;
        else return "";
    }
    if (!digits) return "";
    if (!dot) s += ".0";
    if (s.front() == '.') s = "0" + s;
    if (s.back() == '.') s += '0';
    while (s.size() > 1 && s[0] == '0' && s[1] != '.') s.erase(0, 1);
    while (s.back() == '0' && s[s.size()-2] != '.') s.pop_back();
    return s;
}
static void decimal_align(std::string& a, std::string& b) {
    size_t ad = a.size()-a.find('.')-1, bd = b.size()-b.find('.')-1;
    a.append(bd > ad ? bd-ad : 0, '0'); b.append(ad > bd ? ad-bd : 0, '0');
    size_t ai = a.find('.'), bi = b.find('.');
    a.insert(0, bi > ai ? bi-ai : 0, '0'); b.insert(0, ai > bi ? ai-bi : 0, '0');
}
static int decimal_compare(std::string a, std::string b) {
    decimal_align(a,b); return a == b ? 0 : a < b ? -1 : 1;
}
static std::string decimal_subtract(std::string a, std::string b) {
    decimal_align(a,b); int borrow = 0;
    for (size_t i=a.size(); i-- > 0;) if (a[i] != '.') {
        int n = a[i]-b[i]-borrow; borrow = n < 0; a[i] = char('0'+(n+10)%10);
    }
    return decimal_value(a);
}
static std::string jump_decimal(const Jump& j) {
    auto s = decimal_value(j.distanceText);
    return s.empty() && std::isfinite(j.dist) && j.dist >= 0 ? decimal_value(sfmt("%.10f", j.dist)) : s;
}
enum GoalMode { GOAL_NEXT, GOAL_WRECKER, GOAL_CUSTOM, GOAL_WR };
struct JumpPreference {
    bool customSuccess = false;
    int successRank = R_PERFECT;
    std::string minimum = "249.9", target = "249.9", wr; // WR is manual, never official.
    int goalMode = GOAL_NEXT;
};
static const char* GOAL_MODES[] = {"Следующий ранг", "Wrecker", "Своя дистанция", "WR (вручную)"};
static const char* JUMP_MODES[] = {"VNL", "CKZ", "KZT"};
static JumpPreference g_preferences[3][6];
static int g_prefMode = 0, g_prefType = 0;
static int preference_mode(const Jump& j) {
    for (int m=0;m<3;++m) if (upper(trim(j.gs("Mode"))) == JUMP_MODES[m]) return m;
    return -1;
}
static int preference_type(const Jump& j) {
    for (int t=0;t<6;++t) if (j.type == RANK_THRESHOLDS[t].type) return t;
    return -1;
}
static const JumpPreference* preference(const Jump& j) {
    int m=preference_mode(j), t=preference_type(j);
    return m < 0 || t < 0 ? nullptr : &g_preferences[m][t];
}
struct GoalProgress { bool valid=false, reached=false, isNear=false; std::string text; };
static GoalProgress goal_progress(const Jump& j) {
    GoalProgress out;
    auto p=preference(j); auto distance=jump_decimal(j);
    if (!p || distance.empty()) return out;
    int t=preference_type(j), m=preference_mode(j);
    const double* ranks = m == 0 ? RANK_THRESHOLDS[t].vnl : RANK_THRESHOLDS[t].kz;
    std::string target, label="цели";
    if (p->goalMode == GOAL_CUSTOM) target=p->target;
    else if (p->goalMode == GOAL_WR) { target=p->wr; label="WR (вручную)"; }
    else if (p->goalMode == GOAL_WRECKER) { target=sfmt("%.0f",ranks[4]); label="Wrecker"; }
    else {
        for (int r=0;r<5;++r) if (decimal_compare(distance,decimal_value(sfmt("%.0f",ranks[r]))) < 0) {
            target=sfmt("%.0f",ranks[r]); label=rank_name(r+1); break;
        }
    }
    target=decimal_value(target);
    if (target.empty()) {
        if (p->goalMode == GOAL_WR) out.text="WR: задайте дистанцию вручную";
        return out;
    }
    out.valid=true; out.reached=decimal_compare(distance,target)>=0;
    if (out.reached) out.text="Цель достигнута!";
    else {
        auto remaining=decimal_subtract(target,distance);
        out.isNear=decimal_compare(remaining,"0.50")<=0;
        out.text="До " + label + ": " + remaining + "u";
    }
    return out;
}

