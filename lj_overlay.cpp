// LJOverlay — оверлей джампстатов для CS2 (CYBERSHOKE, LJ VNL).
//
// Читает game/csgo/console.log (CS2 пишет его с параметром запуска -condebug).
// Сервер на каждый прыжок печатает в консоль блок:
//   anonimMizantr0p jumped 233.84125 units with a Long Jump
//   VNL | 230 Block | 3 Strafes | 64.29% AvgSync | 250.00 Pre | 274.46 Max | ...
//   13.54 Deviation | ... | 0.41 JumpEdge | 3.00 LandingEdge | +4.55 W
//   #.  Sync  Gain ...            <- таблица по стрейфам
//   LEFT KEY  | ..LLLL$$...       <- график клавиш/мыши
// В процесс игры ничего не внедряется. Win32 + GDI, один exe без зависимостей.
// Настройки и клавиши: lj_overlay.ini рядом с exe.
//
// Сборка (MinGW):  g++ -std=c++20 -O2 -municode -mwindows lj_overlay.cpp -o LJOverlay.exe
//                  -lgdi32 -luser32 -lshell32 -ladvapi32 -lwinmm
// Тест парсера:    g++ -std=c++20 -DLJ_TEST lj_overlay.cpp -o lj_test && ./lj_test console.log

#if defined(_WIN32) && !defined(LJ_TEST)
#define LJ_WIN 1
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <windowsx.h>
#include <tlhelp32.h>
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <regex>
#include <string>
#include <vector>

// ============================ утилиты ============================

static const double NA = NAN;
static const char* DASH = "\xE2\x80\x94";   // —
static const char* DEG = "\xC2\xB0";        // °

static bool ok(double v) { return !std::isnan(v); }

static std::string sfmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string upper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static size_t u8len(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) n++;
    return n;
}

[[maybe_unused]] static std::string padl(const std::string& s, size_t w) {   // выравнивание влево
    size_t n = u8len(s);
    return n >= w ? s : s + std::string(w - n, ' ');
}

[[maybe_unused]] static std::string padr(const std::string& s, size_t w) {   // выравнивание вправо
    size_t n = u8len(s);
    return n >= w ? s : std::string(w - n, ' ') + s;
}

