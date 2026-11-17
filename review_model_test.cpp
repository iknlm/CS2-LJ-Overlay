#define LJ_TEST
#define main parser_cli_main
#include "lj_overlay.cpp"
#undef main
#include <cassert>
static std::string joined(const std::vector<std::string>& lines) {
    std::string out; for (const auto& line : lines) out += line + "\n"; return out;
}
static Jump sample() {
    Jump j; j.type = "LJ"; j.dist = 239.96712; j.distanceText = "239.96712";
    j.set("Mode", Val{false, NA, "VNL"}); j.set("Strafes", Val{true, 3, ""});
    for (const char* k : {"AvgSync", "Pre", "Max", "JumpEdge", "AvgBadAngles", "AvgOverlap", "AvgDeadAir", "AvgGainEff"})
        j.set(k, Val{true, 50, ""});
    for (int n = 1; n <= 3; ++n) {
        Strafe s; s.n = n; s.v = {{"Sync", 85}, {"Gain", 10}, {"Loss", 0}, {"Airtime", 33.33},
            {"BadAngles", 0}, {"Overlap", 0}, {"DeadAir", 0}, {"GainEff", 60}};
        j.strafes.push_back(s);
    }
    return j;
}
int main() {
    Jump base = sample();
    for (JumpDirection direction : {DIR_UNKNOWN, DIR_STRAIGHT, DIR_RIGHT, DIR_LEFT}) {
        Jump j = base; j.direction = direction;
        j.strafes[2].v["Sync"] = 0; j.strafes[2].v["Loss"] = -2.5; j.strafes[2].v["GainEff"] = 0;
        std::string text = joined(analyze_jump(j));
        assert(text.find(direction_name(direction)) != std::string::npos);
        assert(text.find("Последний стрейф #3: Sync 0% и Loss скорости 2.5") != std::string::npos);
        assert(text.find("коррекция перед приземлением") != std::string::npos);
        assert(text.find("не доказательство ошибки") != std::string::npos);
        assert(text.find("достиг") == std::string::npos && text.find("ошибка техники") == std::string::npos);
        std::swap(j.strafes[0], j.strafes[2]);
        assert(joined(analyze_jump(j)).find("Последний стрейф #3") != std::string::npos);
    }
    Jump weak = base; weak.strafes[1].v["Gain"] = 1; weak.strafes[1].v["GainEff"] = 10;
    auto text = joined(analyze_jump(weak));
    assert(text.find("Стрейф #2: GainEff 10%") != std::string::npos);
    assert(text.find("набор скорости на долю Airtime ниже остальных") != std::string::npos);
    weak.strafes[1].v["Airtime"] = NA;
    assert(joined(analyze_jump(weak)).find("набор скорости на долю Airtime") == std::string::npos);
    // Real Parser preserves -nan as unavailable, rather than a numeric zero.
    std::deque<Jump> jumps; Parser parser(jumps); std::string cmd;
    for (const auto& line : {"JS jumped 239.96712 units with a Long Jump", "VNL | 2 Strafes | 250 Pre",
         "#. Sync Gain Loss GainEff", "1. 85 10 -0.1 -nan", "2. 80 -nan -nan 50"}) parser.feed(line, cmd);
    assert(jumps.size() == 1 && jumps[0].strafes.size() == 2);
    assert(!std::isfinite(jumps[0].strafes[0].g("GainEff")) && !std::isfinite(jumps[0].strafes[1].g("Loss")));
    assert(joined(analyze_jump(jumps[0])).find("-nan") == std::string::npos);
    assert(next_rank_text(jumps[0]) == "До Godlike: 0.03288u");
    Jump rounded = base; rounded.dist = 239.97; rounded.distanceText = "239.97";
    assert(next_rank_text(rounded) == "До Godlike: 0.03u");
    assert(next_rank_text(rounded).find("0.03000") == std::string::npos);
    rounded.distanceText.clear(); assert(next_rank_text(rounded) == "До Godlike: 0.03u");

    Jump other = base; other.dist = 240.42; other.distanceText = "240.42";
    other.stats["AvgSync"].d = 70; other.strafes[1].v["Gain"] = 15;
    other.strafes[1].v["Loss"] = -1;
    text = joined(compare_jumps(base, other));
    for (const char* key : {"Distance:", "Sync:", "Pre Speed:", "Max Speed:", "JumpEdge:", "BadAngles:", "Overlap:", "DeadAir:", "GainEff:", "Gain скорости", "Loss скорости", "#1:", "#2:", "#3:"})
        assert(text.find(key) != std::string::npos);
    assert(text.find("Δ 0.45 u") != std::string::npos); // No precision beyond the less precise source.
    assert(text.find("Loss скорости не определяет недобор дистанции") != std::string::npos);
    other.strafes.pop_back(); other.stats["Strafes"].d = 2;
    text = joined(compare_jumps(base, other));
    assert(text.find("при разном количестве") != std::string::npos && text.find("#1:") == std::string::npos);
    other.stats["Strafes"].d = 3;
    assert(joined(compare_jumps(base, other)).find("неполна") != std::string::npos);
    other = base; other.stats.erase("Pre"); other.strafes[0].v["Loss"] = NAN;
    text = joined(compare_jumps(base, other));
    assert(text.find("Pre Speed: недостаточно данных") != std::string::npos);
    assert(text.find("Loss: нет полной корректной таблицы") != std::string::npos);
    assert(text.find("nan") == std::string::npos);
    other = base; other.direction = DIR_LEFT;
    assert(joined(compare_jumps(base, other)).find("справочно") != std::string::npos);
    other = base; other.stats["Mode"].s = "CKZ";
    assert(joined(compare_jumps(base, other)).find("справочно") != std::string::npos);
    puts("PASS directions unknown/straight/right/left; terminal Sync=0/Loss with uncertainty; numbered last strafe; Gain normalized by Airtime and GainEff; real -nan parser; source precision; pair metrics/strafe rows; unequal counts; incomplete/NaN comparison; different modes/directions");
}
