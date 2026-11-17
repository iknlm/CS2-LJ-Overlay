static void on_log_timer() {
    bool changed = false;
    if (g.autoLog && GetTickCount64() >= g.nextLogSearch && (!g.tail.readingHistory || !g.tail.connected || g.tail.error)) {
        g.nextLogSearch = GetTickCount64() + 5000;
        std::wstring found = find_console_log();
        if (!found.empty() && found != g.tail.path) {
            g.tail.select(found); g.importer.reset_stream(g.parser); g.awardPending = false;
        }
    }
    auto lines = g.tail.poll();
    if (g.tail.streamReset) {
        if (g.importer.active < g.jumps.size()) finalize_analysis(g.importer.active);
        g.importer.reset_stream(g.parser); g.awardPending = false;
    }
    std::string source = console_source_key();
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string cmd;
        size_t previous = g.importer.active;
        auto event = g.importer.feed(g.parser, lines[i], source, g.tail.lineOffsets[i], cmd);
        if (event.boundary && previous < g.jumps.size() && previous != event.index) finalize_analysis(previous);
        bool historical = g.tail.lineHistorical[i];
        if (event.ev == Parser::NEW) {
            if (!g.jumps[event.index].attemptId) g.jumps[event.index].attemptId = g.nextAttemptId++;
            if (previous == ConsoleImport::none && g.jumps.size() > 1) finalize_analysis(g.jumps.size() - 2);
            g.analyses.resize(g.jumps.size());
            g.awardPending = !historical;
        } else if (event.boundary) g.awardPending = false; // A replay is not a new award.
        if (event.ev == Parser::UPD && event.index < g.analyses.size()) {
            g.analyses[event.index].clear();
            g.pairCacheA = g.pairCacheB = 0;
        }
        if ((event.ev == Parser::NEW || event.ev == Parser::UPD) && g.awardPending &&
            event.index < g.jumps.size() && g.jumps[event.index].rank() != R_UNKNOWN) {
            sound_rank(g.jumps[event.index].rank()); g.awardPending = false;
        }
        if (event.ev == Parser::NEW || event.ev == Parser::UPD) changed = true;
        // Historical console commands must never mark/quit/hide the current session.
        else if (event.ev == Parser::CMD && !historical) command(cmd);
    }
    size_t active = g.importer.active < g.jumps.size() ? g.importer.active : g.jumps.empty() ? ConsoleImport::none : g.jumps.size() - 1;
    if (!g.tail.readingHistory && active < g.jumps.size() && active < g.analyses.size() && g.analyses[active].empty() &&
        std::chrono::steady_clock::now() - g.parser.curT > std::chrono::seconds(3)) {
        finalize_analysis(active); changed = true;
    }
    std::string status, state = console_state();
    if (state == "LOG ERROR") {
        status = "LOG ERROR: console.log недоступен" + (g.tail.error ? sfmt(" (Windows %lu)", (unsigned long)g.tail.error) : std::string("")) +
            ".\nОжидаемый файл: " + (g.tail.path.empty() ? "не найден: задайте путь в настройках" : U8(g.tail.path)) +
            "\nSteam → CS2 → Свойства → Параметры запуска: -condebug; перезапустите CS2.\nПуть: INS → 9 Сессия → 10 Источник console.log.";
    } else if (!g.tail.readingHistory && g.jumps.empty()) {
        status = g.tail.observedSize == 0 ? "console.log пуст: проверьте -condebug и сделайте прыжок в CS2." :
            "История прочитана: поддерживаемых прыжков нет (с учётом фильтра player). Проверьте источник и -condebug.";
        status += "\nФайл: " + U8(g.tail.path) + "\nНастройка: INS → 9 → 10 Источник console.log.";
    } else if (!g.tail.buf.empty()) status = "Ожидается завершение последней строки console.log.";
    else if (g.tail.lastAdvance && GetTickCount64() - g.tail.lastAdvance > 30000)
        status = "Новых строк console.log нет более 30 с. Это может быть пауза; если прыжки не приходят, проверьте -condebug и путь (INS → 9 → 10).";
    if (changed) save_csv();
    if (changed || status != g.statusLog || state != g.logState) {
        g.statusLog = status; g.logState = state; relayout();
    }
}
