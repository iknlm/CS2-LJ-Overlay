// Local observations only: no parser, rank, CSV, sound or physics changes.
static std::string analysis_number(double value) {
    std::string s = sfmt("%.10f", value);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    if (value > 0 && s == "0") return sfmt("%.15g", value);
    return s;
}

static int distance_precision(const Jump& j) {
    if (j.distanceText.empty()) return 2; // No invented precision for old/rounded data.
    auto dot = j.distanceText.find('.');
    return dot == std::string::npos ? 0 : std::min(10, (int)(j.distanceText.size() - dot - 1));
}
static std::string distance_label(const Jump& j) {
    return j.distanceText.empty() ? sfmt("%.2f", j.dist) : j.distanceText;
}
static const char* direction_name(JumpDirection direction) {
    switch (direction) {
        case DIR_STRAIGHT: return "Прямой прыжок";
        case DIR_RIGHT: return "Диагональ вправо";
        case DIR_LEFT: return "Диагональ влево";
        default: return "Направление не задано";
    }
}
static std::string landing_note(const Jump& j) {
    return std::string(j.direction == DIR_RIGHT || j.direction == DIR_LEFT ?
        "При выбранной диагонали возможна намеренная коррекция перед приземлением." :
        "Возможна намеренная коррекция перед приземлением.") +
        " Лог не подтверждает намерение; это не доказательство ошибки. Проверь конец таймлайна.";
}

// One rule for both HUD and report; deliberately no unrestricted variant.
static std::string next_rank_text(const Jump& j) {
    auto policy=preference(j);
    if (policy && policy->goalMode != GOAL_NEXT) {
        auto progress=goal_progress(j); return progress.isNear ? progress.text : "";
    }
    // Preserve the precision and fallback semantics of the existing Near Rank.
    if (j.distanceText.empty()) {
        int rank=j.rank(); if(rank==R_UNKNOWN || rank==R_WRECKER) return "";
        for(const auto& row:RANK_THRESHOLDS) if(j.type==row.type) {
            double remaining=(upper(trim(j.gs("Mode")))=="VNL" ? row.vnl : row.kz)[rank]-j.dist;
            if(remaining>0 && remaining<=0.50) {
                auto amount=sfmt("%.2f",remaining); if(pnum(amount)==0) amount="<0.01";
                return std::string("До ")+rank_name(rank+1)+": "+amount+"u";
            }
        }
        return "";
    }
    auto progress=goal_progress(j);
    if(!progress.isNear) return "";
    auto pos=progress.text.find(": ");
    auto amount=progress.text.substr(pos+2); amount.pop_back();
    int digits=distance_precision(j);
    auto dot=amount.find('.');
    int have=dot==std::string::npos ? 0 : (int)(amount.size()-dot-1);
    if(have<digits) { if(dot==std::string::npos) amount+='.'; amount.append(digits-have,'0'); }
    return progress.text.substr(0,pos+2)+amount+"u";
}

static bool analysis_complete_table(const Jump& j) {
    double expected = j.g("Strafes");
    if (!std::isfinite(expected) || expected < 1 || expected != std::floor(expected) ||
        expected != (double)j.strafes.size()) return false;
    std::map<int, bool> seen;
    for (const auto& s : j.strafes) {
        if (s.n < 1 || s.n > expected || seen[s.n]) return false;
        seen[s.n] = true;
    }
    return true;
}

static double analysis_median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    size_t mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : values[mid - 1] / 2 + values[mid] / 2;
}

struct AnalysisObservation {
    int priority;
    double contrast;
    std::string text;
};

