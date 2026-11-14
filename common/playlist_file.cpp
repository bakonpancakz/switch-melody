#include "playlist_file.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "melody.h"

static bool endsWith(const std::string& s, const char* tail) {
    size_t n = strlen(tail);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)s[s.size() - n + i]) != tail[i]) return false;
    return true;
}

std::string playlistPath(const char* name) {
    std::string n = name;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    while (!n.empty() && n[0] == ' ') n.erase(0, 1);
    if (n.empty() || n.size() > 80 || n[0] == '.') return "";
    for (unsigned char c : n)
        if (c < 0x20 || strchr("/\\:*?\"<>|", c)) return "";
    if (!endsWith(n, ".m3u8") && !endsWith(n, ".m3u")) n += ".m3u8";
    return std::string(MELODY_PLAYLIST_DIR) + "/" + n;
}

bool writePlaylist(const std::string& file, const std::function<bool(std::string& out)>& next) {
    FILE* f = fopen(file.c_str(), "wb");
    if (!f) return false;
    bool ok = fputs("#EXTM3U\n", f) >= 0;
    std::string p;
    while (ok && next(p)) {
        const char* s = p.c_str();
        if (!strncmp(s, "sdmc:", 5)) s += 5;  // (the card's root is "/" to other players)
        ok = fputs(s, f) >= 0 && fputc('\n', f) != EOF;
    }
    return fclose(f) == 0 && ok;
}

bool writePlaylist(const std::string& file, const std::vector<std::string>& paths) {
    size_t i = 0;
    return writePlaylist(file, [&](std::string& out) {
        if (i >= paths.size()) return false;
        out = paths[i++];
        return true;
    });
}

std::string playlistLinePath(const std::string& file, std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
        line.erase(0, 3);  // (a UTF-8 byte order mark)
    if (line.empty() || line[0] == '#') return "";
    for (char& c : line)
        if (c == '\\') c = '/';
    std::string p;
    if (!strncmp(line.c_str(), "sdmc:/", 6))
        p = line.substr(5);
    else if (line[0] == '/')
        p = line;
    else  // relative to the playlist's folder
        p = file.substr(5, file.find_last_of('/') - 4) + line;
    // . and .. away
    std::vector<std::string> parts;
    size_t at = 1;
    while (at <= p.size()) {
        size_t end = p.find('/', at);
        if (end == std::string::npos) end = p.size();
        std::string part = p.substr(at, end - at);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        at = end + 1;
    }
    std::string out = "sdmc:";
    for (const std::string& part : parts) out += "/" + part;
    return parts.empty() ? "" : out;
}

bool readPlaylist(const std::string& file, const std::function<bool(const std::string& path)>& each) {
    FILE* f = fopen(file.c_str(), "rb");
    if (!f) return false;
    char line[MELODY_PATH_MAX + 2];
    while (fgets(line, sizeof(line), f)) {
        std::string p = playlistLinePath(file, line);
        if (!p.empty() && p.size() < MELODY_PATH_MAX && !each(p)) break;
    }
    fclose(f);
    return true;
}

bool readPlaylist(const std::string& file, std::vector<std::string>& paths, size_t max) {
    return readPlaylist(file, [&](const std::string& p) {
        paths.push_back(p);
        return paths.size() < max;
    });
}
