// Adapter around the existing Parser: source identity, replay protection and
// idempotent detailed rows. No scoring or game-process access here.
struct ConsoleImport {
    static constexpr size_t none = (size_t)-1;
    struct Event { Parser::Ev ev = Parser::NONE; size_t index = none; bool boundary = false; };
    std::map<std::string, size_t> records, stamped;
    size_t active = none;
    std::vector<std::string> columns;
    bool activeCompact = false, replayCandidate = false;
    Jump replayHeader;
    std::string activeRecordKey, activeTimeKey;
    bool activeStamped = false;

    void reset_stream(Parser& p) { active = none; columns.clear(); replayCandidate=false; activeRecordKey.clear(); activeTimeKey.clear(); activeStamped=false; p.reset(); }
    void clear(Parser& p) { records.clear(); stamped.clear(); reset_stream(p); }
    static bool equal_number(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }
    static bool equal_metrics(const std::map<std::string, double>& a, const std::map<std::string, double>& b) {
        if (a.size() != b.size()) return false;
        for (const auto& [key, value] : a) {
            auto it = b.find(key); if (it == b.end() || !equal_number(value, it->second)) return false;
        }
        return true;
    }
    static bool equal_data(const Jump& a, const Jump& b) {
        if (a.stats.size() != b.stats.size() || a.strafes.size() != b.strafes.size() || a.keys != b.keys) return false;
        for (const auto& [key, value] : a.stats) {
            auto it = b.stats.find(key);
            if (it == b.stats.end() || value.num != it->second.num || value.s != it->second.s ||
                (value.num && !equal_number(value.d, it->second.d))) return false;
        }
        for (size_t i = 0; i < a.strafes.size(); ++i)
            if (a.strafes[i].n != b.strafes[i].n || !equal_metrics(a.strafes[i].v, b.strafes[i].v)) return false;
        return true;
    }
    static std::string identity_line(const std::string& raw) {
        std::string s;
        for (unsigned char c : raw) if (c == '\t' || (c >= 32 && c != 127)) s += (char)c;
        if (starts_with(s, "\xEF\xBB\xBF")) s.erase(0, 3);
        return trim(s);
    }
    static bool stamped_header(const std::string& raw) {
        return identity_line(raw) != clean(raw); // clean removes only the known console date/time prefix.
    }
    static bool compact(const std::string& s, Jump& out) {
        static const std::regex head(R"(^JS\s*\|\s*([A-Z]+):\s*(\d+(?:\.\d+)?)(?:\s*\||$))");
        std::smatch m;
        if (!std::regex_search(s, m, head)) return false;
        out.name.clear(); out.type = m[1]; out.distanceText = m[2]; out.dist = pnum(m[2]);
        const std::pair<const char*, const char*> names[] = {{"LJ","Long Jump"},{"BH","Bunnyhop"},{"MBH","Multi Bunnyhop"},
            {"WJ","Weird Jump"},{"LAJ","Ladder Jump"},{"JB","Jumpbug"},{"CJ","Count Jump"},{"DBH","Drop Bunnyhop"},{"LAH","Ladderhop"}};
        for (const auto& [abbr, full] : names) if (out.type == abbr) out.typeFull = full;
        if (out.typeFull.empty()) return false;
        static const std::regex strafes(R"((\d+)\s+Strafes?(?:\s*\((\d+(?:\.\d+)?)%\))?)");
        if (std::regex_search(s, m, strafes)) {
            out.set("Strafes", Val{true, pnum(m[1]), ""});
            if (m[2].matched) out.set("AvgSync", Val{true, pnum(m[2]), ""});
        }
        static const std::regex speed(R"((\d+(?:\.\d+)?)\s*/\s*(\d+(?:\.\d+)?)\s+Speed)");
        if (std::regex_search(s, m, speed)) {
            out.set("Pre", Val{true, pnum(m[1]), ""}); out.set("Max", Val{true, pnum(m[2]), ""});
        }
        static const std::regex values(R"(([-+]?\d+(?:\.\d+)?)\s+(JumpEdge|W)\b)");
        for (std::sregex_iterator it(s.begin(), s.end(), values), end; it != end; ++it)
            out.set((*it)[2], Val{true, pnum((*it)[1]), ""});
        for (const auto& segment : split(s, '|')) {
            auto mode = upper(trim(segment));
            if (mode == "VNL" || mode == "CKZ" || mode == "KZT") out.set("Mode", Val{false, NA, mode});
        }
        return true;
    }
    Event feed(Parser& p, const std::string& raw, const std::string& source, long long offset, std::string& cmd) {
        std::string s = clean(raw);
        if (s.empty() || raw.size() > 4000) return {};
        Jump shortJump;
        bool isCompact = starts_with(s, "JS") && compact(s, shortJump);
        bool header = isCompact || upper(s).find(" JUMPED ") != std::string::npos;
        if (header) {
            // A compact echo of the current detailed record must not reduce precision
            // or create a second jump. It has no player/mode identity of its own.
            if (isCompact && !activeCompact && active < p.jumps.size() && p.cur &&
                std::chrono::steady_clock::now() - p.curT <= std::chrono::seconds(3) &&
                (!stamped_header(raw) || identity_line(raw).substr(6, 8) == p.jumps[active].time) &&
                (shortJump.gs("Mode").empty() || shortJump.gs("Mode") == p.jumps[active].gs("Mode")) &&
                p.jumps[active].type == shortJump.type &&
                sfmt("%.*f", (int)(shortJump.distanceText.size() - (shortJump.distanceText.find('.') == std::string::npos ? shortJump.distanceText.size() : shortJump.distanceText.find('.') + 1)), p.jumps[active].dist) == shortJump.distanceText)
                return {};
            if (isCompact && !p.player.empty()) { reset_stream(p); return {Parser::NONE, none, true}; }
            std::string headerLine = isCompact ? "JS jumped " + shortJump.distanceText + " units with a " + shortJump.typeFull : raw;
            auto ev = p.feed(headerLine, cmd);
            if (ev != Parser::NEW) { reset_stream(p); return {Parser::NONE, none, true}; }
            std::string identity = identity_line(raw);
            std::string recordKey = source + "\n" + std::to_string(offset) + "\n" + identity;
            std::string timeKey = source + "\n" + identity;
            size_t target = none;
            auto found = records.find(recordKey);
            if (found != records.end() && found->second + 1 < p.jumps.size()) target = found->second;
            if (target == none && stamped_header(raw)) {
                auto same = stamped.find(timeKey);
                if (same != stamped.end() && same->second + 1 < p.jumps.size()) target = same->second;
            }
            bool fresh = target == none;
            replayCandidate=!fresh;
            replayHeader=p.jumps.back();
            activeRecordKey=recordKey;activeTimeKey=timeKey;activeStamped=stamped_header(raw);
            if (activeStamped && identity.size()>=14) replayHeader.time=identity.substr(6,8);
            if (!fresh) p.jumps.pop_back();
            else target = p.jumps.size() - 1;
            active = target; activeCompact = isCompact; columns.clear();
            p.cur = &p.jumps[target]; p.cols.clear(); p.curT = std::chrono::steady_clock::now();
            records[recordKey] = target;
            if (stamped_header(raw)) {
                stamped[timeKey] = target;
                if (identity.size() >= 14) p.cur->time = identity.substr(6, 8);
                if (!p.cur->gs("Mode").empty()) stamped[timeKey+"\nMode="+upper(p.cur->gs("Mode"))]=target;
            }
            if (isCompact) p.cur->name.clear();
            if (isCompact) for (const auto& [key, value] : shortJump.stats) p.cur->set(key, value);
            return {fresh ? Parser::NEW : Parser::NONE, target, true};
        }
        bool detail = starts_with(s, "#.") || starts_with(s, "LEFT ") || starts_with(s, "RIGHT ");
        if (!s.empty() && isdigit((unsigned char)s[0]) && !columns.empty() && s.find('.') != std::string::npos) detail = true;
        std::vector<std::pair<std::string, Val>> stats;
        if (parse_stat_line(s, stats)) {
            static const char* labels[] = {"Mode","Strafes","AvgSync","Pre","Max","JumpEdge","LandingEdge","W",
                "AvgOverlap","AvgDeadAir","AvgBadAngles","Deviation","Airpath","AvgGainEff","GainEff","AvgLoss","AvgWidth","Width"};
            for (const auto& item : stats) for (const auto* key : labels) if (item.first == key) detail = true;
        }
        bool forked=false;
        if (detail && active < p.jumps.size()) for (const auto& item:stats) {
            auto mode=upper(item.second.s);
            if(item.first!="Mode" || (mode!="VNL" && mode!="CKZ" && mode!="KZT")) continue;
            auto oldMode=upper(p.jumps[active].gs("Mode"));
            if (!oldMode.empty() && mode!=oldMode) {
                // An identical unstamped-mode header may occur in the same second
                // in two modes. Resolve by source/header/time AND actual Mode.
                if(replayCandidate && replayHeader.gs("Mode").empty()) {
                    auto found=stamped.find(activeTimeKey+"\nMode="+mode);
                    if(activeStamped && found!=stamped.end()) active=found->second;
                    else {
                        p.jumps.push_back(replayHeader);active=p.jumps.size()-1;forked=true;
                    }
                    p.cur=&p.jumps[active];p.cols.clear();columns.clear();
                    records[activeRecordKey]=active;
                    if(activeStamped)stamped[activeTimeKey]=active;
                } else { reset_stream(p); return {Parser::NONE,none,true}; }
            }
            replayCandidate=false;
            if(activeStamped)stamped[activeTimeKey+"\nMode="+mode]=active;
        }
        Jump before;
        bool bound = detail && active < p.jumps.size();
        if (bound) {
            p.cur = &p.jumps[active]; p.cols = columns;
            // File chunks/partial writes are not constrained by wall time spent
            // reading historical data. Recognized detail remains with its header.
            p.curT = std::chrono::steady_clock::now(); before = *p.cur;
        }
        if (!bound && upper(s).find("LJMARK") == std::string::npos) return {};
        auto ev = p.feed(raw, cmd);
        if (bound) columns = p.cols;
        // Parser historically requires the first metric to be numeric. Keep it
        // unchanged, but retain other available fields when Sync itself is -nan.
        if (ev == Parser::NONE && bound && !columns.empty()) {
            size_t dot = s.find('.');
            if (dot != std::string::npos && dot > 0 && s.find_first_not_of("0123456789", 0) == dot) {
                double number = pnum(s.substr(0, dot));
                auto values = split_ws(s.substr(dot + 1));
                Strafe row; bool available = false;
                if (std::isfinite(number) && number > 0 && number <= 2147483647.0) {
                    row.n = (int)number;
                    for (size_t c = 0; c < columns.size() && columns[c].find('(') == std::string::npos; ++c) {
                        double value = c < values.size() ? pnum(values[c]) : NA;
                        row.v[columns[c]] = value; available = available || std::isfinite(value);
                    }
                    if (available) { p.cur->strafes.push_back(std::move(row)); ev = Parser::UPD; }
                }
            }
        }
        if (ev != Parser::UPD || !bound) return {ev, active, false};
        Jump& j = p.jumps[active];
        if (j.strafes.size() > before.strafes.size()) {
            Strafe row = std::move(j.strafes.back()); j.strafes.pop_back();
            auto it = std::find_if(j.strafes.begin(), j.strafes.end(), [&](const Strafe& old) { return old.n == row.n; });
            if (it == j.strafes.end()) j.strafes.push_back(std::move(row));
            else for (const auto& [key, value] : row.v) if (std::isfinite(value) || !it->v.count(key)) it->v[key] = value;
        }
        if (j.keys.size() > before.keys.size()) {
            auto row = std::move(j.keys.back()); j.keys.pop_back();
            auto it = std::find_if(j.keys.begin(), j.keys.end(), [&](const auto& old) { return old.first == row.first; });
            if (it == j.keys.end()) j.keys.push_back(std::move(row)); else it->second = std::move(row.second);
        }
        return {forked ? Parser::NEW : equal_data(before, j) ? Parser::NONE : Parser::UPD, active, forked};
    }
};
