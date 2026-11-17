#define LJ_TEST
#define main parser_cli_main
#include "lj_overlay.cpp"
#undef main
#include <cassert>
#include <limits>

static Jump make_jump(const char* type, double distance, const char* mode) {
    Jump j; j.type = type; j.dist = distance; j.set("Mode", Val{false, NA, mode}); return j;
}
static std::string report(const Jump& j) {
    std::string out; for (const auto& line : analyze_jump(j)) out += line + "\n"; return out;
}
static Jump fixture() {
    Jump j = make_jump("LJ", 239.96712, "VNL");
    j.set("Strafes", Val{true, 3, ""});
    for (int n = 1; n <= 3; ++n) {
        Strafe s; s.n = n;
        s.v = {{"Sync", 85}, {"Gain", 10}, {"Loss", 0}, {"Overlap", 0}, {"DeadAir", 0}, {"BadAngles", 0}, {"GainEff", 60}, {"Airtime", 33.33}};
        j.strafes.push_back(s);
    }
    return j;
}
static void check_language(const Jump& j) {
    std::string text = report(j);
    for (const char* forbidden : {"Лучший Sync", "Лучший Gain", "Сумма", "сумма", "Наибольший Loss", "достиг", "был бы", "если бы", "из-за", "гарант", "потеря дистанции"})
        assert(text.find(forbidden) == std::string::npos);
    auto lines = analyze_jump(j);
    // Two result lines, <=3 observations, at most one missing-data notice.
    assert(lines.size() <= 6);
}
int main() {
    int checks = 0;
    for (const auto& row : RANK_THRESHOLDS) for (const char* mode : {"VNL", "CKZ", "KZT"}) {
        const double* t = std::string(mode) == "VNL" ? row.vnl : row.kz;
        for (int rank = 0; rank < 5; ++rank) {
            for (double delta : {0.50001, 0.5, 0.03288, 0.0, -0.00001}) {
                Jump j = make_jump(row.type, t[rank] - delta, mode);
                bool wanted = delta > 0 && delta <= 0.5;
                std::string text = next_rank_text(j);
                assert(!text.empty() == wanted);
                assert((report(j).find("До ") != std::string::npos) == wanted);
                if (wanted) assert(text.find(rank_name(rank + 1)) != std::string::npos);
                ++checks;
            }
            Jump j = make_jump(row.type, std::nextafter(t[rank], -INFINITY), mode);
            assert(!next_rank_text(j).empty()); ++checks;
            j.dist = std::nextafter(t[rank] - 0.5, -INFINITY);
            assert(next_rank_text(j).empty()); ++checks; // Rounds to 0.50, but is >0.50.
            j.dist = std::nextafter(t[rank] - 0.5, INFINITY);
            assert(!next_rank_text(j).empty()); ++checks;
        }
        assert(next_rank_text(make_jump(row.type, t[4] + 1, mode)).empty());
    }
    struct Example { const char* distance; const char* wanted; };
    const Example examples[] = {
        {"219.07756", ""}, {"229.75", "До Impressive: 0.25u"},
        {"234.60", "До Perfect: 0.40u"}, {"239.96712", "До Godlike: 0.03288u"},
        {"244.80", "До Ownage: 0.20u"}, {"249.90", "До Wrecker: 0.10u"}
    };
    for (const auto& example : examples) {
        std::deque<Jump> jumps; Parser p(jumps); std::string cmd;
        p.feed(std::string("JS jumped ") + example.distance + " units with a Long Jump", cmd);
        p.feed("VNL | 3 Strafes | 250 Pre", cmd);
        Jump& j = jumps.back(); assert(j.distanceText == example.distance);
        assert(next_rank_text(j) == example.wanted);
        assert(report(j).find(std::string(example.distance) + "u") != std::string::npos);
        assert(report(j).find("10.92244") == std::string::npos);
        assert((report(j).find("До ") != std::string::npos) == (example.wanted[0] != 0));
        printf("PASS LJ VNL %s -> %s\n", example.distance, example.wanted[0] ? example.wanted : "no next-rank message");
        check_language(j);
    }
    for (const char* mode : {"", "UNKNOWN", "VNL?"}) {
        Jump j = make_jump("LJ", 239.99, mode);
        assert(next_rank_text(j).empty()); assert(report(j).find("Ранг не определён") != std::string::npos);
    }
    for (double distance : {double(NAN), double(INFINITY), -1.0}) assert(next_rank_text(make_jump("LJ", distance, "VNL")).empty());
    assert(next_rank_text(make_jump("CJ", 239.99, "VNL")).empty());
    assert(next_rank_text(make_jump("LJ", 250, "VNL")).empty());

    Jump good = fixture();
    assert(report(good).find("хорошем диапазоне") != std::string::npos);
    assert(report(good).find("Проверь") == std::string::npos);
    const char* metrics[] = {"Sync", "Overlap", "DeadAir", "BadAngles"};
    const double problems[] = {40, 25, 20, 40};
    for (int i = 0; i < 4; ++i) {
        Jump j = good; j.strafes[1].v[metrics[i]] = problems[i];
        std::string text = report(j);
        assert(text.find("Стрейф #2: " + std::string(metrics[i])) != std::string::npos);
        assert(text.find("у остальных медиана") != std::string::npos && text.find("Проверь") != std::string::npos);
        assert(analyze_jump(j).size() == 3);
        check_language(j);
    }
    Jump crowded = good;
    for (int i = 0; i < 4; ++i) crowded.strafes[1].v[metrics[i]] = problems[i];
    crowded.strafes[1].v["Loss"] = -3;
    assert(analyze_jump(crowded).size() == 5); // Five candidates, only three observations.
    check_language(crowded);
    Jump loss = good;
    loss.strafes[0].v["Loss"] = -0.1; loss.strafes[1].v["Loss"] = -3; loss.strafes[2].v["Loss"] = -0.2;
    std::string text = report(loss);
    assert(text.find("#2 Loss скорости 3, у остальных вместе 0.3") != std::string::npos);
    assert(text.find("Недобор дистанции по Loss не рассчитывается") != std::string::npos);
    check_language(loss);
    printf("\nSample concise report:\n%s\n", text.c_str());
    Jump positiveLoss = loss;
    for (auto& s : positiveLoss.strafes) s.v["Loss"] = -s.v["Loss"];
    assert(report(positiveLoss) == text);
    for (double invalid : {NAN, INFINITY}) {
        Jump j = loss; j.strafes[2].v["Loss"] = invalid;
        assert(report(j).find("потери сосредоточены") == std::string::npos);
        assert(report(j).find("метрик отсутствует") != std::string::npos);
    }
    Jump mixed = loss; mixed.strafes[2].v["Loss"] = 0.2;
    assert(report(mixed).find("потери сосредоточены") == std::string::npos);
    Jump equal = good; for (auto& s : equal.strafes) s.v["Loss"] = -0.5;
    assert(report(equal).find("потери сосредоточены") == std::string::npos);
    Jump uniform = good; for (auto& s : uniform.strafes) s.v["Sync"] = 40;
    assert(report(uniform).find("Sync в плохом диапазоне HUD у всех") != std::string::npos);
    assert(report(uniform).find("Стрейф #") == std::string::npos);
    Jump tiny = good; tiny.strafes[1].v["Sync"] = 84.99;
    assert(report(tiny).find("Проверь") == std::string::npos); // Difference within good band is not a problem.
    Thr previous = g_thr["sync"]; g_thr["sync"] = Thr{30, 20};
    assert(report(uniform).find("хорошем диапазоне") != std::string::npos);
    g_thr["sync"] = previous; // Uses the user's HUD settings, no invented cutoffs.

    Jump incomplete = crowded; incomplete.strafes.pop_back();
    assert(report(incomplete).find("Недостаточно данных") != std::string::npos);
    assert(report(incomplete).find("Проверь") == std::string::npos);
    Jump duplicate = crowded; duplicate.strafes[2].n = 1;
    assert(report(duplicate).find("Недостаточно данных") != std::string::npos);
    Jump countMissing = crowded; countMissing.stats.erase("Strafes");
    assert(report(countMissing).find("Недостаточно данных") != std::string::npos);
    Jump single = good; single.strafes.resize(1); single.stats["Strafes"].d = 1;
    assert(report(single).find("только один стрейф") != std::string::npos);
    Jump noMetrics = good; for (auto& s : noMetrics.strafes) s.v.clear();
    assert(report(noMetrics).find("Недостаточно данных") != std::string::npos);
    Jump lossOnly = noMetrics; for (auto& s : lossOnly.strafes) s.v["Loss"] = -0.5;
    assert(report(lossOnly).find("Недостаточно данных") != std::string::npos);
    assert(report(lossOnly).find("нет перехода") == std::string::npos);
    for (double invalid : {double(NAN), double(INFINITY), -1.0, 101.0}) {
        Jump j = good; j.strafes[0].v["Sync"] = invalid; j.strafes[1].v["Sync"] = 40;
        assert(report(j).find("Стрейф #2: Sync") == std::string::npos);
        assert(report(j).find("метрик отсутствует") != std::string::npos);
    }
    for (const auto& j : {good, crowded, loss, mixed, equal, uniform, incomplete, duplicate, countMissing, single, noMetrics}) check_language(j);
    printf("PASS %d all-mode/all-type rank boundary checks incl. nextafter at 0.50; six requested examples; metric comparisons; <=3 observations; missing data; no distance-from-Loss claims\n", checks);
}
