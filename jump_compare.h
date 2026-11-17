// B minus A; all claims are measured differences, not a counterfactual jump.
static bool comparison_percent(double v) { return std::isfinite(v) && v >= 0 && v <= 100; }
static double strafe_total(const Jump& j, const char* key) {
    if (!analysis_complete_table(j)) return NA;
    double total = 0;
    int sign = 0;
    for (const auto& s : j.strafes) {
        double v = s.g(key);
        if (!std::isfinite(v)) return NA;
        if (std::string(key) == "Loss") {
            int next = v > 0 ? 1 : v < 0 ? -1 : 0;
            if (next && sign && next != sign) return NA;
            if (next) sign = next;
            v = std::abs(v);
        }
        total += v;
    }
    return std::isfinite(total) ? total : NA;
}
static std::vector<std::string> compare_jumps(const Jump& a, const Jump& b) {
    std::vector<std::string> out;
    out.push_back("A: " + distance_label(a) + "u " + a.type + " " + a.gs("Mode") +
                  " → B: " + distance_label(b) + "u " + b.type + " " + b.gs("Mode"));
    bool compatible = a.type == b.type && a.rank() != R_UNKNOWN && b.rank() != R_UNKNOWN &&
                      upper(trim(a.gs("Mode"))) == upper(trim(b.gs("Mode"))) && a.direction == b.direction && a.direction != DIR_UNKNOWN;
    out.push_back(compatible ? "Разности: B − A. Они описывают измерения, а не причины результата." :
        "Тип, режим или направление различаются/неизвестны: числа показаны справочно, вывод о качестве техники не делается.");
    if (std::isfinite(a.dist) && std::isfinite(b.dist)) {
        int precision = std::min(distance_precision(a), distance_precision(b));
        out.push_back(sfmt("Distance: %s → %s u; Δ %.*f u.", distance_label(a).c_str(), distance_label(b).c_str(), precision, b.dist - a.dist));
    }
    if (comparison_percent(a.g("AvgSync")) && comparison_percent(b.g("AvgSync")) && a.g("AvgSync") != b.g("AvgSync"))
        out.push_back(std::string("В B средний Sync ") + (b.g("AvgSync") > a.g("AvgSync") ? "выше" : "ниже") +
            "; это измеренное различие, оно само по себе не объясняет дистанцию. Проверь участки с разным Sync ниже.");
    struct Metric { const char* key; const char* label; bool percent; };
    const Metric metrics[] = {
        {"AvgSync", "Sync", true}, {"Pre", "Pre Speed", false}, {"Max", "Max Speed", false},
        {"JumpEdge", "JumpEdge", false}, {"AvgBadAngles", "BadAngles", true},
        {"AvgOverlap", "Overlap", true}, {"AvgDeadAir", "DeadAir", true}, {"AvgGainEff", "GainEff", true}
    };
    for (const auto& m : metrics) {
        double va = a.g(m.key), vb = b.g(m.key);
        bool valid = m.percent ? comparison_percent(va) && comparison_percent(vb) : std::isfinite(va) && std::isfinite(vb);
        if (valid) out.push_back(sfmt("%s: %s → %s; Δ %+.2f%s.", m.label,
            analysis_number(va).c_str(), analysis_number(vb).c_str(), vb - va, m.percent ? " п.п." : ""));
        else out.push_back(std::string(m.label) + ": недостаточно данных у A или B.");
    }
    for (const char* key : {"Gain", "Loss"}) {
        double va = strafe_total(a, key), vb = strafe_total(b, key);
        if (std::isfinite(va) && std::isfinite(vb))
            out.push_back(sfmt("%s скорости по стрейфам: %s → %s; Δ %+.2f. Это не units дистанции.", key,
                analysis_number(va).c_str(), analysis_number(vb).c_str(), vb - va));
        else out.push_back(std::string(key) + ": нет полной корректной таблицы у A или B.");
    }
    if (!analysis_complete_table(a) || !analysis_complete_table(b)) {
        out.push_back("Таблица стрейфов неполна: построчное сравнение недоступно.");
        return out;
    }
    if (a.strafes.size() != b.strafes.size()) {
        out.push_back(sfmt("Стрейфов A: %zu, B: %zu. Номера не сопоставляются при разном количестве; отсутствующие стрейфы не заменяются нулями.", a.strafes.size(), b.strafes.size()));
        return out;
    }
    out.push_back("Стрейфы по номеру, A → B (одинаковый номер не гарантирует одну фазу полёта):");
    for (size_t n = 1; n <= a.strafes.size(); ++n) {
        const Strafe *sa = nullptr, *sb = nullptr;
        for (const auto& s : a.strafes) if (s.n == (int)n) sa = &s;
        for (const auto& s : b.strafes) if (s.n == (int)n) sb = &s;
        std::string line = sfmt("#%zu:", n);
        int paired = 0;
        for (const char* key : {"Sync", "Gain", "Loss", "BadAngles", "Overlap", "DeadAir", "GainEff"}) {
            double va = sa->g(key), vb = sb->g(key);
            bool percent = std::string(key) != "Gain" && std::string(key) != "Loss";
            if (!(percent ? comparison_percent(va) && comparison_percent(vb) : std::isfinite(va) && std::isfinite(vb))) continue;
            ++paired;
            if (va == vb) continue;
            line += " " + std::string(key) + " " + analysis_number(va) + "→" + analysis_number(vb) + ";";
        }
        if (line == sfmt("#%zu:", n)) line += paired ? " в доступных парных метриках числовых различий нет." : " нет пар корректных метрик.";
        out.push_back(line);
    }
    out.push_back("Проверь участки с отличиями по таймлайну обеих попыток. Loss скорости не определяет недобор дистанции.");
    return out;
}