static std::vector<std::string> analyze_jump(const Jump& j) {
    std::vector<std::string> out;
    out.push_back("Игрок: " + j.playerName() + " · " + distance_label(j) + "u · " + j.type + " · " +
                  (j.gs("Mode").empty() ? "режим неизвестен" : j.gs("Mode")) + " · " +
                  direction_name(j.direction) + (j.direction == DIR_UNKNOWN ? " (лог не определяет)" : " (выбрано вручную)"));
    if (j.rank() == R_UNKNOWN) out.push_back("Ранг не определён: режим, тип или дистанция не распознаны.");
    else {
        std::string nearRank = preference(j) && preference(j)->goalMode != GOAL_NEXT ? goal_progress(j).text : next_rank_text(j);
        out.push_back(std::string("Результат: ") + j.rankName() + "." + (nearRank.empty() ? "" : " " + nearRank));
    }
    // A closed parser block can still be incomplete. Do not infer from its subset.
    if (!analysis_complete_table(j)) {
        out.push_back("Недостаточно данных: таблица стрейфов неполна или её нумерация не подтверждена. Сравнение стрейфов недоступно.");
        return out;
    }
    if (j.strafes.size() < 2) {
        out.push_back("Недостаточно данных для сравнения: в попытке только один стрейф. По нему нельзя выделить провал относительно остальных.");
        return out;
    }

    std::vector<AnalysisObservation> observations;
    const Strafe& last = *std::max_element(j.strafes.begin(), j.strafes.end(), [](const auto& a, const auto& b) { return a.n < b.n; });
    double lastSync = last.g("Sync"), lastLoss = last.g("Loss");
    bool landing = std::isfinite(lastSync) && lastSync == 0 && std::isfinite(lastLoss) && lastLoss != 0;
    if (landing) observations.push_back({0, 100,
        sfmt("Последний стрейф #%d: Sync 0%% и Loss скорости %s. %s", last.n,
             analysis_number(std::abs(lastLoss)).c_str(), landing_note(j).c_str())});
    struct Metric { const char* key; const char* threshold; const char* advice; bool low; };
    const Metric metrics[] = {
        {"Sync", "sync", "Проверь согласование клавиши с поворотом мыши на этом участке.", true},
        {"Overlap", "overlap", "Проверь момент отпускания предыдущей клавиши при смене стрейфа.", false},
        {"DeadAir", "deadair", "Проверь паузы ввода при смене направления.", false},
        {"BadAngles", "badangles", "Проверь поворот мыши на этом участке.", false},
        {"GainEff", "gaineff", "Проверь эффективность набора скорости и длительность этого стрейфа.", true}
    };
    int comparable = 0, allGood = 0;
    bool missing = false;
    for (const auto& metric : metrics) {
        std::vector<double> values;
        size_t worst = 0;
        for (size_t i = 0; i < j.strafes.size(); ++i) {
            double v = j.strafes[i].g(metric.key);
            if (!std::isfinite(v) || v < 0 || v > 100) break;
            values.push_back(v);
            if (metric.low ? v < values[worst] : v > values[worst]) worst = i;
        }
        if (values.size() != j.strafes.size()) { missing = true; continue; }
        ++comparable;
        double value = values[worst];
        bool terminal = j.strafes[worst].n == (int)j.strafes.size();
        if (landing && terminal && (std::string(metric.key) == "Sync" || std::string(metric.key) == "GainEff")) continue;
        Tone worstTone = tone(metric.threshold, value);
        if (worstTone == T_GOOD) { ++allGood; continue; }
        std::vector<double> others = values;
        others.erase(others.begin() + worst);
        double typical = analysis_median(others);
        Tone typicalTone = tone(metric.threshold, typical);
        // A distinct HUD band is evidence of a meaningful relative difference.
        // No new numerical severity thresholds are introduced by the analyzer.
        if (worstTone > typicalTone && (metric.low ? value < typical : value > typical)) {
            int priority = worstTone == T_BAD && typicalTone == T_GOOD ? 0 : 1;
            observations.push_back({priority, std::abs(value - typical),
                sfmt("Стрейф #%d: %s %s%%, у остальных медиана %s%% — %s. %s",
                    j.strafes[worst].n, metric.key, analysis_number(value).c_str(), analysis_number(typical).c_str(),
                    metric.low ? "значение ниже" : "значение выше",
                    terminal ? landing_note(j).c_str() : metric.advice)});
        } else if (std::all_of(values.begin(), values.end(), [&](double v) { return tone(metric.threshold, v) == T_BAD; })) {
            auto range = std::minmax_element(values.begin(), values.end());
            observations.push_back({3, std::abs(value - typical),
                sfmt("%s в плохом диапазоне HUD у всех стрейфов (%s–%s%%); отдельного провала по диапазонам нет. %s",
                    metric.key, analysis_number(*range.first).c_str(), analysis_number(*range.second).c_str(), metric.advice)});
        }
    }

    // Raw Gain depends on time spent in a strafe. Only flag a weak rate when
    // finite positive Airtime and weak measured GainEff corroborate it.
    std::vector<double> rates;
    size_t weak = 0;
    for (size_t i = 0; i < j.strafes.size(); ++i) {
        double gain = j.strafes[i].g("Gain"), airtime = j.strafes[i].g("Airtime");
        if (!std::isfinite(gain) || !std::isfinite(airtime) || airtime <= 0 || airtime > 100) break;
        double rate = gain / airtime;
        if (!std::isfinite(rate)) break;
        rates.push_back(rate);
        if (rates[i] < rates[weak]) weak = i;
    }
    if (rates.size() == j.strafes.size()) {
        auto peers = rates; peers.erase(peers.begin() + weak);
        double efficiency = j.strafes[weak].g("GainEff");
        if (rates[weak] < *std::min_element(peers.begin(), peers.end()) &&
            std::isfinite(efficiency) && efficiency >= 0 && efficiency <= 100 && tone("gaineff", efficiency) != T_GOOD &&
            !(landing && j.strafes[weak].n == (int)j.strafes.size()))
            observations.push_back({1, 0,
                sfmt("Стрейф #%d: набор скорости на долю Airtime ниже остальных (Gain %s за %s%% времени; GainEff %s%%). Проверь длительность и поворот, причина по логу не установлена. %s",
                    j.strafes[weak].n, analysis_number(j.strafes[weak].g("Gain")).c_str(),
                    analysis_number(j.strafes[weak].g("Airtime")).c_str(), analysis_number(efficiency).c_str(),
                    j.strafes[weak].n == (int)j.strafes.size() ? landing_note(j).c_str() : "")});
    }

    // Loss is speed, never distance. Compare magnitudes only with complete,
    // finite values and an unambiguous sign convention within this attempt.
    bool lossValid = true;
    int sign = 0;
    size_t worstLoss = 0;
    std::vector<double> losses;
    for (size_t i = 0; i < j.strafes.size(); ++i) {
        double v = j.strafes[i].g("Loss");
        if (!std::isfinite(v)) { lossValid = false; break; }
        int currentSign = v > 0 ? 1 : v < 0 ? -1 : 0;
        if (sign && currentSign && currentSign != sign) { lossValid = false; break; }
        if (currentSign) sign = currentSign;
        losses.push_back(std::abs(v));
        if (losses[i] > losses[worstLoss]) worstLoss = i;
    }
    if (lossValid) {
        double rest = 0;
        for (size_t i = 0; i < losses.size(); ++i) if (i != worstLoss) rest += losses[i];
        // Concentration means more speed loss here than on all peers combined,
        // not an invented absolute cutoff or a claim about physical jump units.
        if (std::isfinite(rest) && losses[worstLoss] > rest && !(landing && j.strafes[worstLoss].n == (int)j.strafes.size()))
            observations.push_back({2, 0,
                sfmt("На стрейфе #%d Loss скорости %s, у остальных вместе %s: потери сосредоточены здесь. %s Недобор дистанции по Loss не рассчитывается.",
                    j.strafes[worstLoss].n, analysis_number(losses[worstLoss]).c_str(), analysis_number(rest).c_str(),
                    j.strafes[worstLoss].n == (int)j.strafes.size() ? landing_note(j).c_str() : "Проверь этот участок по вводу.")});
    } else missing = true;

    // Prefer isolated bad-vs-good contrasts, then other band changes, then
    // concentrated Loss, then shared bad bands. Within a class use percent-point
    // contrast; speed and percentage magnitudes are never compared with each other.
    std::stable_sort(observations.begin(), observations.end(), [](const auto& a, const auto& b) {
        return a.priority != b.priority ? a.priority < b.priority : a.contrast > b.contrast;
    });
    for (size_t i = 0; i < std::min<size_t>(3, observations.size()); ++i) out.push_back(observations[i].text);
    if (observations.empty()) {
        if (comparable == 0)
            out.push_back("Недостаточно данных: нет полных корректных процентных метрик для сравнения стрейфов. По одному Loss причина результата не определяется.");
        else if (comparable > 0 && allGood == comparable)
            out.push_back("По доступным метрикам стрейфы держатся в хорошем диапазоне HUD. Выраженного провала при сравнении не обнаружено.");
        else out.push_back("Сравнение не выделило выраженного провала: нет перехода в худший диапазон HUD или концентрации Loss на одном стрейфе. Причина результата по этим данным не установлена.");
    }
    if (missing && !(observations.empty() && comparable == 0))
        out.push_back("Часть метрик отсутствует или некорректна; выводы по ним пропущены.");
    return out;
}

