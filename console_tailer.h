// Bounded shared-access reader. A poll never rereads the file except for a
// constant-size continuity anchor; initial history has no total-size limit.
struct Tailer {
    std::wstring path, openedPath;
    long long pos = 0, replayUntil = -1, observedSize = 0, previousPos = 0;
    std::string buf, anchor, headAnchor;
    long long bufferStart = 0;
    bool historical = true, readingHistory = false, connected = false, streamReset = false;
    bool droppingLine = false;
    DWORD error = 0;
    ULONGLONG lastAdvance = 0;
    bool identified = false;
    BY_HANDLE_FILE_INFORMATION identity{};
    std::vector<long long> lineOffsets;
    std::vector<bool> lineHistorical;
    unsigned long long bytesRead = 0;

    bool exists() const {
        DWORD attrs = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }
    void select(std::wstring value) { *this = Tailer{}; path = std::move(value); }
    static bool same_file(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) {
        return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
               a.nFileIndexLow == b.nFileIndexLow && CompareFileTime(&a.ftCreationTime, &b.ftCreationTime) == 0;
    }
    static bool read_at(HANDLE h, long long offset, char* data, DWORD size, DWORD& got) {
        LARGE_INTEGER position{}; position.QuadPart = offset;
        return SetFilePointerEx(h, position, nullptr, FILE_BEGIN) && ReadFile(h, data, size, &got, nullptr);
    }
    std::vector<std::string> poll() {
        std::vector<std::string> lines;
        lineOffsets.clear(); lineHistorical.clear(); streamReset = false;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) { error = GetLastError(); connected = false; return lines; }
        LARGE_INTEGER size{}; BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileSizeEx(h, &size) || !GetFileInformationByHandle(h, &info)) {
            error = GetLastError(); connected = false; CloseHandle(h); return lines;
        }
        bool reset = identified && (openedPath != path || !same_file(identity, info) || size.QuadPart < pos || pos < previousPos);
        if (!reset && identified && !headAnchor.empty()) {
            std::string check(headAnchor.size(), '\0'); DWORD got = 0;
            if (!read_at(h, 0, check.data(), (DWORD)check.size(), got)) {
                error = GetLastError(); connected = false; CloseHandle(h); return lines;
            }
            reset = got != check.size() || check != headAnchor;
        }
        if (!reset && identified && pos > 0 && !anchor.empty() && size.QuadPart >= pos) {
            std::string check(anchor.size(), '\0'); DWORD got = 0;
            if (!read_at(h, pos - (long long)anchor.size(), check.data(), (DWORD)check.size(), got)) {
                error = GetLastError(); connected = false; CloseHandle(h); return lines;
            }
            reset = got != check.size() || check != anchor;
        }
        if (reset) { pos = 0; buf.clear(); anchor.clear(); headAnchor.clear(); droppingLine = false; replayUntil = -1; streamReset = true; }
        if (!identified || openedPath != path) streamReset = true;
        if (pos < 0) pos = 0; // Legacy callers now also import from byte zero.
        if (replayUntil < 0) replayUntil = size.QuadPart;
        identity = info; identified = true; openedPath = path;
        connected = true; error = 0; observedSize = size.QuadPart;
        historical = pos < replayUntil;
        if (size.QuadPart > pos) {
            DWORD wanted = (DWORD)std::min<long long>(size.QuadPart - pos, 256 * 1024);
            std::string data(wanted, '\0'); DWORD got = 0;
            if (!read_at(h, pos, data.data(), wanted, got)) {
                error = GetLastError(); connected = false; CloseHandle(h); return lines;
            }
            data.resize(got); bytesRead += got;
            if (got) lastAdvance = GetTickCount64();
            for (DWORD i = 0; i < got; ++i) {
                char c = data[i];
                if (buf.empty() && !droppingLine) bufferStart = pos + i;
                if (c == '\n') {
                    if (!droppingLine) {
                        if (!buf.empty() && buf.back() == '\r') buf.pop_back();
                        lines.push_back(std::move(buf)); buf.clear();
                        lineOffsets.push_back(bufferStart);
                        lineHistorical.push_back(bufferStart < replayUntil);
                    }
                    droppingLine = false;
                } else if (!droppingLine) {
                    buf += c;
                    if (buf.size() > 65536) { buf.clear(); droppingLine = true; }
                }
            }
            pos += got;
        }
        // Preserve a bounded anchor to detect truncate-and-regrow of the same inode.
        DWORD wanted = (DWORD)std::min<long long>(pos, 128), got = 0;
        anchor.assign(wanted, '\0');
        if (wanted && (!read_at(h, pos - wanted, anchor.data(), wanted, got) || got != wanted)) {
            anchor.clear(); error = ERROR_READ_FAULT; connected = false;
        }
        if (headAnchor.empty() && pos > 0) {
            wanted = (DWORD)std::min<long long>(pos, 128); headAnchor.assign(wanted, '\0');
            if (!read_at(h, 0, headAnchor.data(), wanted, got) || got != wanted) headAnchor.clear();
        }
        previousPos = pos;
        readingHistory = pos < replayUntil;
        CloseHandle(h);
        return lines;
    }
};
