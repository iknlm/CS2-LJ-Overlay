static int draw_review_lines(Painter& p, int x, int y, int width, const std::vector<std::string>& lines,
                             int& scroll, int& lineCount, RECT& body, int visible = 6) {
    std::vector<std::string> wrapped;
    for (const auto& line : lines) {
        std::string row;
        for (const auto& word : split_ws(line)) {
            std::string next = row.empty() ? word : row + " " + word;
            if (!row.empty() && p.width(g.fFoot, next) > width) { wrapped.push_back(row); row = word; }
            else row = next;
        }
        wrapped.push_back(row);
    }
    lineCount = (int)wrapped.size();
    scroll = std::clamp(scroll, 0, std::max(0, lineCount - visible));
    int top = y, lh = p.lh(g.fFoot) + g.S(3);
    for (int i = scroll; i < std::min(lineCount, scroll + visible); ++i) {
        p.text(x, y, g.fFoot, clr::sub, wrapped[i]); y += lh;
    }
    body = RECT{x, top, x + width, y};
    if (lineCount > visible) {
        p.text(x, y, g.fFoot, clr::accent, sfmt("%d–%d / %d · колесо / PgUp/PgDn", scroll + 1, std::min(lineCount, scroll + visible), lineCount));
        y += lh;
    }
    return y;
}