static std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = s.find(d, a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

static std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        size_t j = i;
        while (j < s.size() && !isspace((unsigned char)s[j])) j++;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

static bool starts_with(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// '68.49%' -> 68.49, '37.50°' -> 37.5, '-nan' -> NA
static double pnum(std::string t) {
    t = trim(t);
    if (t.size() >= 2 && t.compare(t.size() - 2, 2, DEG) == 0) t.resize(t.size() - 2);
    if (!t.empty() && t.back() == '%') t.pop_back();
    if (t.empty()) return NA;
    char* end = nullptr;
    double v = strtod(t.c_str(), &end);
    if (end == t.c_str() || *end) return NA;
    return std::isnan(v) ? NA : v;
}

static std::string fnum(double v, const char* f = "%.1f") { return ok(v) ? sfmt(f, v) : DASH; }
static std::string pct(double v) { return ok(v) ? sfmt("%.1f%%", v) : DASH; }
static std::string deg(double v) { return ok(v) ? sfmt("%.1f", v) + DEG : DASH; }

static std::tm now_tm() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// ============================ данные ============================

struct Val {
    bool num = false;
    double d = NA;
    std::string s;
};

struct Strafe {
    int n = 0;
    std::map<std::string, double> v;
    double g(const char* k) const {
        auto it = v.find(k);
        return it == v.end() ? NA : it->second;
    }
};

enum JumpDirection { DIR_UNKNOWN, DIR_STRAIGHT, DIR_RIGHT, DIR_LEFT };
struct Jump {
    unsigned long long attemptId = 0; // Assigned outside Parser; never reused in this process.
    JumpDirection direction = DIR_UNKNOWN; // Manual annotation, not inferred from key traces.
    std::string name, type, typeFull, time, stamp, distanceText;
    double dist = 0;
    int mark = 0;   // Ручное переопределение: 0 авто, 1 GOOD, 2 BAD. Парсер его не меняет.
    std::string playerName() const { return name.empty() ? "Неизвестный игрок" : name; }
    int rank() const;
    int result() const;
    const char* rankName() const;
    std::map<std::string, Val> stats;
    std::vector<std::string> order;
    std::vector<Strafe> strafes;
    std::vector<std::pair<std::string, std::string>> keys;

    double g(const std::string& k) const {
        auto it = stats.find(k);
        return (it != stats.end() && it->second.num) ? it->second.d : NA;
    }
    std::string gs(const std::string& k) const {
        auto it = stats.find(k);
        if (it == stats.end()) return "";
        return it->second.num ? sfmt("%.10g", it->second.d) : it->second.s;
    }
    void set(const std::string& k, const Val& v) {
        if (!stats.count(k)) order.push_back(k);
        stats[k] = v;
    }
};

enum Rank { R_UNKNOWN = -1, R_MEH, R_IMPRESSIVE, R_PERFECT, R_GODLIKE, R_OWNAGE, R_WRECKER };
// Общая политика оценки сессии. Ранги и исходные данные попыток не изменяются.
static int g_successRank = R_PERFECT;
static const char* rank_name(int rank) {
    static const char* names[] = {"Meh", "Impressive", "Perfect", "Godlike", "Ownage", "Wrecker"};
    return rank >= R_MEH && rank <= R_WRECKER ? names[rank] : "—";
}
static int parse_success_rank(const std::string& value) {
    std::string name = lower(trim(value));
    for (int rank = R_IMPRESSIVE; rank <= R_WRECKER; ++rank)
        if (name == lower(rank_name(rank))) return rank;
    return R_PERFECT;
}
struct RankThresholds { const char* type; double vnl[5], kz[5]; };
static const RankThresholds RANK_THRESHOLDS[] = {
    {"LJ",  {230,235,240,245,250}, {260,265,275,285,290}},
    {"BH",  {235,240,245,250,255}, {315,320,330,342,350}},
    {"MBH", {235,240,245,250,255}, {335,340,350,358,365}},
    {"WJ",  {240,245,250,255,260}, {280,285,295,305,315}},
    {"LAJ", {140,145,150,155,160}, {160,165,175,185,195}},
    {"JB",  {240,245,250,255,260}, {280,285,295,305,315}}
};

int Jump::rank() const {
    // Только явно разобранный Mode; никаких догадок по дистанции/типу/предыдущему прыжку.
    const std::string mode = upper(trim(gs("Mode")));
    if (mode != "VNL" && mode != "CKZ" && mode != "KZT") return R_UNKNOWN;
    if (!std::isfinite(dist) || dist < 0) return R_UNKNOWN;
    for (const auto& row : RANK_THRESHOLDS) if (type == row.type) {
        const double* thresholds = mode == "VNL" ? row.vnl : row.kz;
        int r = R_MEH;
        for (int i = 0; i < 5; ++i) if (dist >= thresholds[i]) r = i + 1;
        return r;
    }
    return R_UNKNOWN;
}

#include "jump_goals.h"

int Jump::result() const {
    if (mark == 1 || mark == 2) return mark;
    int r = rank();
    auto policy = preference(*this);
    if (r == R_UNKNOWN || !policy) return 0;
    if (policy->customSuccess) {
        auto distance = jump_decimal(*this);
        return distance.empty() ? 0 : decimal_compare(distance, policy->minimum) >= 0 ? 1 : 2;
    }
    return r >= policy->successRank ? 1 : 2;
}

const char* Jump::rankName() const {
    return rank_name(rank());
}

struct JumpCounts { int total = 0, good = 0, bad = 0; };
static JumpCounts lj_counts(const std::deque<Jump>& jumps) {
    JumpCounts counts;
    for (const auto& j : jumps) if (j.type == "LJ") {
        ++counts.total;
        counts.good += j.result() == 1;
        counts.bad += j.result() == 2;
    }
    return counts;
}

// ============================ парсер ============================

static std::string type_abbr(const std::string& full) {
    static const std::map<std::string, std::string> m = {
        {"long jump", "LJ"}, {"bunnyhop", "BH"}, {"multi bunnyhop", "MBH"}, {"weird jump", "WJ"},
        {"ladder jump", "LAJ"}, {"ladderhop", "LAH"}, {"jumpbug", "JB"}, {"count jump", "CJ"},
        {"drop bunnyhop", "DBH"}, {"fall", "FALL"}};
    std::string k = lower(trim(full));
    auto it = m.find(k);
    if (it != m.end()) return it->second;
    auto w = split_ws(k);
    if (w.size() > 1) {
        std::string r;
        for (auto& x : w) r += (char)toupper((unsigned char)x[0]);
        return r;
    }
    return upper(k.substr(0, 3));
}

// Убираем цветовые коды и префикс времени "11/11 22:34:20 "
static std::string clean(const std::string& raw) {
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw)
        if (c == '\t' || (c >= 0x20 && c != 0x7f)) s += (char)c;
    if (starts_with(s, "\xEF\xBB\xBF")) s.erase(0, 3);
    auto d = [&](size_t i) { return i < s.size() && isdigit((unsigned char)s[i]); };
    if (s.size() >= 14 && d(0) && d(1) && s[2] == '/' && d(3) && d(4) && s[5] == ' ' && d(6) && d(7) &&
        s[8] == ':' && d(9) && d(10) && s[11] == ':' && d(12) && d(13))
        s.erase(0, (s.size() > 14 && s[14] == ' ') ? 15 : 14);
    return trim(s);
}

// 'VNL | 230 Block | 3 Strafes | 64.29% AvgSync | ...'
static bool parse_stat_line(const std::string& s, std::vector<std::pair<std::string, Val>>& out) {
    if (s.find('|') == std::string::npos) return false;
    static const std::regex num_re(
        std::string(R"(^([-+]?\d+(?:\.\d+)?(?:\s*/\s*[-+]?\d+(?:\.\d+)?)?)\s*(?:%|)") + DEG +
        R"()?\s+([A-Za-z]\w*)$)");
    static const std::regex kv_re(R"(^([A-Za-z]\w*)\s*:\s*(\S.*)$)");
    static const std::regex word_re(R"(^[A-Za-z][A-Za-z0-9\-]*$)");
    int n = 0;
    for (auto& seg0 : split(s, '|')) {
        std::string seg = trim(seg0);
        if (seg.empty() || seg.size() > 200) continue;
        std::smatch m;
        if (std::regex_match(seg, m, num_re)) {
            std::string v = m[1].str();
            v.erase(std::remove(v.begin(), v.end(), ' '), v.end());
            Val val;
            if (v.find('/') != std::string::npos) val.s = v;
            else { val.num = true; val.d = strtod(v.c_str(), nullptr); }
            std::string label = m[2].str();
            if (label == "Strafe") label = "Strafes";   // "1 Strafe" в единственном числе
            out.push_back({label, val});
            n++;
        } else if (std::regex_match(seg, m, kv_re)) {
            Val val;
            val.s = trim(m[2].str());
            out.push_back({m[1].str(), val});
            n++;
        } else if (out.empty() && std::regex_match(seg, word_re)) {
            Val val;
            val.s = seg;
            out.push_back({"Mode", val});
        }
    }
    return n >= 2;
}

struct Parser {
    enum Ev { NONE, NEW, UPD, CMD };
    std::deque<Jump>& jumps;
    std::string player;
    Jump* cur = nullptr;
    std::vector<std::string> cols;
    std::chrono::steady_clock::time_point curT{};

    explicit Parser(std::deque<Jump>& j) : jumps(j) {}
    void reset() { cur = nullptr; cols.clear(); }

    Ev feed(const std::string& raw, std::string& cmd) {
        if (raw.size() > 4000) return NONE;
        std::string s = clean(raw);
        if (s.empty()) return NONE;
        std::string up = upper(s);

        if (up.find("LJMARK") != std::string::npos && up.find(" JUMPED ") == std::string::npos) {
            static const std::regex mk(R"(\bLJMARK\s+(GOOD|BAD|CLEAR|LOCK|HIDE|QUIT)\b)");
            std::smatch m;
            if (std::regex_search(up, m, mk)) { cmd = lower(m[1].str()); return CMD; }
        }

        if (up.find(" JUMPED ") != std::string::npos) {
            static const std::regex hr(
                R"(^(.*?)\s*\bjumped\s+(\d+(?:\.\d+)?)\s+units\s+with\s+an?\s+([A-Za-z][A-Za-z \-]*?)(?:\s+\[(VNL|CKZ|KZT)\])?\s*$)",
                std::regex::icase);
            std::smatch m;
            if (std::regex_match(s, m, hr)) {
                reset();
                std::string name = trim(m[1].str());
                if (!player.empty() && lower(name) != lower(player)) return NONE;
                Jump j;
                j.name = name;
                if (m[4].matched) j.set("Mode", Val{false, NA, upper(m[4].str())});
                j.typeFull = trim(m[3].str());
                j.type = type_abbr(j.typeFull);
                j.distanceText = m[2].str(); // Preserve console precision for the local report.
                j.dist = strtod(j.distanceText.c_str(), nullptr);
                std::tm tm = now_tm();
                j.time = sfmt("%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
                j.stamp = sfmt("%04d-%02d-%02d_%02d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                               tm.tm_hour, tm.tm_min, tm.tm_sec);
                jumps.push_back(std::move(j));
                cur = &jumps.back();
                curT = std::chrono::steady_clock::now();
                return NEW;
            }
        }

        if (!cur) return NONE;
        if (std::chrono::steady_clock::now() - curT > std::chrono::seconds(3)) { reset(); return NONE; }

        if (starts_with(up, "LEFT ") || starts_with(up, "RIGHT ")) {
            static const std::regex kr(R"(^(LEFT|RIGHT)\s+(KEY|MOUSE)\s*\|\s?(.*)$)");
            std::smatch m;
            if (std::regex_match(s, m, kr)) {
                std::string g = m[3].str();
                while (!g.empty() && g.back() == ' ') g.pop_back();
                cur->keys.push_back({m[1].str() + " " + m[2].str(), g});
                return UPD;
            }
        }
        if (starts_with(s, "#.")) {
            auto t = split_ws(s);
            cols.assign(t.begin() + 1, t.end());
            return NONE;
        }
        if (!cols.empty() && isdigit((unsigned char)s[0])) {
            size_t i = 0;
            while (i < s.size() && isdigit((unsigned char)s[i])) i++;
            if (i < s.size() && s[i] == '.') {
                Strafe st;
                st.n = atoi(s.substr(0, i).c_str());
                auto toks = split_ws(s.substr(i + 1));
                for (size_t c = 0; c < cols.size(); c++) {
                    if (cols[c].find('(') != std::string::npos) break;
                    st.v[cols[c]] = c < toks.size() ? pnum(toks[c]) : NA;
                }
                if (ok(st.g(cols[0].c_str()))) { cur->strafes.push_back(st); return UPD; }
                return NONE;
            }
        }
        std::vector<std::pair<std::string, Val>> kv;
        if (parse_stat_line(s, kv)) {
            for (auto& p : kv) cur->set(p.first, p.second);
            return UPD;
        }
        return NONE;   // чужая строка внутри блока — пропускаем
    }
};

// ============================ оценки и таблицы ============================

// Цветовая оценка значений: зелёный = хорошо, жёлтый = средне, красный = плохо.
// Пороги можно поменять в ini, секция [thresholds]: ключ=хорошо,средне
#include "console_import.h"

enum Tone { T_NORM, T_GOOD, T_OK, T_BAD, T_DIM, T_PB, T_SUB, T_FAIL,
            T_MEH, T_IMPRESSIVE, T_PERFECT, T_GODLIKE, T_OWNAGE, T_WRECKER };
static Tone rank_tone(const Jump& j) {
    return j.rank() == R_UNKNOWN ? T_DIM : (Tone)(T_MEH + j.rank());
}

struct Thr {
    double good, ok;   // good >= ok — чем больше, тем лучше; good < ok — чем меньше, тем лучше
};

static std::map<std::string, Thr> g_thr = {
    {"sync", {70, 55}},      {"pre", {249.9, 248}},  {"edge", {1.5, 5}},      {"overlap", {5, 12}},
    {"deadair", {3, 10}},    {"badangles", {12, 22}}, {"gaineff", {40, 30}},
};

static Tone tone(const char* k, double v) {
    if (!ok(v)) return T_DIM;
    auto it = g_thr.find(k);
    if (it == g_thr.end()) return T_NORM;
    const Thr& t = it->second;
    if (t.good >= t.ok) return v >= t.good ? T_GOOD : v >= t.ok ? T_OK : T_BAD;
    return v <= t.good ? T_GOOD : v <= t.ok ? T_OK : T_BAD;
}

#include "jump_analysis.h"
#include "jump_compare.h"

struct Cell {
    std::string s;
    Tone t = T_NORM;
    bool bold = false;
};

template <class T>
struct TCol {
    const char* head;
    const char* tmpl;   // самое широкое значение — по нему считается ширина колонки
    bool left;
    std::function<Cell(const T&)> fn;
};

static std::map<std::string, double> g_best;   // лучший результат по типу прыжка за сессию

static const std::vector<TCol<Strafe>>& strafe_cols() {
    static const std::vector<TCol<Strafe>> c = {
        {"#", "0", true, [](const Strafe& s) { return Cell{std::to_string(s.n), T_SUB}; }},
        {"синк", "100.0%", false, [](const Strafe& s) { return Cell{pct(s.g("Sync")), tone("sync", s.g("Sync"))}; }},
        {"гейн", "+00.00", false, [](const Strafe& s) { return Cell{fnum(s.g("Gain"), "%+.2f")}; }},
        {"лосс", "-00.00", false, [](const Strafe& s) { return Cell{fnum(s.g("Loss"), "%.2f"), T_SUB}; }},
        {"время", "100.0%", false, [](const Strafe& s) { return Cell{pct(s.g("Airtime")), T_SUB}; }},
        {"углы", "100.0%", false, [](const Strafe& s) { return Cell{pct(s.g("BadAngles")), tone("badangles", s.g("BadAngles"))}; }},
        {"овлп", "100.0%", false, [](const Strafe& s) { return Cell{pct(s.g("Overlap")), tone("overlap", s.g("Overlap"))}; }},
        {"дэд", "100.0%", false, [](const Strafe& s) { return Cell{pct(s.g("DeadAir")), tone("deadair", s.g("DeadAir"))}; }},
        {"ширина", "00.0°", false, [](const Strafe& s) { return Cell{deg(s.g("Width")), T_SUB}; }},
    };
    return c;
}

static const std::vector<TCol<Jump>>& hist_cols() {
    static const std::vector<TCol<Jump>> c = {
        {"ИГРОК", "Неизвестный…", true, [](const Jump& j) { return Cell{j.playerName(), T_SUB}; }},
        {"", "MBH", true, [](const Jump& j) { return Cell{j.type, T_SUB}; }},
        {"итог", "OK", false, [](const Jump& j) {
             return Cell{j.result() == 1 ? "OK" : j.result() == 2 ? "NO" : "·",
                         j.result() == 1 ? T_GOOD : j.result() == 2 ? T_FAIL : T_DIM, true};
         }},
        {"дист", "000.00", false, [](const Jump& j) {
             return Cell{sfmt("%.2f", j.dist), rank_tone(j), true};
         }},
        {"ранг", "Impressive", true, [](const Jump& j) { return Cell{j.rankName(), rank_tone(j), true}; }},
        {"стр", "00", false, [](const Jump& j) { return Cell{fnum(j.g("Strafes"), "%.0f")}; }},
        {"синк", "100.0%", false, [](const Jump& j) { return Cell{pct(j.g("AvgSync")), tone("sync", j.g("AvgSync"))}; }},
        {"пре", "000.0", false, [](const Jump& j) { return Cell{fnum(j.g("Pre"), "%.1f"), j.type == "LJ" ? tone("pre", j.g("Pre")) : T_NORM}; }},
        {"макс", "000.0", false, [](const Jump& j) { return Cell{fnum(j.g("Max"), "%.1f")}; }},
        {"эдж", "00.00", false, [](const Jump& j) { return Cell{fnum(j.g("JumpEdge"), "%.2f"), tone("edge", j.g("JumpEdge"))}; }},
        {"овлп", "100.0%", false, [](const Jump& j) { return Cell{pct(j.g("AvgOverlap")), tone("overlap", j.g("AvgOverlap"))}; }},
        {"дэд", "100.0%", false, [](const Jump& j) { return Cell{pct(j.g("AvgDeadAir")), tone("deadair", j.g("AvgDeadAir"))}; }},
        {"W", "+0.00", false, [](const Jump& j) { return Cell{fnum(j.g("W"), "%+.2f"), T_SUB}; }},
    };
    return c;
}

struct Group {
    std::string label;
    Tone t;
    std::vector<const Jump*> js;
    double avg(const char* key) const {
        double s = 0;
        int n = 0;
        for (auto* j : js) {
            double v = std::string(key) == "dist" ? j->dist : j->g(key);
            if (ok(v)) { s += v; n++; }
        }
        return n ? s / n : NA;
    }
};

static const std::vector<TCol<Group>>& cmp_cols() {
    static const std::vector<TCol<Group>> c = {
        {"", "неудачные 000", true, [](const Group& g) { return Cell{sfmt("%s %zu", g.label.c_str(), g.js.size()), g.t, true}; }},
        {"дист", "000.00", false, [](const Group& g) { return Cell{fnum(g.avg("dist"), "%.2f"), T_NORM, true}; }},
        {"синк", "100.0%", false, [](const Group& g) { double v = g.avg("AvgSync"); return Cell{pct(v), tone("sync", v)}; }},
        {"пре", "000.0", false, [](const Group& g) { double v = g.avg("Pre"); bool lj = !g.js.empty() && g.js[0]->type == "LJ"; return Cell{fnum(v, "%.1f"), lj ? tone("pre", v) : T_NORM}; }},
        {"макс", "000.0", false, [](const Group& g) { return Cell{fnum(g.avg("Max"), "%.1f")}; }},
        {"эдж", "00.00", false, [](const Group& g) { double v = g.avg("JumpEdge"); return Cell{fnum(v, "%.2f"), tone("edge", v)}; }},
        {"овлп", "100.0%", false, [](const Group& g) { double v = g.avg("AvgOverlap"); return Cell{pct(v), tone("overlap", v)}; }},
        {"дэд", "100.0%", false, [](const Group& g) { double v = g.avg("AvgDeadAir"); return Cell{pct(v), tone("deadair", v)}; }},
        {"углы", "100.0%", false, [](const Group& g) { double v = g.avg("AvgBadAngles"); return Cell{pct(v), tone("badangles", v)}; }},
    };
    return c;
}

static void update_best(const std::deque<Jump>& jumps) {
    g_best.clear();
    for (auto& j : jumps) {
        auto it = g_best.find(j.type);
        if (it == g_best.end() || j.dist > it->second) g_best[j.type] = j.dist;
    }
}

// Краткая строка джампстатов для шапки оверлея.
// Используем только разобранные значения; отсутствующие поля не придумываем.
static std::vector<std::string> jump_summary(const Jump& j) {
    std::vector<std::string> parts;
    parts.push_back(sfmt("JS | %s: %.1f", j.type.c_str(), j.dist));

    double n = j.g("Strafes"), sync = j.g("AvgSync");
    if (ok(n)) {
        std::string s = sfmt("%.0f Strafe%s", n, std::fabs(n - 1.0) < 0.01 ? "" : "s");
        if (ok(sync)) s += sfmt(" (%.1f%%)", sync);
        parts.push_back(s);
    } else if (ok(sync)) {
        parts.push_back(sfmt("%.1f%% AvgSync", sync));
    }

    double pre = j.g("Pre"), maxSpeed = j.g("Max");
    if (ok(pre) && ok(maxSpeed))
        parts.push_back(sfmt("%.1f / %.1f Speed", pre, maxSpeed));
    if (ok(j.g("JumpEdge")))
        parts.push_back(sfmt("%.1f JumpEdge", j.g("JumpEdge")));
    if (ok(j.g("W")))
        parts.push_back(sfmt("%+.1f W", j.g("W")));
    return parts;
}

// ============================ CSV ============================

static const char* STAT_ORDER[] = {"Mode", "Block", "Strafes", "AvgSync", "Pre", "Max", "JumpEdge",
                                   "LandingEdge", "W", "AvgOverlap", "AvgDeadAir", "AvgBadAngles",
                                   "AvgGainEff", "AvgLoss", "AvgWidth", "Deviation", "Airpath", "Offset",
                                   "Crouched", "Height", "JumpDirection"};

static std::string csv_field(std::string s) {
    if (s.find_first_of(";\"\n") != std::string::npos) {
        std::string q = "\"";
        for (char c : s) { if (c == '"') q += '"'; q += c; }
        return q + "\"";
    }
    return s;
}

static std::string csv_num(double v) {
    if (!ok(v)) return "";
    std::string s = sfmt("%.10g", v);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

static std::string build_csv(const std::deque<Jump>& jumps) {
    std::vector<std::string> keys(std::begin(STAT_ORDER), std::end(STAT_ORDER));
    for (auto& j : jumps)
        for (auto& k : j.order)
            if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    std::string out = "\xEF\xBB\xBF";
    std::vector<std::string> head = {"время", "отметка", "ник", "тип", "дистанция"};
    head.insert(head.end(), keys.begin(), keys.end());
    head.push_back("стрейфы");
    head.push_back("ранг");
    head.push_back("ручная_отметка");
    head.push_back("attempt_id");
    for (size_t i = 0; i < head.size(); i++) out += (i ? ";" : "") + csv_field(head[i]);
    out += "\r\n";
    for (auto& j : jumps) {
        std::vector<std::string> row = {j.time, j.result() == 1 ? "удачный" : j.result() == 2 ? "неудачный" : "",
                                        j.playerName(), j.type, csv_num(j.dist)};
        for (auto& k : keys) {
            auto it = j.stats.find(k);
            row.push_back(it == j.stats.end() ? "" : it->second.num ? csv_num(it->second.d) : it->second.s);
        }
        std::string st;
        for (auto& s : j.strafes) {
            if (!st.empty()) st += " | ";
            st += sfmt("%d: синк %s гейн %s лосс %s овлп %s дэд %s", s.n, fnum(s.g("Sync")).c_str(),
                       fnum(s.g("Gain"), "%+.2f").c_str(), fnum(s.g("Loss"), "%.2f").c_str(),
                       fnum(s.g("Overlap")).c_str(), fnum(s.g("DeadAir")).c_str());
        }
        row.push_back(st);
        row.push_back(j.rankName());
        row.push_back(j.mark == 1 ? "GOOD" : j.mark == 2 ? "BAD" : "AUTO");
        row.push_back(std::to_string(j.attemptId));
        for (size_t i = 0; i < row.size(); i++) out += (i ? ";" : "") + csv_field(row[i]);
        out += "\r\n";
    }
    return out;
}


// ============================ тест парсера ============================
#ifdef LJ_TEST
static const char* tone_tag(Tone t) {
    switch (t) {
        case T_GOOD: return "\033[32m";
        case T_OK: return "\033[33m";
        case T_BAD: return "\033[31m";
        case T_PB: return "\033[93m";
        case T_DIM: case T_SUB: return "\033[90m";
        default: return "";
    }
}

template <class T>
static void tprint(const std::vector<TCol<T>>& cols, const std::vector<const T*>& rows, bool color) {
    std::vector<size_t> w;
    for (auto& c : cols) w.push_back(std::max(u8len(c.head), u8len(c.tmpl)));
    std::string h;
    for (size_t i = 0; i < cols.size(); i++) h += (i ? "  " : "") + (cols[i].left ? padl(cols[i].head, w[i]) : padr(cols[i].head, w[i]));
    printf("%s\n", h.c_str());
    for (auto* r : rows) {
        std::string s;
        for (size_t i = 0; i < cols.size(); i++) {
            Cell c = cols[i].fn(*r);
            std::string v = cols[i].left ? padl(c.s, w[i]) : padr(c.s, w[i]);
            s += (i ? "  " : "") + (color ? std::string(tone_tag(c.t)) + v + "\033[0m" : v);
        }
        printf("%s\n", s.c_str());
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: lj_test console.log [--color]\n"); return 1; }
    bool color = argc > 2;
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("no file\n"); return 1; }
    std::string data;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
    fclose(f);
    std::deque<Jump> jumps;
    Parser p(jumps);
    for (auto& line : split(data, '\n')) {
        std::string cmd;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (p.feed(line, cmd) == Parser::CMD) printf("CMD %s\n", cmd.c_str());
    }
    printf("jumps: %zu\n", jumps.size());
    if (jumps.empty()) return 0;
    printf("summary: ");
    auto summary = jump_summary(jumps.back());
    for (size_t i = 0; i < summary.size(); ++i)
        printf("%s%s", i ? " | " : "", summary[i].c_str());
    printf("\n");
    if (jumps.size() >= 3) { jumps[0].mark = 1; jumps[1].mark = 2; jumps[2].mark = 2; }
    update_best(jumps);
    std::vector<const Jump*> hist;
    for (auto it = jumps.rbegin(); it != jumps.rend() && hist.size() < 10; ++it) hist.push_back(&*it);
    printf("\n");
    tprint(hist_cols(), hist, color);
    std::vector<const Strafe*> st;
    for (auto& s : jumps.back().strafes) st.push_back(&s);
    printf("\n");
    tprint(strafe_cols(), st, color);
    Group g{"удачные", T_GOOD, {}}, b{"неудачные", T_BAD, {}};
    for (auto& j : jumps) { if (j.result() == 1) g.js.push_back(&j); if (j.result() == 2) b.js.push_back(&j); }
    printf("\n");
    tprint(cmp_cols(), std::vector<const Group*>{&g, &b}, color);
    for (auto& k : jumps.back().keys) printf("%-12s|%s\n", k.first.c_str(), k.second.c_str());
    std::string csv = build_csv(jumps);
    printf("\ncsv bytes %zu, lines %zu\n", csv.size(), (size_t)std::count(csv.begin(), csv.end(), '\n'));
    return 0;
}
#endif

// ============================ Windows ============================
#ifdef LJ_WIN

static std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

#ifndef CLR_NONE
#define CLR_NONE ((COLORREF)0xFFFFFFFF)
#endif
#define HEX(h) RGB(((h) >> 16) & 0xff, ((h) >> 8) & 0xff, (h) & 0xff)
namespace clr {
COLORREF bg = HEX(0x0b0f14), frame = HEX(0x1f2630), card = HEX(0x141a22), card2 = HEX(0x1b222c),
               hl = HEX(0x24303d), line = HEX(0x2a333e), text = HEX(0xe8edf2), sub = HEX(0x9aa5b1),
               dim = HEX(0x5f6b78), cap = HEX(0x8792a0), accent = HEX(0x6cb6ff), pill = HEX(0x1a2a3e), good = HEX(0x3fb950),
               goodBg = HEX(0x12301d), ok = HEX(0xe3b341), bad = HEX(0xff6b61), badBg = HEX(0x3b1719),
               pb = HEX(0xf5c84c), warn = HEX(0xe3b341), key = HEX(0x58a6ff),
               ov = HEX(0xff6b61), mouse = HEX(0xc297ff), mouse2 = HEX(0x6f5a9a), empty = HEX(0x222a34),
               noneBg = HEX(0x232b35), menuBg = HEX(0x20251d), menuCard = HEX(0x30382b),
                menuHover = HEX(0x46513a), menuStroke = HEX(0x68734f), cyan = HEX(0xe3c86a);
const COLORREF fail = HEX(0xff9b38), failBg = HEX(0x3b2817);
static const COLORREF ranks[] = {HEX(0x9a9a9a), HEX(0x5599ff), HEX(0x3fb950),
                                 HEX(0xff4b4b), HEX(0xf5c84c), HEX(0xbf6fff)};
}

static COLORREF tone_color(Tone t) {
    switch (t) {
        case T_GOOD: return clr::good;
        case T_OK: return clr::ok;
        case T_BAD: return clr::bad;
        case T_DIM: return clr::dim;
        case T_PB: return clr::pb;
        case T_SUB: return clr::sub;
        case T_FAIL: return clr::fail;
        case T_MEH: case T_IMPRESSIVE: case T_PERFECT: case T_GODLIKE: case T_OWNAGE: case T_WRECKER:
            return clr::ranks[t - T_MEH];
        default: return clr::text;
    }
}

static bool read_file(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    out.assign((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    BOOL r = out.empty() ? TRUE : ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    out.resize(got);
    CloseHandle(h);
    return r;
}

static bool write_file(const std::wstring& path, const void* data, size_t n) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL r = WriteFile(h, data, (DWORD)n, &wr, nullptr);
    CloseHandle(h);
    return r && wr == n;
}

static bool is_dir(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// ---------- чтение растущего лога ----------
#include "console_tailer.h"

// ---------- хоткеи ----------
struct Hotkey {
    std::string text;
    int vk = 0;
    std::vector<int> mods;
    bool down = false;
};

static bool parse_hotkey(const std::string& s0, Hotkey& h) {
    h = Hotkey{};
    std::string s = upper(trim(s0));
    h.text = s;
    if (s.empty()) return true;
    auto parts = split(s, '+');
    for (auto& p : parts) p = trim(p);
    std::string key = parts.back();
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        if (parts[i] == "CTRL") h.mods.push_back(VK_CONTROL);
        else if (parts[i] == "SHIFT") h.mods.push_back(VK_SHIFT);
        else if (parts[i] == "ALT") h.mods.push_back(VK_MENU);
        else return false;
    }
    static const std::map<std::string, int> names = {
        {"NUMPLUS", VK_ADD}, {"NUMMINUS", VK_SUBTRACT}, {"NUMMUL", VK_MULTIPLY}, {"NUMDIV", VK_DIVIDE},
        {"INS", VK_INSERT}, {"DEL", VK_DELETE}, {"HOME", VK_HOME}, {"END", VK_END}, {"PGUP", VK_PRIOR},
        {"PGDN", VK_NEXT}, {"MOUSE4", VK_XBUTTON1}, {"MOUSE5", VK_XBUTTON2}, {"PAUSE", VK_PAUSE},
        {"SCROLLLOCK", VK_SCROLL}};
    auto it = names.find(key);
    if (it != names.end()) h.vk = it->second;
    else if (key.size() >= 2 && key[0] == 'F' && atoi(key.c_str() + 1) >= 1 && atoi(key.c_str() + 1) <= 24)
        h.vk = VK_F1 + atoi(key.c_str() + 1) - 1;
    else if (key.size() == 4 && starts_with(key, "NUM") && isdigit((unsigned char)key[3]))
        h.vk = VK_NUMPAD0 + (key[3] - '0');
    else if (key.size() == 1 && isalnum((unsigned char)key[0]))
        h.vk = (unsigned char)key[0];
    else return false;
    return true;
}

// ---------- приложение ----------
static const wchar_t* DEFAULT_INI =
    L"; Настройки LJ Overlay. После изменения перезапусти оверлей (CS перезапускать не нужно).\r\n"
    L"[overlay]\r\n"
    L"; путь к console.log (пусто = найти автоматически)\r\n"
    L"log=\r\n"
    L"; ник: если указать, учитываются только его прыжки\r\n"
    L"player=\r\n"
    L"; масштаб интерфейса: 0 = авто по разрешению экрана, иначе например 1.0 / 1.25 / 1.5\r\n"
    L"scale=0\r\n"
    L"; непрозрачность 80..255\r\n"
    L"opacity=250\r\n"
    L"history_rows=5\r\n"
    L"; x=right: справа, не закрывать KZ MENU; или укажи числовую координату x\r\n"
    L"x=right\r\n"
    L"y=40\r\n"
    L"locked=0\r\n"
    L"show_strafes=1\r\n"
    L"show_keys=1\r\n"
    L"show_history=1\r\n"
    L"show_compare=1\r\n"
    L"show_details=1\r\n"
    L"style=modern\r\n"
    L"; минимальный удачный ранг: impressive, perfect, godlike, ownage, wrecker\r\n"
    L"success_rank=perfect\r\n"
    L"\r\n"
    L"[sound]\r\n"
    L"enabled=0\r\n"
    L"master=80\r\n"
    L"menu=60\r\n"
    L"awards=100\r\n"
    L"\r\n"
    L"[hotkeys]\r\n"
    L"; F1..F24, NUM0..NUM9, NUMPLUS, NUMMINUS, NUMMUL, NUMDIV, INS, DEL, HOME, END, PGUP, PGDN,\r\n"
    L"; MOUSE4, MOUSE5, PAUSE, SCROLLLOCK, буква/цифра. Модификаторы: CTRL+ ALT+ SHIFT+. Пусто = выкл.\r\n"
    L"; ВАЖНО: игра тоже получает эти нажатия. Не ставь то, что занято в CS2 (F10 = выход из игры!).\r\n"
    L"good=F6\r\n"
    L"bad=F7\r\n"
    L"clear=F8\r\n"
    L"lock=F9\r\n"
    L"hide=HOME\r\n"
    L"quit=CTRL+END\r\n"
    L"menu=INS\r\n"
    L"\r\n"
    L"[thresholds]\r\n"
    L"; цвет значений: ключ=хорошо,средне. Лучше хорошего — зелёный, до среднего — жёлтый, хуже — красный.\r\n"
    L"; если первое число больше второго — чем больше, тем лучше; если меньше — чем меньше, тем лучше.\r\n"
    L"sync=70,55\r\n"
    L"pre=249.9,248\r\n"
    L"edge=1.5,5\r\n"
    L"overlap=5,12\r\n"
    L"deadair=3,10\r\n"
    L"badangles=12,22\r\n"
    L"gaineff=40,30\r\n";

enum { T_LOG = 1, T_KEYS = 2, T_TOP = 3, T_UPD = 4 };
enum MenuPage { MENU_MAIN, MENU_SOUND, MENU_TEST_SOUND, MENU_JUMPS, MENU_VIEW, MENU_SESSION, MENU_CONFIRM_CLEAR, MENU_CONFIRM_IMPORT, MENU_LOG, MENU_GOALS };
struct MenuHit { RECT r; int key; int direction = 1; };

struct App {
    HINSTANCE inst{};
    HWND hwnd{};
    bool settingsOpen = false;
    HWND previousForeground{};
    RECT settingsButton{}, analysisButton{}, analysisCopy{}, analysisLatest{}, analysisBody{};
    bool analysisOpen = false;
    int analysisScroll = 0, analysisLineCount = 0;
    unsigned long long selectedAttempt = 0, nextAttemptId = 1;
    unsigned long long compareA = 0, compareB = 0;
    int comparePick = 0, compareScroll = 0, compareLineCount = 0;
    bool pairOpen = false;
    unsigned long long pairCacheA = 0, pairCacheB = 0;
    JumpDirection pairDirectionA = DIR_UNKNOWN, pairDirectionB = DIR_UNKNOWN;
    std::vector<std::string> pairReport;
    size_t historyPage = 0, historyEnd = 0;
    RECT historyBody{}, latestButton{}, pairButton{}, pairBody{}, pairAButton{}, pairBButton{}, directionButton{};
    std::vector<std::pair<RECT, unsigned long long>> historyHits;
    std::vector<std::vector<std::string>> analyses;
    std::string analysisCopyStatus;
    bool classicMenu = true;
    bool soundEnabled = false, awardPending = false;
    int soundMaster = 80, soundMenu = 60, soundAwards = 100;
    int goalEdit = 0;
    std::string goalDraft, goalError;
    MenuPage menuPage = MENU_MAIN;
    int menuHover = -1, menuSelected = 0;
    size_t menuHistoryEnd = 0; // Снимок конца истории: новые прыжки не сдвигают выбор.
    size_t menuHistoryPage = 0;
    std::vector<MenuHit> menuHits;
    std::vector<MenuHit> adjustHits;
    std::wstring exeDir, iniPath, jumpsDir, sessionFile, historyPath;
    std::deque<Jump> jumps;
    Parser parser{jumps};
    Tailer tail;
    ConsoleImport importer;
    bool autoLog = false, logEditing = false;
    ULONGLONG nextLogSearch = 0;
    std::wstring logDraft;
    std::string logEditError, logState;
    bool locked = false, hidden = false;
    bool showStrafes = true, showKeys = true, showHist = true, showCmp = true, showDetails = true;
    int alpha = 250, histRows = 5, dpi = 96, winW = 0, winH = 0;
    double scale = 1.0;
    std::vector<std::pair<std::string, Hotkey>> hk;
    std::string statusLog, statusCsv, statusCfg, statusHistory;
    HFONT fBig{}, fType{}, fPill{}, fSub{}, fTile{}, fTile2{}, fCap{}, fSec{}, fTbl{}, fTblB{}, fTH{}, fLbl{},
        fFoot{}, fFootB{}, fHint{}, fMenuHead{}, fMenuRow{}, fMenuSmall{}, fMenuDigit{};
    HDC measureDC{};

    int S(double v) const { return (int)std::lround(v * scale * dpi / 96.0); }
    std::string hkText(const char* name) const {
        for (auto& p : hk)
            if (p.first == name) return p.second.text;
        return "";
    }
} g;

static std::wstring ini_get(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048];
    GetPrivateProfileStringW(sec, key, def, buf, 2048, g.iniPath.c_str());
    return buf;
}
static int ini_int(const wchar_t* sec, const wchar_t* key, int def) {
    std::wstring v = ini_get(sec, key, L"");
    return v.empty() ? def : _wtoi(v.c_str());
}
static void ini_set(const wchar_t* sec, const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(sec, key, v.c_str(), g.iniPath.c_str());
}

#include "sound_system.h"
#include "jump_review_state.h"

#include "console_source.h"

// Шрифт с проверкой: если нужного нет в системе (Bahnschrift — Win10+), берём Segoe UI
static HFONT mkfont(const wchar_t* face, double pt, int weight) {
    auto make = [&](const wchar_t* f) {
        return CreateFontW(-(int)std::lround(pt * g.scale * g.dpi / 72.0), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, f);
    };
    HFONT h = make(face);
    wchar_t got[LF_FACESIZE] = {};
    HGDIOBJ old = SelectObject(g.measureDC, h);
    GetTextFaceW(g.measureDC, LF_FACESIZE, got);
    SelectObject(g.measureDC, old);
    if (_wcsnicmp(got, face, wcslen(face)) != 0) {
        DeleteObject(h);
        h = make(L"Segoe UI");
    }
    return h;
}

static void rebuild_fonts() {
    SelectObject(g.measureDC, GetStockObject(SYSTEM_FONT));
    for (HFONT f : {g.fBig, g.fType, g.fPill, g.fSub, g.fTile, g.fTile2, g.fCap, g.fSec, g.fTbl, g.fTblB, g.fTH,
                    g.fLbl, g.fFoot, g.fFootB, g.fHint, g.fMenuHead, g.fMenuRow, g.fMenuSmall, g.fMenuDigit})
        if (f) DeleteObject(f);
    g.fBig = mkfont((g.classicMenu ? L"Tahoma" : L"Bahnschrift"), 23, FW_BOLD);
    g.fType = mkfont((g.classicMenu ? L"Tahoma" : L"Bahnschrift"), 10, FW_BOLD);
    g.fPill = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 7.5, FW_BOLD);
    g.fSub = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 8, FW_NORMAL);
    g.fTile = mkfont((g.classicMenu ? L"Tahoma" : L"Bahnschrift"), 12, FW_BOLD);
    g.fTile2 = mkfont((g.classicMenu ? L"Tahoma" : L"Bahnschrift"), 10, FW_BOLD);
    g.fCap = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 7, FW_NORMAL);
    g.fSec = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 7, FW_BOLD);
    g.fTbl = mkfont((g.classicMenu ? L"Tahoma" : L"Bahnschrift"), 9.5, FW_NORMAL);
    g.fTblB = mkfont((g.classicMenu ? L"Tahoma" : L"Bahnschrift"), 9.5, FW_BOLD);
    g.fTH = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 7, FW_NORMAL);
    g.fLbl = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 7, FW_SEMIBOLD);
    g.fFoot = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 8, FW_NORMAL);
    g.fFootB = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 8, FW_BOLD);
    g.fHint = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 7, FW_NORMAL);
    g.fMenuHead = mkfont((g.classicMenu ? L"Tahoma" : L"Bahnschrift"), 17, FW_BOLD);
    g.fMenuRow = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 10, FW_SEMIBOLD);
    g.fMenuSmall = mkfont((g.classicMenu ? L"Tahoma" : L"Segoe UI"), 8, FW_NORMAL);
    g.fMenuDigit = mkfont((g.classicMenu ? L"Tahoma" : L"Bahnschrift"), 11, FW_BOLD);

}

