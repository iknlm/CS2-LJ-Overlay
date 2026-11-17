static int draw_console_text(Painter& p, int x, int y, int width, const std::string& value, COLORREF color) {
    std::wstring row;
    for (wchar_t ch : W(value)) {
        if (ch == L'\n' || (!row.empty() && p.width(g.fFoot, U8(row + ch)) > width)) {
            p.text(x, y, g.fFoot, color, U8(row)); y += p.lh(g.fFoot) + g.S(2); row.clear();
        }
        if (ch != L'\n') row += ch;
    }
    if (!row.empty()) { p.text(x, y, g.fFoot, color, U8(row)); y += p.lh(g.fFoot) + g.S(2); }
    return y;
}
