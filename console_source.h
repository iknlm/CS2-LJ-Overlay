static std::wstring console_path(std::wstring path) {
    while (!path.empty() && iswspace(path.front())) path.erase(path.begin());
    while (!path.empty() && iswspace(path.back())) path.pop_back();
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') path = path.substr(1, path.size() - 2);
    if (path.empty()) return {};
    wchar_t expanded[32768];
    DWORD n = ExpandEnvironmentStringsW(path.c_str(), expanded, 32768);
    if (n && n < 32768) path = expanded;
    if (!(path.size() >= 2 && path[1] == L':') && !starts_with(U8(path), "\\\\")) path = g.exeDir + L"\\" + path;
    wchar_t absolute[32768]; n = GetFullPathNameW(path.c_str(), 32768, absolute, nullptr);
    return n && n < 32768 ? std::wstring(absolute) : path;
}
static std::string console_source_key() {
    std::wstring path = console_path(g.tail.path);
    for (auto& c : path) c = (wchar_t)towlower(c);
    return U8(path);
}
static std::wstring parent_path(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}
static std::wstring running_cs2_log() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof pe;
    std::wstring result;
    if (Process32FirstW(snap, &pe)) do {
        if (_wcsicmp(pe.szExeFile, L"cs2.exe") != 0) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!process) continue;
        wchar_t exe[32768]; DWORD count = 32768;
        if (QueryFullProcessImageNameW(process, 0, exe, &count)) {
            std::wstring game = parent_path(parent_path(parent_path(exe)));
            if (is_dir(game + L"\\csgo")) result = game + L"\\csgo\\console.log";
        }
        CloseHandle(process);
        if (!result.empty()) break;
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return result;
}
static std::wstring find_console_log() {
    // The explicit INI path is handled before this function and always wins.
    std::wstring running = running_cs2_log();
    if (!running.empty()) return running;
    std::vector<std::wstring> libraries;
    for (auto spec : {std::make_pair(HKEY_CURRENT_USER, L"Software\\Valve\\Steam"),
                      std::make_pair(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam")}) {
        wchar_t root[32768]; DWORD size = sizeof root;
        const wchar_t* value = spec.first == HKEY_CURRENT_USER ? L"SteamPath" : L"InstallPath";
        if (RegGetValueW(spec.first, spec.second, value, RRF_RT_REG_SZ, nullptr, root, &size) != ERROR_SUCCESS) continue;
        libraries.push_back(root);
        std::string vdf;
        if (!read_file(std::wstring(root) + L"\\steamapps\\libraryfolders.vdf", vdf)) continue;
        static const std::regex paths("\"path\"\\s+\"([^\"]+)\"");
        for (std::sregex_iterator it(vdf.begin(), vdf.end(), paths), end; it != end; ++it) {
            std::string p = (*it)[1]; size_t k;
            while ((k = p.find("\\\\")) != std::string::npos) p.erase(k, 1);
            libraries.push_back(W(p));
        }
    }
    std::wstring newest, expected;
    FILETIME last{};
    for (const auto& library : libraries) {
        std::string manifest, install = "Counter-Strike Global Offensive";
        if (read_file(library + L"\\steamapps\\appmanifest_730.acf", manifest)) {
            std::smatch m; static const std::regex dir("\"installdir\"\\s+\"([^\"]+)\"");
            if (std::regex_search(manifest, m, dir)) install = m[1];
        }
        std::wstring dir = library + L"\\steamapps\\common\\" + W(install) + L"\\game\\csgo";
        if (!is_dir(dir)) continue;
        std::wstring file = dir + L"\\console.log";
        if (expected.empty()) expected = file;
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            (newest.empty() || CompareFileTime(&info.ftLastWriteTime, &last) > 0)) { newest = file; last = info.ftLastWriteTime; }
    }
    return newest.empty() ? expected : newest;
}
static std::string console_state() {
    if (g.tail.path.empty() || g.tail.error || !g.tail.exists()) return "LOG ERROR";
    if (!g.tail.connected) return "LOG CONNECTED";
    return g.tail.readingHistory ? "READING HISTORY" : "LIVE";
}
static std::wstring clipboard_log_path() {
    std::wstring value;
    if (OpenClipboard(g.hwnd)) {
        HANDLE mem = GetClipboardData(CF_UNICODETEXT);
        if (mem) {
            auto text = (const wchar_t*)GlobalLock(mem);
            if (text) { value.assign(text, wcsnlen(text, 2047)); GlobalUnlock(mem); }
        }
        CloseClipboard();
    }
    return value;
}
static void set_console_source(std::wstring path, bool automatic) {
    if (path.find_first_of(L"\r\n") != std::wstring::npos || path.size() > 2047) {
        g.logEditError = "Введите один путь длиной до 2047 символов."; return;
    }
    path = automatic ? find_console_log() : console_path(path);
    if (!automatic && path.empty()) { g.logEditError = "Путь пуст. Для поиска используйте Автопоиск."; return; }
    g.autoLog = automatic; g.tail.select(path); g.importer.reset_stream(g.parser);
    g.awardPending = false; g.logEditing = false; g.logEditError.clear();
    ini_set(L"overlay", L"log", automatic ? L"" : path);
}