static void apply_theme() {
    const bool c = g.classicMenu;
    clr::bg = c ? HEX(0x20251d) : HEX(0x0b0f14);
    clr::frame = c ? HEX(0x727b59) : HEX(0x1f2630);
    clr::card = c ? HEX(0x292f24) : HEX(0x141a22);
    clr::card2 = c ? HEX(0x343c2c) : HEX(0x1b222c);
    clr::hl = c ? HEX(0x49553a) : HEX(0x24303d);
    clr::line = c ? HEX(0x636e4d) : HEX(0x2a333e);
    clr::text = c ? HEX(0xe8e9d9) : HEX(0xe8edf2);
    clr::sub = c ? HEX(0xb1b69e) : HEX(0x9aa5b1);
    clr::cap = c ? HEX(0xb4ba9c) : HEX(0x8792a0);
    clr::dim = c ? HEX(0x80876f) : HEX(0x5f6b78);
    clr::accent = c ? HEX(0xe3c86a) : HEX(0x6cb6ff);
    clr::pill = c ? HEX(0x3b442f) : HEX(0x1a2a3e);
    clr::noneBg = c ? HEX(0x3b4232) : HEX(0x232b35);
    clr::empty = c ? HEX(0x414936) : HEX(0x222a34);
    clr::cyan = c ? HEX(0xe3c86a) : HEX(0x65d6e7);
    if(g.hwnd) SetWindowTextW(g.hwnd,c ? L"LJ Overlay [CS 1.6]" : L"LJ Overlay [Modern]");
    // Цвета CYBERSHOKE и пороги показателей не зависят от темы.
}

// ---------- рисование ----------
struct Painter {
    HDC dc;
    bool draw;
    int text(int x, int y, HFONT f, COLORREF c, const std::string& s) {
        if (s.empty()) return 0;
        std::wstring w = W(s);
        SelectObject(dc, f);
        SIZE sz{};
        GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), &sz);
        if (draw) {
            SetTextColor(dc, c);
            TextOutW(dc, x, y, w.c_str(), (int)w.size());
        }
        return sz.cx;
    }
    int textR(int xr, int y, HFONT f, COLORREF c, const std::string& s) {
        int w = width(f, s);
        text(xr - w, y, f, c, s);
        return w;
    }
    int width(HFONT f, const std::string& s) {
        if (s.empty()) return 0;
        std::wstring w = W(s);
        SelectObject(dc, f);
        SIZE sz{};
        GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), &sz);
        return sz.cx;
    }
    int lh(HFONT f) {
        SelectObject(dc, f);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        return tm.tmHeight;
    }
    void rect(int x, int y, int w, int h, COLORREF c) {
        if (!draw || w <= 0 || h <= 0) return;
        RECT r{x, y, x + w, y + h};
        HBRUSH b = CreateSolidBrush(c);
        FillRect(dc, &r, b);
        DeleteObject(b);
    }
    void rrect(int x, int y, int w, int h, int rad, COLORREF c) {
        if (!draw || w <= 0 || h <= 0) return;
        if (g.classicMenu) {
            rect(x, y, w, h, c);
            if (w > g.S(24) && h > g.S(12)) {
                rect(x, y, w, 1, clr::line); rect(x, y + h - 1, w, 1, clr::frame);
                rect(x, y, 1, h, clr::line); rect(x + w - 1, y, 1, h, clr::frame);
            }
            return;
        }
        HBRUSH b = CreateSolidBrush(c);
        HGDIOBJ ob = SelectObject(dc, b);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, x, y, x + w + 1, y + h + 1, rad, rad);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(b);
    }
};

#include "jump_review_draw.h"
#include "console_draw.h"

// ---------- таблицы ----------
template <class T>
static std::vector<int> col_widths(Painter& p, const std::vector<TCol<T>>& cols) {
    std::vector<int> w;
    for (size_t i = 0; i < cols.size(); i++) {
        int cw = std::max(p.width(g.fTH, cols[i].head), p.width(g.fTblB, cols[i].tmpl));
        w.push_back(cw + (i ? g.S(9) : 0));
    }
    return w;
}

template <class T>
static int table_width(Painter& p, const std::vector<TCol<T>>& cols) {
    int s = g.S(9);   // отступ под цветную полоску отметки
    for (int w : col_widths(p, cols)) s += w;
    return s;
}

static int row_h(Painter& p) { return p.lh(g.fTbl) + g.S(2); }

static std::string fit_text(Painter& p, HFONT font, std::string text, int width) {
    if (p.width(font,text)<=width) return text;
    while (!text.empty() && p.width(font,text+"…")>width) {
        size_t pos=text.size()-1;
        while(pos && ((unsigned char)text[pos]&0xc0)==0x80) --pos;
        text.resize(pos);
    }
    return text+"…";
}

// rowBg(i) / bar(row): CLR_NONE = без фона / без полоски
template <class T>
static int draw_table(Painter& p, int x, int y, int innerW, const std::vector<TCol<T>>& cols,
                      const std::vector<const T*>& rows, const std::function<COLORREF(size_t)>& rowBg,
                      const std::function<COLORREF(const T&)>& bar) {
    auto w = col_widths(p, cols);
    const int bx = x + g.S(9), rh = row_h(p), gap = g.S(9);
    int cx = bx;
    int hy = y + p.lh(g.fTbl) - p.lh(g.fTH);
    for (size_t i = 0; i < cols.size(); i++) {
        if (cols[i].left) p.text(cx + (i ? gap : 0), hy, g.fTH, clr::cap, cols[i].head);
        else p.textR(cx + w[i], hy, g.fTH, clr::cap, cols[i].head);
        cx += w[i];
    }
    y += p.lh(g.fTbl) + g.S(4);
    for (size_t r = 0; r < rows.size(); r++) {
        COLORREF b = rowBg(r);
        if (b != CLR_NONE) p.rrect(x - g.S(4), y, innerW + g.S(8), rh, g.S(6), b);
        COLORREF bc = bar(*rows[r]);
        if (bc != CLR_NONE) p.rrect(x + g.S(1), y + g.S(4), g.S(4), rh - g.S(8), g.S(3), bc);
        cx = bx;
        int ty = y + (rh - p.lh(g.fTbl)) / 2;
        for (size_t i = 0; i < cols.size(); i++) {
            Cell c = cols[i].fn(*rows[r]);
            HFONT f = c.bold ? g.fTblB : g.fTbl;
            if (cols[i].head == "ИГРОК") c.s=fit_text(p,f,c.s,w[i]-(i ? gap : 0));
            if (cols[i].left) p.text(cx + (i ? gap : 0), ty, f, tone_color(c.t), c.s);
            else p.textR(cx + w[i], ty, f, tone_color(c.t), c.s);
            cx += w[i];
        }
        y += rh;
    }
    return y;
}

// ---------- макет ----------
struct Tile {
    const char* cap;
    const char* tmpl;
    Cell v;
};

static std::vector<Tile> tiles_row1(const Jump* j) {
    auto c = [&](std::function<Cell(const Jump&)> fn) { return j ? fn(*j) : Cell{DASH, T_DIM}; };
    return {
        {"стрейфы", "00", c([](const Jump& x) { return Cell{fnum(x.g("Strafes"), "%.0f")}; })},
        {"синк", "100.0%", c([](const Jump& x) { return Cell{pct(x.g("AvgSync")), tone("sync", x.g("AvgSync"))}; })},
        {"пре", "000.00", c([](const Jump& x) { return Cell{fnum(x.g("Pre"), "%.2f"), x.type == "LJ" ? tone("pre", x.g("Pre")) : T_NORM}; })},
        {"макс", "000.00", c([](const Jump& x) { return Cell{fnum(x.g("Max"), "%.2f")}; })},
        {"эдж", "00.00", c([](const Jump& x) { return Cell{fnum(x.g("JumpEdge"), "%.2f"), tone("edge", x.g("JumpEdge"))}; })},
        {"W", "+0.00", c([](const Jump& x) { return Cell{fnum(x.g("W"), "%+.2f")}; })},
    };
}

static std::vector<Tile> tiles_row2(const Jump* j) {
    auto c = [&](std::function<Cell(const Jump&)> fn) { return j ? fn(*j) : Cell{DASH, T_DIM}; };
    return {
        {"оверлап", "100.0%", c([](const Jump& x) { return Cell{pct(x.g("AvgOverlap")), tone("overlap", x.g("AvgOverlap"))}; })},
        {"дэд-эйр", "100.0%", c([](const Jump& x) { return Cell{pct(x.g("AvgDeadAir")), tone("deadair", x.g("AvgDeadAir"))}; })},
        {"плох. углы", "100.0%", c([](const Jump& x) { return Cell{pct(x.g("AvgBadAngles")), tone("badangles", x.g("AvgBadAngles"))}; })},
        {"гейн-эфф", "100.0%", c([](const Jump& x) { return Cell{pct(x.g("AvgGainEff")), tone("gaineff", x.g("AvgGainEff"))}; })},
        {"лэнд-эдж", "00.00", c([](const Jump& x) { return Cell{fnum(x.g("LandingEdge"), "%.2f")}; })},
        {"ширина", "00.0°", c([](const Jump& x) { return Cell{deg(x.g("AvgWidth"))}; })},
        {"девиация", "-00.00", c([](const Jump& x) { return Cell{fnum(x.g("Deviation"), "%.2f")}; })},
    };
}

static int tiles_min_width(Painter& p, const std::vector<Tile>& ts, HFONT vf) {
    int w = 0;
    for (auto& t : ts) w = std::max(w, std::max(p.width(vf, t.tmpl), p.width(g.fCap, t.cap)));
    return (int)ts.size() * (w + g.S(13)) + ((int)ts.size() - 1) * g.S(4);
}

static std::string hint_line(int n) {
    std::string h;
    auto add = [&](const char* name, const char* what) {
        std::string k = g.hkText(name);
        if (!k.empty()) h += (h.empty() ? "" : "   ") + k + " " + what;
    };
    if (n == 1) { add("good", "удачный"); add("bad", "неудачный"); add("clear", "авто"); }
    else {
        add("lock", "замок");
        add("hide", "скрыть");
        add("quit", "закрыть");
        add("menu", "меню");
        h += (h.empty() ? "" : "   ") + std::string("ПКМ — меню");
        if (g.locked) h += "   [закреплено]";
    }
    return h;
}

static const char* STATS_TMPL = "MBH  прыжков 000   удачных 000   неудачных 000   лучший 000.00   ср. 000.00";
static const char* KEY_LABELS[] = {"A", "D", "мышь \xE2\x86\x90", "мышь \xE2\x86\x92"};   // ← →

static int content_inner_width(Painter& p) {
    int w = 0;
    w = std::max(w, tiles_min_width(p, tiles_row1(nullptr), g.fTile));
    w = std::max(w, tiles_min_width(p, tiles_row2(nullptr), g.fTile2));
    w = std::max(w, table_width(p, strafe_cols()));
    w = std::max(w, table_width(p, hist_cols()));
    w = std::max(w, table_width(p, cmp_cols()));
    w = std::max(w, p.width(g.fFoot, STATS_TMPL));
    w = std::max(w, p.width(g.fType, "MBH") + g.S(30) + p.width(g.fBig, "000.00") + p.width(g.fPill, "НЕУДАЧНЫЙ") + g.S(40));
    return w;
}

static COLORREF graph_color(char c) {
    if (c == 'L' || c == 'R') return clr::key;
    if (c == '$') return clr::ov;
    if (c == 'M') return clr::mouse;
    if (c == 'm') return clr::mouse2;
    return clr::empty;
}

static void finalize_analysis(size_t index) {
    if (g.analyses.size() < g.jumps.size()) g.analyses.resize(g.jumps.size());
    if (index < g.jumps.size() && g.analyses[index].empty())
        g.analyses[index] = analyze_jump(g.jumps[index]);
}
static void copy_analysis() {
    g.analysisCopyStatus = "Отчёт ещё не готов";
    if (!g.jumps.empty()) {
        size_t index = analysis_index();
        if (index < g.analyses.size() && !g.analyses[index].empty()) {
            std::string text;
            for (const auto& line : g.analyses[index]) text += line + "\r\n";
            std::wstring wide = W(text);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
            if (mem) {
                void* dst = GlobalLock(mem);
                if (dst) {
                    memcpy(dst, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
                    GlobalUnlock(mem);
                    if (OpenClipboard(g.hwnd)) {
                        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, mem)) mem = nullptr;
                        CloseClipboard();
                    }
                }
                g.analysisCopyStatus = mem ? "Не удалось скопировать" : "Скопировано";
                if (mem) GlobalFree(mem);
            } else g.analysisCopyStatus = "Не удалось скопировать";
        }
    }
}

static int draw_settings(Painter& p, int x, int y, int width);

static int render(Painter& p, int Wd) {
    g.menuHits.clear();
    g.historyHits.clear();
    g.historyBody = g.pairAButton = g.pairBButton = g.pairBody = g.directionButton = RECT{};
    const int pad = g.S(g.classicMenu ? 4 : 5), cp = g.S(g.classicMenu ? 5 : 7), gapY = g.S(g.classicMenu ? 3 : 4);
    const int x0 = pad + cp, innerW = Wd - 2 * pad - 2 * cp;
    int y = pad;
    const Jump* j = viewed_jump();
    update_best(g.jumps);

    std::vector<const Jump*> same;
    if (j)
        for (auto& x : g.jumps)
            if (x.type == j->type) same.push_back(&x);
    bool pb = j && same.size() > 1 && j->dist >= g_best[j->type];

    // карточка: заголовок + содержимое; высота считается первым «пустым» проходом
    auto card = [&](const char* title, COLORREF bar, const std::function<int(int)>& body) {
        int titleH = title ? p.lh(g.fSec) + g.S(4) : 0;
        bool d = p.draw;
        p.draw = false;
        int end = body(y + cp + titleH);
        p.draw = d;
        int h = end - y + cp;
        p.rrect(pad, y, Wd - 2 * pad, h, g.S(12), clr::card);
        if (bar != CLR_NONE) p.rrect(pad, y + g.S(12), g.S(4), h - g.S(24), g.S(3), bar);
        if (title) {
            int tw = p.text(x0, y + cp, g.fSec, clr::accent, title);
            p.rect(x0 + tw + g.S(10), y + cp + p.lh(g.fSec) / 2, innerW - tw - g.S(10), 1, clr::line);
        }
        body(y + cp + titleH);
        y += h + gapY;
    };

    // ---- новая верхняя панель: счётчики сессии и доступ к меню ----
    const auto counts = lj_counts(g.jumps);
    card(nullptr, CLR_NONE, [&](int yy) {
        p.text(x0, yy, g.fType, clr::cyan, j ? "LJ  /  " + (j->gs("Mode").empty() ? std::string("?") : j->gs("Mode")) + "  •  CYBERSHOKE" : "LJ  /  CYBERSHOKE");
        const std::string label = g.settingsOpen ? "INS  ЗАКРЫТЬ" : "INS  НАСТРОЙКИ";
        int bw = p.width(g.fFootB, label) + g.S(14), bh = p.lh(g.fFootB) + g.S(4);
        g.settingsButton = RECT{x0 + innerW - bw, yy - g.S(2), x0 + innerW, yy - g.S(2) + bh};
        p.rrect(g.settingsButton.left, g.settingsButton.top, bw, bh, g.S(5), clr::pill);
        p.textR(x0 + innerW - g.S(7), yy, g.fFootB, clr::accent, label);
        yy += std::max(p.lh(g.fType), p.lh(g.fFootB)) + g.S(9);
        p.text(x0, yy, g.fFootB, console_state() == "LOG ERROR" ? clr::warn : clr::accent, console_state());
        p.textR(x0 + innerW, yy, g.fFoot, clr::sub, g.tail.readingHistory ? sfmt("%lld / %lld bytes", g.tail.pos, g.tail.replayUntil) : "console.log");
        yy += p.lh(g.fFootB) + g.S(6);
        const int gap = g.S(6), tw = (innerW - gap * 2) / 3;
        const int th = std::max(g.S(55), p.lh(g.fBig) + p.lh(g.fCap) + g.S(12));
        struct Stat { const char* label; int n; COLORREF color; };
        Stat stats[] = {{"ВСЕГО LJ", counts.total, clr::accent}, {"УДАЧНЫЕ", counts.good, clr::good},
                        {"НЕУДАЧНЫЕ", counts.bad, clr::fail}};
        for (int i = 0; i < 3; ++i) {
            int tx = x0 + i * (tw + gap);
            int cw = i == 2 ? innerW - (tx - x0) : tw;
            p.rrect(tx, yy, cw, th, g.S(10), clr::card2);
            p.rrect(tx + g.S(9), yy + g.S(10), g.S(3), th - g.S(20), g.S(2), stats[i].color);
            p.text(tx + g.S(19), yy + g.S(5), g.fBig, stats[i].color, std::to_string(stats[i].n));
            p.text(tx + g.S(19), yy + g.S(5) + p.lh(g.fBig), g.fCap, clr::sub, stats[i].label);
        }
        return yy + th;
    });

    if (g.settingsOpen)
        card("НАСТРОЙКИ HUD", CLR_NONE, [&](int yy) { return draw_settings(p, x0, yy, innerW); });

    // ---- шапка + плитки ----
    COLORREF mfg = clr::sub, mbg = clr::noneBg;
    std::string mt = j ? "НЕ ОТМЕЧЕН" : console_state() == "LOG ERROR" ? "LOG ERROR" : g.tail.readingHistory ? "ЧИТАЮ ИСТОРИЮ" : "ЖДУ ПРЫЖОК";
    if (j && j->result() == 1) { mt = "УДАЧНЫЙ"; mfg = clr::good; mbg = clr::goodBg; }
    if (j && j->result() == 2) { mt = "НЕУДАЧНЫЙ"; mfg = clr::fail; mbg = clr::failBg; }
    if (j && j->mark) mt += " · РУЧНО";
    COLORREF bar = !j ? CLR_NONE : j->result() == 1 ? clr::good : j->result() == 2 ? clr::fail : CLR_NONE;
    COLORREF distC = j ? tone_color(rank_tone(*j)) : clr::dim;

    card(nullptr, bar, [&](int yy) {
        const std::string latestLabel = "ПОСЛЕДНИЙ";
        int latestW = p.width(g.fFootB, latestLabel) + g.S(14);
        int topH = p.lh(g.fFootB) + g.S(5);
        g.latestButton = RECT{x0 + innerW - latestW, yy, x0 + innerW, yy + topH};
        p.textR(x0 + innerW - g.S(5), yy, g.fFootB, clr::accent, latestLabel);
        std::string attemptTitle;
        if (j) {
            auto prefix=sfmt("Попытка #%llu · ",j->attemptId);
            std::string suffix=g.selectedAttempt ? " · выбрана из истории" : " · последняя";
            int nameWidth=innerW-latestW-g.S(5)-p.width(g.fFoot,prefix+suffix);
            attemptTitle=prefix+fit_text(p,g.fFoot,j->playerName(),nameWidth)+suffix;
        } else attemptTitle=console_state()=="LOG ERROR" ? "Источник недоступен" : g.tail.readingHistory ? "Читаю историю console.log" : "Жду прыжок";
        p.text(x0,yy,g.fFoot,clr::sub,fit_text(p,g.fFoot,attemptTitle,innerW-latestW-g.S(5)));
        yy += topH;
        int hBig = p.lh(g.fBig);
        std::string type = j ? j->type : "LJ";
        int tpw = p.width(g.fType, type) + g.S(18), tph = p.lh(g.fType) + g.S(6);
        int ty = yy + (hBig - tph) / 2;
        p.rrect(x0, ty, tpw, tph, g.S(10), clr::pill);
        p.text(x0 + g.S(9), ty + g.S(3), g.fType, clr::accent, type);
        int dw = p.text(x0 + tpw + g.S(12), yy, g.fBig, distC, j ? sfmt("%.2f", j->dist) : DASH);
        if (j) p.text(x0 + tpw + g.S(20) + dw, yy + g.S(9), g.fType, distC, j->rankName());

        int mw = p.width(g.fPill, mt) + g.S(20), mh = p.lh(g.fPill) + g.S(8);
        int mx = x0 + innerW - mw, my = yy + g.S(4);
        p.rrect(mx, my, mw, mh, g.S(12), mbg);
        p.text(mx + g.S(10), my + g.S(4), g.fPill, mfg, mt);
        int ry = my + mh + g.S(5);
        if (j) {
            std::vector<std::string> parts;
            double blk = j->g("Block");
            if (ok(blk)) parts.push_back(sfmt("блок %.0f", blk));
            if (!j->gs("Mode").empty()) parts.push_back(j->gs("Mode"));
            std::string sub;
            for (auto& s : parts) sub += (sub.empty() ? "" : "  ·  ") + s;
            if (!sub.empty()) { p.textR(x0 + innerW, ry, g.fSub, clr::sub, sub); ry += p.lh(g.fSub); }
            if (pb) { p.textR(x0 + innerW, ry, g.fFootB, clr::pb, "лучший за сессию"); ry += p.lh(g.fFootB); }
        }
        yy += std::max(hBig, ry - yy) + g.S(5);

        if (j) {
            std::string nearRank = preference(*j) && preference(*j)->goalMode != GOAL_NEXT ? goal_progress(*j).text : next_rank_text(*j);
            if (!nearRank.empty()) { p.text(x0, yy, g.fFootB, goal_progress(*j).reached ? clr::good : goal_progress(*j).isNear ? clr::warn : clr::sub, nearRank); yy += p.lh(g.fFootB) + g.S(3); }
        }
        // Дополнительная компактная строка: JS | LJ: 231.3 | 2 Strafes (81.5%) | ...
        // Длинная строка переносится по секциям, чтобы не расширять окно.
        if (j) {
            int xx = x0;
            const int lh = std::max(p.lh(g.fFoot), p.lh(g.fFootB)) + g.S(3);
            auto parts = jump_summary(*j);
            for (size_t i = 0; i < parts.size(); ++i) {
                HFONT f = i == 0 ? g.fFootB : g.fFoot;
                std::string part = (xx == x0 ? "" : "  |  ") + parts[i];
                if (xx > x0 && xx + p.width(f, part) > x0 + innerW) {
                    yy += lh;
                    xx = x0;
                    part = parts[i];
                }
                xx += p.text(xx, yy, f, i == 0 ? clr::accent : clr::sub, part);
            }
            yy += lh + g.S(3);
        }

        auto tiles = [&](int ty0, const std::vector<Tile>& ts, HFONT vf) {
            int n = (int)ts.size(), tg = g.S(4);
            int tw = (innerW - (n - 1) * tg) / n;
            int th = g.S(4) + p.lh(vf) + p.lh(g.fCap) + g.S(4);
            for (int i = 0; i < n; i++) {
                int tx = x0 + i * (tw + tg);
                p.rrect(tx, ty0, tw, th, g.S(8), clr::card2);
                p.text(tx + g.S(7), ty0 + g.S(4), vf, tone_color(ts[i].v.t), ts[i].v.s);
                p.text(tx + g.S(7), ty0 + g.S(4) + p.lh(vf), g.fCap, clr::cap, ts[i].cap);
            }
            return ty0 + th;
        };
        yy = tiles(yy, tiles_row1(j), g.fTile) + g.S(4);
        return g.showDetails ? tiles(yy, tiles_row2(j), g.fTile2) : yy - g.S(4);
    });

    // One compact, collapsible report inside the existing HUD.
    card(nullptr, CLR_NONE, [&](int yy) {
        int lh = p.lh(g.fFoot) + g.S(3);
        g.analysisButton = RECT{x0, yy, x0 + innerW, yy + lh};
        p.text(x0, yy, g.fFootB, clr::accent, g.analysisOpen ? "− РАЗБОР ПРЫЖКА" : "+ РАЗБОР ПРЫЖКА");
        yy += lh;
        g.analysisCopy = g.analysisLatest = g.analysisBody = RECT{};
        if (!g.analysisOpen) return yy;
        if (g.jumps.empty()) { p.text(x0, yy, g.fFoot, clr::sub, console_state() == "LOG ERROR" ? "Сначала настройте источник console.log." : "Жду прыжок"); return yy + lh; }
        size_t index = analysis_index();
        p.text(x0, yy, g.fFoot, clr::sub, sfmt("Попытка #%llu · %s", index < g.jumps.size() ? g.jumps[index].attemptId : 0,
               g.selectedAttempt == 0 ? "последняя" : "выбрана из истории"));
        yy += lh;
        if (index < g.jumps.size()) {
            g.directionButton = RECT{x0, yy, x0 + innerW, yy + lh};
            p.text(x0, yy, g.fFootB, clr::accent, std::string(direction_name(g.jumps[index].direction)) + " · сменить вручную");
            yy += lh;
        }
        std::vector<std::string> reportLines;
        auto wrapped = [&](const std::string& line) {
            std::string row;
            for (const auto& word : split_ws(line)) {
                std::string next = row.empty() ? word : row + " " + word;
                if (!row.empty() && p.width(g.fFoot, next) > innerW) {
                    reportLines.push_back(row); row = word;
                } else row = next;
                // A nickname may contain no spaces. Wrap on UTF-8 codepoint
                // boundaries instead of letting a single long word escape the HUD.
                while (p.width(g.fFoot,row)>innerW && !row.empty()) {
                    size_t end=0;
                    for(size_t k=1;k<=row.size();++k) {
                        if(k<row.size() && ((unsigned char)row[k]&0xc0)==0x80) continue;
                        if(p.width(g.fFoot,row.substr(0,k))>innerW) break;
                        end=k;
                    }
                    if(!end) { end=1;while(end<row.size() && ((unsigned char)row[end]&0xc0)==0x80)++end; }
                    reportLines.push_back(row.substr(0,end));row.erase(0,end);
                }
            }
            if(!row.empty()) reportLines.push_back(row);
        };
        if (index < g.analyses.size() && !g.analyses[index].empty())
            for (const auto& line : g.analyses[index]) wrapped(line);
        else wrapped("Получаю статистику… Разбор появится после закрытия блока парсера.");
        g.analysisLineCount = (int)reportLines.size();
        g.analysisScroll = std::clamp(g.analysisScroll, 0, std::max(0, g.analysisLineCount - 7));
        int bodyTop = yy;
        for (int n = g.analysisScroll; n < std::min(g.analysisLineCount, g.analysisScroll + 7); ++n) {
            p.text(x0, yy, g.fFoot, clr::sub, reportLines[n]); yy += lh;
        }
        g.analysisBody = RECT{x0, bodyTop, x0 + innerW, yy};
        if (g.analysisLineCount > 7) {
            p.text(x0, yy, g.fFoot, clr::accent, sfmt("Строки %d–%d / %d · прокрутка колёсиком", g.analysisScroll + 1,
                   std::min(g.analysisLineCount, g.analysisScroll + 7), g.analysisLineCount)); yy += lh;
        }
        int cw = p.width(g.fFootB, "КОПИРОВАТЬ") + g.S(12);
        g.analysisCopy = RECT{x0, yy, x0 + cw, yy + lh};
        p.text(x0, yy, g.fFootB, clr::accent, "КОПИРОВАТЬ");
        g.analysisLatest = RECT{x0 + cw, yy, x0 + innerW, yy + lh};
        p.text(x0 + cw, yy, g.fFootB, clr::accent, "ПОСЛЕДНИЙ");
        yy += lh;
        if (!g.analysisCopyStatus.empty()) { p.text(x0, yy, g.fFoot, clr::sub, g.analysisCopyStatus); yy += lh; }
        return yy;
    });

    card(nullptr, CLR_NONE, [&](int yy) {
        int lh = p.lh(g.fFootB) + g.S(4);
        g.pairButton = RECT{x0, yy, x0 + innerW, yy + lh};
        p.text(x0, yy, g.fFootB, clr::accent, g.pairOpen ? "− СРАВНИТЬ" : "+ СРАВНИТЬ");
        yy += lh;
        if (!g.pairOpen) return yy;
        int half = innerW / 2;
        g.pairAButton = RECT{x0, yy, x0 + half, yy + lh};
        g.pairBButton = RECT{x0 + half, yy, x0 + innerW, yy + lh};
        p.text(x0, yy, g.fFootB, clr::accent, g.compareA ? sfmt("A: #%llu · выбрать", g.compareA) : "A: выбрать попытку");
        p.text(x0 + half, yy, g.fFootB, clr::accent, g.compareB ? sfmt("B: #%llu · выбрать", g.compareB) : "B: выбрать попытку");
        yy += lh;
        size_t ia = attempt_index(g.compareA), ib = attempt_index(g.compareB);
        std::vector<std::string> lines;
        if (g.comparePick) lines = {g.comparePick == 1 ? "Выберите попытку A в истории HUD или INS → История." : "Выберите попытку B в истории HUD или INS → История."};
        else if (ia >= g.jumps.size() || ib >= g.jumps.size()) lines = {"Нажмите A или B, затем строку истории. Сравнение откроется здесь."};
        else if (ia == ib) lines = {"Выбрана одна и та же попытка. Выберите другую для A или B."};
        else if (ia >= g.analyses.size() || ib >= g.analyses.size() || g.analyses[ia].empty() || g.analyses[ib].empty())
            lines = {"Жду закрытия блоков статистики обеих попыток; окончательного сравнения пока нет."};
        else lines = comparison_report(ia, ib);
        return draw_review_lines(p, x0, yy, innerW, lines, g.compareScroll, g.compareLineCount, g.pairBody);
    });

    // ---- стрейфы ----
    if (g.showStrafes)
        card("СТРЕЙФЫ", CLR_NONE, [&](int yy) {
            if (!j || j->strafes.empty()) {
                p.text(x0, yy, g.fTbl, clr::dim, "нет данных");
                return yy + p.lh(g.fTbl);
            }
            std::vector<const Strafe*> rows;
            for (auto& s : j->strafes) rows.push_back(&s);
            return draw_table<Strafe>(
                p, x0, yy, innerW, strafe_cols(), rows,
                [](size_t i) { return i % 2 ? clr::card2 : CLR_NONE; }, [](const Strafe&) { return CLR_NONE; });
        });

    // ---- график клавиш: цветные блоки ----
    if (g.showKeys)
        card("КЛАВИШИ И МЫШЬ", CLR_NONE, [&](int yy) {
            if (!j || j->keys.empty()) {
                p.text(x0, yy, g.fTbl, clr::dim, "нет данных");
                return yy + p.lh(g.fTbl);
            }
            int lw = 0;
            for (auto* l : KEY_LABELS) lw = std::max(lw, p.width(g.fLbl, l));
            lw += g.S(12);
            size_t len = 1;
            for (auto& k : j->keys) len = std::max(len, k.second.size());
            int cw = std::clamp((innerW - lw) / (int)len, g.S(3), g.S(9));
            int cg = cw >= g.S(5) ? std::max(1, g.S(1)) : 0;
            int ch = g.S(7), rh = ch + g.S(3);
            for (size_t r = 0; r < j->keys.size() && r < 4; r++) {
                if (r == 2) yy += g.S(3);   // отделяем мышь от клавиш
                const auto& gr = j->keys[r].second;
                p.text(x0, yy + (ch - p.lh(g.fLbl)) / 2, g.fLbl, clr::sub, KEY_LABELS[r]);
                for (size_t k = 0; k < gr.size(); k++) {
                    if (gr[k] == ' ') continue;   // граница между стрейфами
                    int cx = x0 + lw + (int)k * cw;
                    bool weak = gr[k] == 'm';
                    p.rect(cx, yy + (weak ? ch / 4 : 0), cw - cg, weak ? ch / 2 : ch, graph_color(gr[k]));
                }
                yy += rh;
            }
            return yy - g.S(3);
        });

    // ---- история ----
    if (g.showHist)
        card("ИСТОРИЯ", CLR_NONE, [&](int yy) {
            int startY = yy;
            std::vector<const Jump*> rows;
            size_t end = g.historyPage ? std::min(g.historyEnd, g.jumps.size()) : g.jumps.size();
            if (g.historyPage && end) g.historyPage = std::min(g.historyPage, (end - 1) / (size_t)std::max(1, g.histRows));
            size_t skip = g.historyPage * (size_t)g.histRows;
            for (size_t offset = skip; offset < end && (int)rows.size() < g.histRows; ++offset)
                rows.push_back(&g.jumps[end - 1 - offset]);
            p.text(x0, yy, g.fFoot, clr::sub, sfmt("Страница %zu · колесо / PgUp/PgDn%s", g.historyPage + 1,
                   g.comparePick ? " · выберите попытку для сравнения" : ""));
            yy += p.lh(g.fFoot) + g.S(3);
            int rowTop = yy + p.lh(g.fTbl) + g.S(4);
            int result = draw_table<Jump>(
                p, x0, yy, innerW, hist_cols(), rows,
                [&](size_t i) { return rows[i] == j ? clr::hl : i % 2 ? clr::card2 : CLR_NONE; },
                [](const Jump& x) { return x.result() == 1 ? clr::good : x.result() == 2 ? clr::fail : CLR_NONE; });
            // Card bodies have a measuring pass; record each row only once.
            for (size_t i = 0; i < rows.size(); ++i) {
                RECT hit{x0, rowTop + (int)i * row_h(p), x0 + innerW, rowTop + ((int)i + 1) * row_h(p)};
                if (std::none_of(g.historyHits.begin(), g.historyHits.end(), [&](const auto& h) { return h.second == rows[i]->attemptId; }))
                    g.historyHits.push_back({hit, rows[i]->attemptId});
            }
            g.historyBody = RECT{x0, startY, x0 + innerW, result};
            return result;
        });

    // ---- сравнение (только когда есть отметки) ----
    Group gd{"удачные", T_GOOD, {}}, bd{"неудачные", T_FAIL, {}};
    for (auto* x : same) {
        if (x->result() == 1) gd.js.push_back(x);
        if (x->result() == 2) bd.js.push_back(x);
    }
    if (g.showCmp && (!gd.js.empty() || !bd.js.empty()))
        card("УДАЧНЫЕ / НЕУДАЧНЫЕ — СРЕДНЕЕ", CLR_NONE, [&](int yy) {
            std::vector<const Group*> rows{&gd, &bd};
            return draw_table<Group>(
                p, x0, yy, innerW, cmp_cols(), rows, [](size_t i) { return i % 2 ? clr::card2 : CLR_NONE; },
                [](const Group& gr) { return gr.t == T_GOOD ? clr::good : clr::fail; });
        });

    // ---- низ ----
    y += g.S(1);
    {
        int x = pad + g.S(5);
        auto seg = [&](HFONT f, COLORREF c, const std::string& s) { x += p.text(x, y, f, c, s) + g.S(14); };
        if (j) {
            int good = 0, bad = 0;
            double sum = 0;
            for (auto* q : same) { good += q->result() == 1; bad += q->result() == 2; sum += q->dist; }
            seg(g.fFootB, clr::text, sfmt("%s  прыжков %zu", j->type.c_str(), same.size()));
            seg(g.fFoot, clr::good, sfmt("удачных %d", good));
            seg(g.fFoot, clr::fail, sfmt("неудачных %d", bad));
            seg(g.fFoot, clr::pb, sfmt("лучший %.2f", g_best[j->type]));
            seg(g.fFoot, clr::sub, sfmt("ср. %.2f", sum / same.size()));
        } else seg(g.fFoot, clr::sub, console_state() == "LOG ERROR" ? "Нет данных из console.log" : g.tail.readingHistory ? "Читаю историю console.log" : "прыжков пока нет — прыгай");
        y += p.lh(g.fFoot) + g.S(3);
    }
    for (int n : {1, 2}) {
        if (j && !g.locked) break;   // подсказки только пока нет прыжков
        std::string h = hint_line(n);
        if (!h.empty()) { p.text(pad + g.S(5), y, g.fHint, clr::dim, h); y += p.lh(g.fHint); }
    }
    for (auto* st : {&g.statusCfg, &g.statusLog, &g.statusCsv, &g.statusHistory})
        if (!st->empty())
            for (auto& l : split(*st, '\n')) {
                if (st == &g.statusLog) y = draw_console_text(p, pad + g.S(6), y, Wd - 2 * pad - g.S(12), l, clr::warn);
                else { p.text(pad + g.S(6), y, g.fFoot, clr::warn, l); y += p.lh(g.fFoot); }
            }

    return y + pad;
}

static void relayout() {
    if (!g.hwnd) return;
    Painter p{g.measureDC, false};
    int Wd = content_inner_width(p) + 2 * g.S(5) + 2 * g.S(7);
    int H = render(p, Wd);
    if (Wd != g.winW || H != g.winH) {
        g.winW = Wd;
        g.winH = H;
        SetWindowPos(g.hwnd, nullptr, 0, 0, Wd, H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

// ---------- действия ----------
#include "jump_history_store.h"

static void save_history() {
    g.statusHistory=history_store::save() ? "" : "Не удалось сохранить историю попыток; проверьте доступ к папке EXE.";
}
static void save_csv() {
    save_history();
    if (g.jumps.empty()) return;
    if (g.sessionFile.empty()) {
        CreateDirectoryW(g.jumpsDir.c_str(), nullptr);
        std::wstring stem = g.jumpsDir + L"\\" + W(g.jumps.front().stamp);
        g.sessionFile = stem + L".csv";
        for (unsigned suffix = 2; GetFileAttributesW(g.sessionFile.c_str()) != INVALID_FILE_ATTRIBUTES; ++suffix)
            g.sessionFile = stem + L"_" + std::to_wstring(suffix) + L".csv";
    }
    std::string data = build_csv(g.jumps);
    std::wstring tmp = g.sessionFile + L".tmp";
    bool okw = write_file(tmp, data.data(), data.size()) &&
               MoveFileExW(tmp.c_str(), g.sessionFile.c_str(), MOVEFILE_REPLACE_EXISTING);
    g.statusCsv = okw ? "" : "CSV открыт в другой программе — сохраню при следующем прыжке/отметке";
}

static void new_session() {
    g.awardPending = false;
    g.jumps.clear();
    g.analyses.clear(); follow_latest();
    g.compareA = g.compareB = 0; g.comparePick = 0; g.pairOpen = false;
    g.pairReport.clear(); g.pairCacheA = g.pairCacheB = 0;
    g.historyPage = g.historyEnd = 0;
    g.menuSelected = 0;
    g.menuHistoryEnd = 0;
    g.menuHistoryPage = 0;
    g.parser.reset();
    g.importer.clear(g.parser);
    g.sessionFile.clear();
    save_history();
    relayout();
}

static void toggle_lock() {
    g.locked = !g.locked;
    LONG_PTR ex = GetWindowLongPtrW(g.hwnd, GWL_EXSTYLE);
    ex = g.locked ? (ex | WS_EX_TRANSPARENT) : (ex & ~(LONG_PTR)WS_EX_TRANSPARENT);
    SetWindowLongPtrW(g.hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(g.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ini_set(L"overlay", L"locked", g.locked ? L"1" : L"0");
    relayout();
}

static void toggle_hide() {
    g.hidden = !g.hidden;
    ShowWindow(g.hwnd, g.hidden ? SW_HIDE : SW_SHOWNOACTIVATE);
}

static void menu_toggle();
static void update_hud_input();
static void rebuild_fonts();
static void apply_theme();

static bool load_menu_style() {
    std::wstring legacy = ini_get(L"overlay", L"menu_style", L"modern");
    std::string value = lower(trim(U8(ini_get(L"overlay", L"style", legacy.c_str()))));
    return value == "cs16" || value == "classic" || value == "cs 1.6";
}

static void set_success_rank(int rank) {
    if (rank < R_IMPRESSIVE || rank > R_WRECKER || rank == g_successRank) return;
    g_successRank = rank;
    for (auto& mode:g_preferences) for(auto& p:mode) p.successRank=rank;
    ini_set(L"overlay", L"success_rank", W(lower(rank_name(rank))));
    // result() вычисляется при обращении: все счётчики и группы средних
    // обновятся при отрисовке. Перезаписываем только CSV текущей сессии.
    save_csv();
    relayout();
}

static std::wstring preference_section(int mode, int type) {
    return W(std::string("jump_") + JUMP_MODES[mode] + "_" + RANK_THRESHOLDS[type].type);
}
static void save_preference(bool successChanged) {
    auto section=preference_section(g_prefMode,g_prefType);
    auto& p=g_preferences[g_prefMode][g_prefType];
    ini_set(section.c_str(),L"success_mode",p.customSuccess ? L"distance" : L"rank");
    ini_set(section.c_str(),L"success_rank",W(lower(rank_name(p.successRank))));
    ini_set(section.c_str(),L"minimum",W(p.minimum));
    ini_set(section.c_str(),L"goal_mode",std::to_wstring(p.goalMode));
    ini_set(section.c_str(),L"target",W(p.target));
    ini_set(section.c_str(),L"wr_manual",W(p.wr));
    // Cached reports include the current target. Rebuild completed reports only.
    for (size_t i=0;i<g.analyses.size() && i<g.jumps.size();++i)
        if (!g.analyses[i].empty()) g.analyses[i]=analyze_jump(g.jumps[i]);
    if (successChanged) save_csv();
}
static void load_preferences() {
    for (int m=0;m<3;++m) for (int t=0;t<6;++t) {
        auto section=preference_section(m,t); auto& p=g_preferences[m][t];
        p=JumpPreference{};
        p.successRank=parse_success_rank(U8(ini_get(section.c_str(),L"success_rank",W(lower(rank_name(g_successRank))).c_str())));
        p.customSuccess=lower(U8(ini_get(section.c_str(),L"success_mode",L"rank")))=="distance";
        p.goalMode=std::clamp(ini_int(section.c_str(),L"goal_mode",0),0,3);
        auto number=[&](const wchar_t* key,const std::string& fallback) {
            auto v=decimal_value(U8(ini_get(section.c_str(),key,W(fallback).c_str())));
            return v.empty() || decimal_compare(v,"0.0")<=0 ? fallback : v;
        };
        p.minimum=number(L"minimum","249.9"); p.target=number(L"target","249.9"); p.wr=number(L"wr_manual","");
    }
}
static bool apply_goal_edit() {
    auto value=decimal_value(g.goalDraft);
    if (value.empty() || decimal_compare(value,"0.0")<=0) {
        g.goalError="Введите положительную десятичную дистанцию (например 249.9)."; return false;
    }
    auto& p=g_preferences[g_prefMode][g_prefType];
    bool success=g.goalEdit==7;
    if (success) p.minimum=value;
    else if (g.goalEdit==8) p.target=value;
    else if (g.goalEdit==9) p.wr=value;
    save_preference(success); g.goalEdit=0; g.goalError.clear(); return true;
}

static void load_visual_preferences() {
    g_successRank = parse_success_rank(U8(ini_get(L"overlay", L"success_rank", L"perfect")));
    load_preferences();
    g.alpha = std::clamp(ini_int(L"overlay", L"opacity", 250), 80, 255);
    g.histRows = std::clamp(ini_int(L"overlay", L"history_rows", 5), 1, 30);
    g.locked = ini_int(L"overlay", L"locked", 0) != 0;
    g.showStrafes = ini_int(L"overlay", L"show_strafes", 1) != 0;
    g.showKeys = ini_int(L"overlay", L"show_keys", 1) != 0;
    g.showHist = ini_int(L"overlay", L"show_history", 1) != 0;
    g.showCmp = ini_int(L"overlay", L"show_compare", 1) != 0;
    g.showDetails = ini_int(L"overlay", L"show_details", 1) != 0;
    g.classicMenu = load_menu_style();
    sound_load_settings();

    double sc = pnum(U8(ini_get(L"overlay", L"scale", L"0")));
    if (!ok(sc) || sc <= 0.0) sc = std::max(1.0, GetSystemMetrics(SM_CYSCREEN) / 1350.0);   // 1440p -> ~1.07
    g.scale = std::clamp(sc, 0.6, 3.0);

    if (ini_get(L"overlay", L"style", L"").empty())
        ini_set(L"overlay", L"style", g.classicMenu ? L"cs16" : L"modern");
}

static void set_menu_style(bool classic) {
    g.classicMenu = classic;
    if (!classic) sound_stop();
    ini_set(L"overlay", L"style", classic ? L"cs16" : L"modern");
    apply_theme();
    rebuild_fonts();
    relayout();
}

static void command(const std::string& a) {
    if (a == "good" || a == "bad" || a == "clear") {
        if (g.jumps.empty()) return;
        int mark = a == "good" ? 1 : a == "bad" ? 2 : 0;
        if (g.jumps.back().mark == mark) return;
        g.jumps.back().mark = mark;
        sound_play(mark == 1 ? "success" : mark == 2 ? "error" : "tick", false);
        save_csv();
        relayout();
    } else if (a == "lock") toggle_lock();
    else if (a == "menu") menu_toggle();
    else if (a == "hide") toggle_hide();
    else if (a == "quit") PostMessageW(g.hwnd, WM_CLOSE, 0, 0);
}

#include "console_pump.h"

static constexpr UINT WM_LJ_KEY = WM_APP + 10, WM_LJ_MENU = WM_APP + 11;
static HHOOK keyboardHook{};
static HANDLE keyboardThread{}, keyboardReady{};
static DWORD keyboardThreadId{};
static std::vector<std::pair<std::string, Hotkey>> hookKeys;
static bool hookDown[256]{};
static HWND hookTarget{};

static bool mouse_key(int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
           vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}

static bool hook_modifier(int vk) {
    if (vk == VK_CONTROL) return hookDown[VK_LCONTROL] || hookDown[VK_RCONTROL] || hookDown[VK_CONTROL];
    if (vk == VK_SHIFT) return hookDown[VK_LSHIFT] || hookDown[VK_RSHIFT] || hookDown[VK_SHIFT];
    if (vk == VK_MENU) return hookDown[VK_LMENU] || hookDown[VK_RMENU] || hookDown[VK_MENU];
    return hookDown[vk];
}

static LRESULT CALLBACK KeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if (key->vkCode < 256 && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN ||
                                  wp == WM_KEYUP || wp == WM_SYSKEYUP)) {
            bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
            bool wasDown = hookDown[key->vkCode];
            hookDown[key->vkCode] = down;
            if (down && !wasDown) {
                for (size_t i = 0; i < hookKeys.size(); ++i) {
                    const Hotkey& h = hookKeys[i].second;
                    if (!h.vk || mouse_key(h.vk) || h.vk != (int)key->vkCode) continue;
                    bool mods = true;
                    for (int m : h.mods) mods = mods && hook_modifier(m);
                    if (mods) PostMessageW(hookTarget, WM_LJ_KEY, (WPARAM)i, 0);
                }
            }
        }
    }
    // Никаких файлов, рисования, ожиданий или блокировки клавиш в callback.
    return CallNextHookEx(nullptr, code, wp, lp);
}

static DWORD WINAPI keyboard_thread(void*) {
    MSG msg{};
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE); // Создать очередь до сигнала готовности.
    for (int i = 0; i < 256; ++i) hookDown[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    // Модификаторы храним по левому/правому VK, чтобы отпускание не оставляло общий VK нажатым.
    hookDown[VK_CONTROL] = hookDown[VK_SHIFT] = hookDown[VK_MENU] = false;
    keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
    SetEvent(keyboardReady);
    if (!keyboardHook) return 1;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(keyboardHook);
    return 0;
}

static bool start_keyboard() {
    hookKeys = g.hk; // Конфигурация неизменна до остановки потока.
    hookTarget = g.hwnd;
    keyboardReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!keyboardReady) return false;
    keyboardThread = CreateThread(nullptr, 0, keyboard_thread, nullptr, 0, &keyboardThreadId);
    if (!keyboardThread) { CloseHandle(keyboardReady); keyboardReady = nullptr; return false; }
    WaitForSingleObject(keyboardReady, INFINITE); // Только старт; callback никогда не ждёт.
    return keyboardHook != nullptr;
}

static void stop_keyboard() {
    if (keyboardThread) {
        PostThreadMessageW(keyboardThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(keyboardThread, INFINITE);
        CloseHandle(keyboardThread);
        keyboardThread = nullptr;
    }
    if (keyboardReady) { CloseHandle(keyboardReady); keyboardReady = nullptr; }
    keyboardHook = nullptr;
}

static void on_keys_timer() {
    for (auto& [name, h] : g.hk) {
        if (!h.vk) continue;
        if (keyboardHook && !mouse_key(h.vk)) continue; // Не выполнять одно нажатие дважды.
        bool d = (GetAsyncKeyState(h.vk) & 0x8000) != 0;
        for (int m : h.mods) d = d && (GetAsyncKeyState(m) & 0x8000) != 0;
        if (d && !h.down) command(name);
        h.down = d;
    }
}

// Автообновление: если рядом появился LJOverlay_update.exe (новая версия), запускаем его —
// он закроет нас, заменит собой LJOverlay.exe и перезапустится.
static std::wstring g_selfExe, g_updExe, g_dir;
static long long g_updLastSize = -1;
static ULONGLONG g_updLaunchedAt = 0;

static void check_update() {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(g_updExe.c_str(), GetFileExInfoStandard, &fa)) { g_updLastSize = -1; return; }
    long long size = ((long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    bool stable = size > 100000 && size == g_updLastSize;   // файл дописан до конца
    g_updLastSize = size;
    if (!stable || GetTickCount64() - g_updLaunchedAt < 15000) return;
    std::string a, b;
    if (!read_file(g_updExe, a) || !read_file(g_selfExe, b)) return;
    if (a == b) { DeleteFileW(g_updExe.c_str()); return; }   // это мы и есть — просто убираем
    g_updLaunchedAt = GetTickCount64();
    save_csv();
    ShellExecuteW(nullptr, L"open", g_updExe.c_str(), nullptr, g_dir.c_str(), SW_SHOWNORMAL);
}

static void save_show() {
    ini_set(L"overlay", L"show_strafes", g.showStrafes ? L"1" : L"0");
    ini_set(L"overlay", L"show_keys", g.showKeys ? L"1" : L"0");
    ini_set(L"overlay", L"show_history", g.showHist ? L"1" : L"0");
    ini_set(L"overlay", L"show_compare", g.showCmp ? L"1" : L"0");
    ini_set(L"overlay", L"show_details", g.showDetails ? L"1" : L"0");
}

// ======================= Встроенные настройки HUD =======================
static int draw_settings(Painter& p, int x, int y, int width) {
    g.menuHits.clear();
    g.adjustHits.clear();
    auto row = [&](int key, const std::string& title, const std::string& value = "") {
        int h = std::max(g.S(22), p.lh(g.fFoot) + g.S(7));
        RECT r{x, y, x + width, y + h};
        g.menuHits.push_back({r, key});
        if (g.menuHover == key) p.rrect(x, y, width, h, g.S(4), clr::hl);
        std::string label = std::to_string(key) + ". " + title;
        bool adjust = (g.menuPage == MENU_MAIN && (key == 2 || key == 3)) || (g.menuPage == MENU_SOUND && key >= 2 && key <= 4) || (g.menuPage == MENU_GOALS && key >= 1 && key <= 6);
        int reserved = value.empty() ? 0 : p.width(g.fFootB, value) + g.S(adjust ? 70 : 12);
        int saved = p.draw ? SaveDC(p.dc) : 0;
        if (p.draw) IntersectClipRect(p.dc, x, y, x + width - reserved, y + h);
        p.text(x + g.S(5), y + g.S(3), g.fFoot, clr::text, label);
        if (p.draw) RestoreDC(p.dc, saved);
        p.textR(x + width - g.S(adjust ? 34 : 5), y + g.S(3), g.fFootB, clr::accent, value);
        if (adjust) for (int dir : {-1, 1}) {
            int bx = dir == 1 ? x + width - g.S(27) : x + width - reserved + g.S(2);
            RECT br{bx, y + g.S(2), bx + g.S(22), y + h - g.S(2)};
            g.adjustHits.push_back({br, key, dir});
            p.rrect(br.left, br.top, br.right-br.left, br.bottom-br.top, g.S(3), clr::pill);
            p.text(bx + g.S(7), y + g.S(3), g.fFootB, clr::accent, dir == 1 ? "+" : "−");
        }
        y += h;
    };
    if (g.menuPage == MENU_MAIN) {
        row(1, "Style", g.classicMenu ? "CS 1.6 classic" : "Modern");
        row(2, "Scale  (← / →)", sfmt("%.2fx", g.scale));
        row(3, "Opacity  (← / →)", sfmt("%d / 255", g.alpha));
        row(4, "Show strafes", g.showStrafes ? "ВКЛ" : "ВЫКЛ");
        row(5, "Show keys", g.showKeys ? "ВКЛ" : "ВЫКЛ");
        row(6, "Show history", g.showHist ? "ВКЛ" : "ВЫКЛ");
        row(7, "Show compare", g.showCmp ? "ВКЛ" : "ВЫКЛ");
        row(8, "Выбрать и отметить старый прыжок", "ИСТОРИЯ");
        row(9, "CSV / сессия / дополнительные настройки", "ДАЛЕЕ");
        row(0, "Закрыть настройки", "Esc / INS");
        row(10, "Звуки CS 1.6", "Enter / мышь");
        row(11, "Успех / персональная цель", "НАСТРОИТЬ");
    } else if (g.menuPage == MENU_GOALS) {
        auto& policy = g_preferences[g_prefMode][g_prefType];
        row(1, "Режим", JUMP_MODES[g_prefMode]);
        row(2, "Тип", RANK_THRESHOLDS[g_prefType].type);
        row(3, "Успешность", policy.customSuccess ? "Своя дистанция" : "По рангу");
        row(4, "Удачный ранг", rank_name(policy.successRank));
        row(5, "Минимум (шаг 0.1u)", policy.minimum + "u");
        row(6, "Цель", GOAL_MODES[policy.goalMode]);
        row(7, "Ввести минимум", g.goalEdit == 7 ? g.goalDraft + "|" : policy.minimum);
        row(8, "Ввести свою цель", g.goalEdit == 8 ? g.goalDraft + "|" : policy.target);
        row(9, "Ввести WR вручную", g.goalEdit == 9 ? g.goalDraft + "|" : policy.wr.empty() ? "не задан" : policy.wr);
        y = draw_console_text(p,x,y,width,g.goalEdit ? "Дистанция: цифры, точка; Enter применить, Esc отмена." : "WR задаётся вручную и не является официальным рекордом.",clr::sub);
        if (!g.goalError.empty()) y=draw_console_text(p,x,y,width,g.goalError,clr::warn);
        row(0,"Назад");
    } else if (g.menuPage == MENU_SOUND) {
        row(1, "Звуки (только CS 1.6)", g.soundEnabled ? "ВКЛ" : "ВЫКЛ");
        row(2, "Общая громкость", sfmt("%d%%", g.soundMaster));
        row(3, "Громкость меню", sfmt("%d%%", g.soundMenu));
        row(4, "Громкость наград", sfmt("%d%%", g.soundAwards));
        row(5, "Тест звуков меню / наград"); row(0, "Назад");
    } else if (g.menuPage == MENU_TEST_SOUND) {
        row(1, "Move / Tick"); row(2, "Select"); row(3, "Deny / Error");
        row(4, "HUD on"); row(5, "HUD off"); row(6, "Perfect / Success");
        row(7, "Godlike"); row(8, "Ownage"); row(9, "Wrecker / Achievement"); row(0, "Назад");
    } else if (g.menuPage == MENU_JUMPS) {
        size_t end = std::min(g.menuHistoryEnd, g.jumps.size());
        p.text(x, y, g.fFoot, clr::sub, sfmt("История %zu / %zu · PgUp/PgDn, колесо", g.menuHistoryPage + 1,
                                                           std::max<size_t>(1, (end + 5) / 6)));
        y += p.lh(g.fFoot) + g.S(4);
        for (int i = 0; i < 6; ++i) {
            size_t offset = g.menuHistoryPage * 6 + i;
            if (offset < end) {
                size_t index = end - 1 - offset;
                const Jump& j = g.jumps[index];
                row(i + 1, sfmt("%s · %s %.2f · %s", fit_text(p,g.fFoot,j.playerName(),g.S(105)).c_str(), j.type.c_str(), j.dist, j.rankName()),
                    std::string(index == (size_t)g.menuSelected ? "> " : "") +
                    (j.result() == 1 ? "GOOD" : j.result() == 2 ? "BAD" : "—"));
            } else row(i + 1, "—");
        }
        row(10, "Показать / разобрать выбранный прыжок");
        row(11, "СРАВНИТЬ: выбранный = A, затем выберите B");
        row(7, "Выбранный: вернуть авто"); row(8, "Выбранный: GOOD"); row(9, "Выбранный: BAD"); row(0, "Назад");
    } else if (g.menuPage == MENU_SESSION) {
        row(10, "Источник console.log", "НАСТРОИТЬ");
        row(1, "Сохранить CSV"); row(2, "Открыть папку CSV"); row(3, "Открыть INI");
        row(4, "Перечитать лог…"); row(5, "Новая сессия…");
        row(6, "Подробные плитки", g.showDetails ? "ВКЛ" : "ВЫКЛ");
        row(7, "Количество строк истории", std::to_string(g.histRows));
        row(8, "Закрепить HUD", g.locked ? "ВКЛ" : "ВЫКЛ"); row(9, "Вернуть HUD вправо"); row(0, "Назад");
    } else if (g.menuPage == MENU_LOG) {
        p.text(x, y, g.fFootB, clr::accent, console_state()); y += p.lh(g.fFootB) + g.S(4);
        y = draw_console_text(p, x, y, width, "Источник: " + (g.tail.path.empty() ? std::string("не найден") : U8(g.tail.path)), clr::sub);
        if (g.logEditing) {
            y = draw_console_text(p, x, y, width, "Путь: " + U8(g.logDraft) + "|", clr::text);
            y = draw_console_text(p, x, y, width, "Введите путь. Ctrl+V — вставить, Ctrl+A — очистить, Enter — применить, Esc — отмена.", clr::sub);
        }
        row(1, "Вставить путь из буфера и применить"); row(2, "Ввести / изменить путь внутри HUD"); row(3, "Автопоиск Steam / запущенной CS2");
        y = draw_console_text(p, x, y, width, "Steam → CS2 → Свойства → Параметры запуска: -condebug. Нужен файл game\\csgo\\console.log. Перезапустите CS2 после изменения параметра.", clr::sub);
        if (!g.logEditError.empty()) y = draw_console_text(p, x, y, width, g.logEditError, clr::warn);
        row(0, "Назад");
    } else {
        p.text(x, y, g.fFootB, clr::warn, g.menuPage == MENU_CONFIRM_CLEAR ?
               "Начать новую сессию? Текущие прыжки будут удалены." : "Заменить сессию содержимым console.log?");
        y += p.lh(g.fFootB) + g.S(6);
        row(1, "Подтвердить"); row(0, "Отмена");
    }
    return y;
}

static void update_hud_input() {
    if (!g.hwnd) return;
    POINT pt{}; GetCursorPos(&pt); ScreenToClient(g.hwnd, &pt);
    bool overButton = PtInRect(&g.settingsButton, pt) || review_hit(pt);
    LONG_PTR before = GetWindowLongPtrW(g.hwnd, GWL_EXSTYLE), after = before;
    if (g.settingsOpen) after &= ~(LONG_PTR)(WS_EX_NOACTIVATE | WS_EX_TRANSPARENT);
    else {
        after |= WS_EX_NOACTIVATE;
        if (g.locked && !overButton) after |= WS_EX_TRANSPARENT;
        else after &= ~(LONG_PTR)WS_EX_TRANSPARENT;
    }
    if (before != after) SetWindowLongPtrW(g.hwnd, GWL_EXSTYLE, after);
}

static void menu_toggle() {
    if (!g.settingsOpen) g.previousForeground = GetForegroundWindow();
    g.settingsOpen = !g.settingsOpen;
    g.logEditing = false; g.goalEdit = 0;
    sound_play(g.settingsOpen ? "wpn_hudon" : "wpn_hudoff", true);
    g.menuPage = MENU_MAIN; g.menuHover = 1;
    if (g.settingsOpen && g.hidden) { g.hidden = false; ShowWindow(g.hwnd, SW_SHOWNORMAL); }
    update_hud_input(); relayout();
    if (g.settingsOpen) { SetForegroundWindow(g.hwnd); SetFocus(g.hwnd); }
    else if (g.previousForeground != g.hwnd && IsWindow(g.previousForeground)) SetForegroundWindow(g.previousForeground);
}

static void change_scale(int direction) {
    RECT old{}; GetWindowRect(g.hwnd, &old);
    g.scale = std::clamp(std::round((g.scale + direction * 0.1) * 100.0) / 100.0, 0.6, 3.0);
    ini_set(L"overlay", L"scale", W(sfmt("%.2f", g.scale)));
    rebuild_fonts(); relayout();
    MONITORINFO monitor{}; monitor.cbSize = sizeof monitor;
    GetMonitorInfoW(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = std::max((int)monitor.rcWork.left, (int)old.right - g.winW);
    SetWindowPos(g.hwnd, nullptr, x, old.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ini_set(L"overlay", L"x", std::to_wstring(x));
}

static void menu_select(int key, int direction = 1) {
    if (!g.settingsOpen || key < 0 || key > 11) return;
    if (g.menuPage == MENU_JUMPS && key >= 1 && key <= 6 &&
        g.menuHistoryPage * 6 + (size_t)key - 1 >= std::min(g.menuHistoryEnd, g.jumps.size())) {
        sound_play("wpn_denyselect", true); return;
    }
    if (g.menuPage != MENU_TEST_SOUND && !(key == 0 && g.menuPage == MENU_MAIN)) sound_play("wpn_select", true);
    if (!key) {
        g.logEditing = false; g.goalEdit = 0;
        if (g.menuPage == MENU_MAIN) { menu_toggle(); return; }
        g.menuPage = (g.menuPage == MENU_CONFIRM_CLEAR || g.menuPage == MENU_CONFIRM_IMPORT) ? MENU_SESSION : MENU_MAIN;
        g.menuHover = 1; relayout(); return;
    }
    if (g.menuPage == MENU_MAIN) {
        if (key == 11) { g.menuPage = MENU_GOALS; } else if (key == 10) g.menuPage = MENU_SOUND;
        else if (key == 1) set_menu_style(!g.classicMenu);
        else if (key == 2) change_scale(direction);
        else if (key == 3) {
            g.alpha = std::clamp(g.alpha + direction * 15, 80, 255);
            SetLayeredWindowAttributes(g.hwnd, 0, (BYTE)g.alpha, LWA_ALPHA);
            ini_set(L"overlay", L"opacity", std::to_wstring(g.alpha));
        } else if (key == 4) g.showStrafes = !g.showStrafes;
        else if (key == 5) g.showKeys = !g.showKeys;
        else if (key == 6) g.showHist = !g.showHist;
        else if (key == 7) g.showCmp = !g.showCmp;
        else if (key == 8) {
            g.menuPage = MENU_JUMPS; g.menuHistoryEnd = g.jumps.size(); g.menuHistoryPage = 0;
            g.menuSelected = g.jumps.empty() ? -1 : (int)analysis_index();
        } else if (key == 9) g.menuPage = MENU_SESSION;
        save_show();
    } else if (g.menuPage == MENU_GOALS) {
        g.goalEdit=0; g.goalError.clear();
        auto& p=g_preferences[g_prefMode][g_prefType];
        int step=direction < 0 ? -1 : 1;
        if (key==1) g_prefMode=(g_prefMode+step+3)%3;
        else if (key==2) g_prefType=(g_prefType+step+6)%6;
        else if (key==3) { p.customSuccess=!p.customSuccess; save_preference(true); }
        else if (key==4) { p.successRank=1+(p.successRank-1+step+5)%5; save_preference(true); }
        else if (key==5) {
            // Decimal addition/subtraction by exactly one tenth, independent of float.
            if (step<0) { if (decimal_compare(p.minimum,"0.1")>0) p.minimum=decimal_subtract(p.minimum,"0.1"); }
            else {
                std::string a=p.minimum,b="0.1"; decimal_align(a,b); int carry=0;
                for (size_t i=a.size();i-- >0;) if(a[i]!='.') { int n=a[i]+b[i]-2*'0'+carry; a[i]=char('0'+n%10); carry=n/10; }
                if(carry) a="1"+a; p.minimum=decimal_value(a);
            }
            save_preference(true);
        } else if (key==6) { p.goalMode=(p.goalMode+step+4)%4; save_preference(false); }
        else if (key>=7 && key<=9) {
            g.goalEdit=key; g.goalDraft=key==7 ? p.minimum : key==8 ? p.target : p.wr;
        }
    } else if (g.menuPage == MENU_SOUND) {
        if (key == 1) g.soundEnabled = !g.soundEnabled;
        else if (key >= 2 && key <= 4) {
            int& v = key == 2 ? g.soundMaster : key == 3 ? g.soundMenu : g.soundAwards;
            v = std::clamp(v + direction * 10, 0, 100);
        } else if (key == 5) g.menuPage = MENU_TEST_SOUND;
        sound_save_settings(); sound_stop();
    } else if (g.menuPage == MENU_TEST_SOUND) {
        static const char* names[] = {"", "wpn_moveselect", "wpn_select", "wpn_denyselect", "wpn_hudon", "wpn_hudoff", "success", "Godlike", "Ownage", "achievement"};
        if (key <= 9) sound_play(names[key], key <= 5);
    } else if (g.menuPage == MENU_JUMPS) {
        size_t end = std::min(g.menuHistoryEnd, g.jumps.size());
        size_t offset = g.menuHistoryPage * 6 + (size_t)key - 1;
        if ((key == 10 || key == 11) && g.menuSelected >= 0 && (size_t)g.menuSelected < g.jumps.size()) {
            select_attempt((size_t)g.menuSelected);
            if (key == 10) { g.analysisOpen = true; g.pairOpen = false; g.comparePick = 0; }
            else { g.pairOpen = true; g.analysisOpen = false; g.compareA = g.selectedAttempt; g.compareB = 0; g.comparePick = 2; }
            menu_toggle(); return;
        }
        if (key <= 6) { if (offset < end) { select_attempt(end - 1 - offset); } }
        else if (g.menuSelected >= 0 && (size_t)g.menuSelected < g.jumps.size()) {
            g.jumps[(size_t)g.menuSelected].mark = key == 8 ? 1 : key == 9 ? 2 : 0;
            save_csv();
        }
    } else if (g.menuPage == MENU_SESSION) {
        if (key == 1) save_csv();
        else if (key == 2) { CreateDirectoryW(g.jumpsDir.c_str(), nullptr); ShellExecuteW(nullptr,L"open",g.jumpsDir.c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
        else if (key == 3) ShellExecuteW(nullptr,L"open",L"notepad.exe",g.iniPath.c_str(),nullptr,SW_SHOWNORMAL);
        else if (key == 4) g.menuPage = MENU_CONFIRM_IMPORT;
        else if (key == 5) g.menuPage = MENU_CONFIRM_CLEAR;
        else if (key == 6) { g.showDetails = !g.showDetails; save_show(); }
        else if (key == 7) { g.histRows = g.histRows <= 3 ? 5 : g.histRows <= 5 ? 8 : 3; ini_set(L"overlay",L"history_rows",std::to_wstring(g.histRows)); }
        else if (key == 8) toggle_lock();
        else if (key == 9) {
            SetWindowPos(g.hwnd,nullptr,std::max(0,GetSystemMetrics(SM_CXSCREEN)-g.winW-g.S(24)),g.S(40),0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            ini_set(L"overlay",L"x",L"right");
        }
        else if (key == 10) g.menuPage = MENU_LOG;
    } else if (g.menuPage == MENU_LOG) {
        if (key == 1) set_console_source(clipboard_log_path(), false);
        else if (key == 2) { g.logDraft = g.tail.path; g.logEditing = true; g.logEditError.clear(); }
        else if (key == 3) set_console_source(L"", true);
    } else if (key == 1) {
        bool import = g.menuPage == MENU_CONFIRM_IMPORT;
        save_csv(); new_session();
        if (import) g.tail.select(g.tail.path);
        g.menuPage = MENU_MAIN;
    }
    update_hud_input(); relayout();
}

static void menu_history_scroll(bool older) {
    if (!g.settingsOpen || g.menuPage != MENU_JUMPS) return;
    size_t end = std::min(g.menuHistoryEnd, g.jumps.size());
    if (older && (g.menuHistoryPage + 1) * 6 < end) ++g.menuHistoryPage;
    else if (!older && g.menuHistoryPage) --g.menuHistoryPage;
    relayout();
}

static void show_menu(int, int) { menu_toggle(); }

static void review_scroll(bool older, POINT pt) {
    if (!hud_interactive()) return;
    if (g.analysisOpen && PtInRect(&g.analysisBody, pt))
        g.analysisScroll = std::clamp(g.analysisScroll + (older ? 3 : -3), 0, std::max(0, g.analysisLineCount - 7));
    else if (g.pairOpen && PtInRect(&g.pairBody, pt))
        g.compareScroll = std::clamp(g.compareScroll + (older ? 3 : -3), 0, std::max(0, g.compareLineCount - 6));
    else if (g.settingsOpen && g.menuPage == MENU_JUMPS) { menu_history_scroll(older); return; }
    else if (g.showHist) {
        if (!g.historyPage) g.historyEnd = g.jumps.size();
        size_t end = std::min(g.historyEnd, g.jumps.size());
        if (older && (g.historyPage + 1) * (size_t)g.histRows < end) ++g.historyPage;
        else if (!older && g.historyPage) --g.historyPage;
    }
    relayout();
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_LJ_KEY:
            if ((size_t)wp < g.hk.size()) command(g.hk[(size_t)wp].first);
            return 0;
        case WM_LJ_MENU: menu_toggle(); return 0;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(h, &pt);
            if (PtInRect(&g.settingsButton,pt) || review_hit(pt)) return HTCLIENT;
            if (g.settingsOpen) {
                // Настройки редактируются в HUD: верхняя строка служит ручкой перетаскивания.
                // При открытых настройках она работает и с включённым закреплением F9.
                if (pt.y >= 0 && pt.y <= g.settingsButton.bottom + g.S(4)) return HTCAPTION;
                return HTCLIENT;
            }
            return g.locked ? HTTRANSPARENT : HTCAPTION;
        }
        case WM_MOUSEACTIVATE: return hud_interactive() ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_KEYDOWN: {
            if (g.goalEdit && g.settingsOpen && g.menuPage==MENU_GOALS) {
                if (wp==VK_ESCAPE) { g.goalEdit=0; g.goalError.clear(); }
                else if (wp==VK_RETURN) apply_goal_edit();
                else if ((GetKeyState(VK_CONTROL)&0x8000) && wp=='A') g.goalDraft.clear();
                else if ((GetKeyState(VK_CONTROL)&0x8000) && wp=='V') {
                    if(OpenClipboard(h)) { auto data=GetClipboardData(CF_UNICODETEXT);
                        if(data) { auto str=(const wchar_t*)GlobalLock(data); if(str) { g.goalDraft=U8(str); GlobalUnlock(data); } }
                        CloseClipboard(); }
                }
                relayout(); return 0;
            }
            if (g.logEditing && g.settingsOpen && g.menuPage == MENU_LOG) {
                if (wp == VK_ESCAPE) { g.logEditing = false; g.logEditError.clear(); }
                else if (wp == VK_RETURN) set_console_source(g.logDraft, false);
                else if ((GetKeyState(VK_CONTROL) & 0x8000) && wp == 'V') g.logDraft = clipboard_log_path();
                else if ((GetKeyState(VK_CONTROL) & 0x8000) && wp == 'A') g.logDraft.clear();
                relayout(); return 0;
            }
            if ((wp == VK_NEXT || wp == VK_PRIOR) && hud_interactive()) {
                POINT pt{}; GetCursorPos(&pt); ScreenToClient(h, &pt);
                review_scroll(wp == VK_NEXT, pt); return 0;
            }
            if (!g.settingsOpen) break;
            if (wp == VK_UP || wp == VK_DOWN) {
                int n = (int)g.menuHits.size(), index = -1;
                for (int i=0;i<n;++i) if (g.menuHits[i].key == g.menuHover) index=i;
                if (n) g.menuHover = g.menuHits[index<0?0:(index+(wp==VK_DOWN?1:n-1))%n].key;
                sound_play("wpn_moveselect", true);
                InvalidateRect(h,nullptr,FALSE); return 0;
            }
            if (wp == VK_RETURN) { menu_select(g.menuHover); return 0; }
            if (wp == VK_LEFT || wp == VK_RIGHT) { if(g.menuPage==MENU_GOALS || (g.menuPage==MENU_MAIN && (g.menuHover==2 || g.menuHover==3 || g.menuHover==11)) || (g.menuPage==MENU_SOUND && g.menuHover>=2 && g.menuHover<=4)) menu_select(g.menuHover,wp==VK_RIGHT?1:-1); return 0; }
            if (wp == VK_ESCAPE || wp == VK_BACK) { menu_select(0); return 0; }
            if (wp == VK_NEXT || wp == VK_PRIOR) { menu_history_scroll(wp==VK_NEXT); return 0; }
            if (wp>='0' && wp<='9') { menu_select((int)wp-'0'); return 0; }
            if (wp>=VK_NUMPAD0 && wp<=VK_NUMPAD9) { menu_select((int)wp-VK_NUMPAD0); return 0; }
            break;
        }
        case WM_CHAR:
            if (g.goalEdit && g.settingsOpen && g.menuPage==MENU_GOALS) {
                if(wp==VK_BACK && !g.goalDraft.empty()) g.goalDraft.pop_back();
                else if(((wp>='0' && wp<='9') || wp=='.' || wp==',') && g.goalDraft.size()<128)
                    g.goalDraft += wp==',' ? '.' : (char)wp;
                relayout(); return 0;
            }
            if (g.logEditing && g.settingsOpen && g.menuPage == MENU_LOG) {
                if (wp == VK_BACK && !g.logDraft.empty()) g.logDraft.pop_back();
                else if (wp >= 32 && wp != 127 && g.logDraft.size() < 2047) g.logDraft += (wchar_t)wp;
                relayout(); return 0;
            }
            break;
        case WM_MOUSEMOVE: {
            if (!g.settingsOpen) break;
            POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            for(const auto& item:g.menuHits) if(PtInRect(&item.r,pt)) {if(g.menuHover!=item.key) sound_play("wpn_moveselect",true);g.menuHover=item.key;InvalidateRect(h,nullptr,FALSE);break;}
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if (PtInRect(&g.settingsButton,pt)) {menu_toggle();return 0;}
            if (!hud_interactive()) return 0;
            if (PtInRect(&g.latestButton,pt) || PtInRect(&g.analysisLatest,pt)) { follow_latest(); relayout(); return 0; }
            if (PtInRect(&g.pairButton,pt)) { g.pairOpen = !g.pairOpen; if (g.pairOpen) g.analysisOpen = false; g.comparePick = 0; relayout(); return 0; }
            if (PtInRect(&g.pairAButton,pt) || PtInRect(&g.pairBButton,pt)) {
                g.comparePick = PtInRect(&g.pairAButton,pt) ? 1 : 2; g.compareScroll = 0; relayout(); return 0;
            }
            if (PtInRect(&g.directionButton,pt)) {
                size_t i = analysis_index();
                if (i < g.jumps.size()) {
                    g.jumps[i].direction = (JumpDirection)(((int)g.jumps[i].direction + 1) % 4);
                    if (i < g.analyses.size() && !g.analyses[i].empty()) g.analyses[i] = analyze_jump(g.jumps[i]);
                    g.analysisScroll = 0; g.analysisCopyStatus.clear();
                }
                relayout(); return 0;
            }
            for (const auto& item : g.historyHits) if (PtInRect(&item.first, pt)) {
                auto id = item.second; select_attempt(attempt_index(id)); relayout(); return 0;
            }
            if (PtInRect(&g.analysisButton,pt)) { g.analysisOpen = !g.analysisOpen; if (g.analysisOpen) { g.pairOpen = false; g.comparePick = 0; } relayout(); return 0; }
            if (PtInRect(&g.analysisCopy,pt)) { copy_analysis(); relayout(); return 0; }
            if (g.settingsOpen) for (const auto& item:g.adjustHits) if(PtInRect(&item.r,pt)) {
                int key=item.key, dir=item.direction; menu_select(key,dir); return 0;
            }
            if(g.settingsOpen) for(const auto& item:g.menuHits) if(PtInRect(&item.r,pt)) {int key=item.key;menu_select(key);break;}
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(h, &pt);
            review_scroll(GET_WHEEL_DELTA_WPARAM(wp) < 0, pt);
            return 0;
        }
        case WM_PRINTCLIENT: {
            HDC dc=reinterpret_cast<HDC>(wp);int saved=SaveDC(dc);
            RECT r{};GetClientRect(h,&r);HBRUSH bg=CreateSolidBrush(clr::bg);FillRect(dc,&r,bg);DeleteObject(bg);
            SetBkMode(dc,TRANSPARENT);Painter p{dc,true};render(p,r.right);RestoreDC(dc,saved);return 0;
        }
        case WM_NCLBUTTONDBLCLK: return 0;
        case WM_NCRBUTTONDOWN: return 0;
        case WM_NCRBUTTONUP: show_menu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
        case WM_CONTEXTMENU: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (x == -1 && y == -1) { POINT pt; GetCursorPos(&pt); x = pt.x; y = pt.y; }
            show_menu(x, y);
            return 0;
        }
        case WM_EXITSIZEMOVE: {
            RECT r;
            GetWindowRect(h, &r);
            ini_set(L"overlay", L"x", std::to_wstring(r.left));
            ini_set(L"overlay", L"y", std::to_wstring(r.top));
            return 0;
        }
        case WM_TIMER:
            if (wp == T_LOG) on_log_timer();
            else if (wp == T_KEYS) { on_keys_timer(); update_hud_input(); }
            else if (wp == T_UPD) check_update();
            else if (wp == T_TOP) {
                if (!g.hidden) SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

            }
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            HGDIOBJ oldFont = SelectObject(mem, g.fFoot);
            HBRUSH fb = CreateSolidBrush(clr::frame);
            FillRect(mem, &rc, fb);
            DeleteObject(fb);
            RECT in{1, 1, rc.right - 1, rc.bottom - 1};
            HBRUSH bg = CreateSolidBrush(clr::bg);
            FillRect(mem, &in, bg);
            DeleteObject(bg);
            SetBkMode(mem, TRANSPARENT);
            Painter p{mem, true};
            render(p, rc.right);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldFont);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_CLOSE:
            save_csv();
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            sound_stop();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HANDLE take_over_instance() {
    // Второй запуск закрывает уже работающий оверлей и занимает его место
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\LJOverlay_CS2");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND old = FindWindowW(L"LJOverlayWnd", nullptr);
        if (old) PostMessageW(old, WM_CLOSE, 0, 0);
        DWORD wr = WaitForSingleObject(mtx, 4000);
        if (wr != WAIT_OBJECT_0 && wr != WAIT_ABANDONED) {
            CloseHandle(mtx);
            return nullptr;
        }
    }
    return mtx;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    wchar_t selfBuf[MAX_PATH];
    GetModuleFileNameW(nullptr, selfBuf, MAX_PATH);
    std::wstring self = selfBuf, dir = self.substr(0, self.find_last_of(L"\\/"));
    std::wstring mainExe = dir + L"\\LJOverlay.exe", updExe = dir + L"\\LJOverlay_update.exe";
    bool isUpdate = _wcsicmp(self.c_str(), updExe.c_str()) == 0;
    g_selfExe = self;
    g_updExe = updExe;
    g_dir = dir;

    HANDLE mtx = take_over_instance();
    if (!mtx) {
        MessageBoxW(nullptr, L"Не получилось закрыть уже запущенный оверлей.\nЗакрой его: Ctrl+End или ПКМ → Выход.",
                    L"LJ Overlay", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return 0;
    }
    if (isUpdate) {
        // Обновление: старый оверлей закрыт — копируем себя в LJOverlay.exe и запускаем его
        ReleaseMutex(mtx);
        CloseHandle(mtx);
        bool copied = false;
        for (int i = 0; i < 40 && !copied; i++) {
            copied = CopyFileW(self.c_str(), mainExe.c_str(), FALSE) != 0;
            if (!copied) Sleep(100);
        }
        if (copied) {
            ShellExecuteW(nullptr, L"open", mainExe.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
            return 0;
        }
        mtx = take_over_instance();   // не вышло скопировать — работаем как есть
        if (!mtx) return 0;
    } else {
        // Убираем файл обновления (процесс обновления может ещё завершаться)
        for (int i = 0; i < 30; i++) {
            if (GetFileAttributesW(updExe.c_str()) == INVALID_FILE_ATTRIBUTES || DeleteFileW(updExe.c_str())) break;
            Sleep(100);
        }
    }

    typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
    auto setCtx = (SetCtxFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (!setCtx || !setCtx((HANDLE)(LONG_PTR)-2)) SetProcessDPIAware();   // -2 = SYSTEM_AWARE

    g.inst = hi;
    wchar_t mod[MAX_PATH];
    GetModuleFileNameW(nullptr, mod, MAX_PATH);
    g.exeDir = mod;
    g.exeDir = g.exeDir.substr(0, g.exeDir.find_last_of(L"\\/"));
    g.iniPath = g.exeDir + L"\\lj_overlay.ini";
    g.jumpsDir = g.exeDir + L"\\lj_jumps";
    if (GetFileAttributesW(g.iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring content = L"\xFEFF";
        content += DEFAULT_INI;
        write_file(g.iniPath, content.data(), content.size() * sizeof(wchar_t));
    }

    std::wstring log = ini_get(L"overlay", L"log", L"");
    g.autoLog = log.empty();
    g.tail.select(g.autoLog ? find_console_log() : console_path(log));
    if (wcsstr(GetCommandLineW(), L"--all")) g.tail.pos = 0;
    g.parser.player = U8(ini_get(L"overlay", L"player", L""));

    for (auto& [key, thr] : g_thr) {
        std::string v = U8(ini_get(L"thresholds", W(key).c_str(), L""));
        auto parts = split(v, ',');
        if (parts.size() == 2) {
            double a = pnum(parts[0]), b = pnum(parts[1]);
            if (ok(a) && ok(b)) thr = Thr{a, b};
        }
    }

    const std::pair<const char*, const wchar_t*> defs[] = {{"good", L"F6"}, {"bad", L"F7"}, {"clear", L"F8"},
                                                           {"lock", L"F9"}, {"hide", L"HOME"},
                                                           {"menu", L"INS"}, {"quit", L"CTRL+END"}};
    for (auto& [name, def] : defs) {
        Hotkey h;
        std::string val = U8(ini_get(L"hotkeys", W(name).c_str(), def));
        if (!parse_hotkey(val, h)) {
            g.statusCfg += (g.statusCfg.empty() ? "" : "\n") + std::string("Не понял клавишу в ini: ") + name + "=" + val;
            h = Hotkey{};
        }
        if (h.vk == VK_F10)
            g.statusCfg += (g.statusCfg.empty() ? "" : "\n") + std::string("Внимание: F10 в CS2 = выход из игры (") + name + ")";
        g.hk.push_back({name, h});
    }

    HDC sdc = GetDC(nullptr);
    g.dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    ReleaseDC(nullptr, sdc);
    load_visual_preferences();
    g.historyPath = g.exeDir + L"\\lj_history.dat";
    if (!history_store::load()) g.statusHistory="История попыток не прочитана: файл повреждён или недоступен. Импортирую console.log.";
    sound_load_assets();

    g.measureDC = CreateCompatibleDC(nullptr);
    apply_theme();
    rebuild_fonts();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    wc.lpszClassName = L"LJOverlayWnd";
    RegisterClassExW(&wc);
    std::wstring xOpt = ini_get(L"overlay", L"x", L"right");
    bool rightAligned = _wcsicmp(xOpt.c_str(), L"right") == 0;
    int x = rightAligned ? 0 : _wtoi(xOpt.c_str());
    int y = ini_int(L"overlay", L"y", 40);
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (!rightAligned && (x < vx || x > vx + vw - 80)) x = vx + 40;
    if (y < vy || y > vy + vh - 80) y = vy + 40;

    DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (g.locked ? WS_EX_TRANSPARENT : 0);
    g.hwnd = CreateWindowExW(ex, wc.lpszClassName, g.classicMenu ? L"LJ Overlay [CS 1.6]" : L"LJ Overlay [Modern]", WS_POPUP, x, y, 400, 300, nullptr, nullptr, hi, nullptr);
    if (!g.hwnd) return 1;
    SetLayeredWindowAttributes(g.hwnd, 0, (BYTE)g.alpha, LWA_ALPHA);
    relayout();
    if (rightAligned) {
        // По умолчанию справа на основном мониторе, KZ MENU слева остаётся открытым.
        int rightX = std::max(12, GetSystemMetrics(SM_CXSCREEN) - g.winW - 24);
        SetWindowPos(g.hwnd, nullptr, rightX, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(g.hwnd, SW_SHOWNOACTIVATE);
    if (!start_keyboard()) {
        stop_keyboard();
        g.statusCfg += "\nНе установлен WH_KEYBOARD_LL: используются опросные хоткеи.";
        relayout();
    }
    SetTimer(g.hwnd, T_LOG, 100, nullptr);
    SetTimer(g.hwnd, T_KEYS, 25, nullptr);
    SetTimer(g.hwnd, T_TOP, 2000, nullptr);
    if (!isUpdate) SetTimer(g.hwnd, T_UPD, 1500, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    stop_keyboard();
    for (HFONT f : {g.fBig, g.fType, g.fPill, g.fSub, g.fTile, g.fTile2, g.fCap, g.fSec, g.fTbl, g.fTblB, g.fTH,
                    g.fLbl, g.fFoot, g.fFootB, g.fHint, g.fMenuHead, g.fMenuRow, g.fMenuSmall, g.fMenuDigit})
        DeleteObject(f);
    DeleteDC(g.measureDC);
    ReleaseMutex(mtx);
    CloseHandle(mtx);
    return 0;
}
#endif


