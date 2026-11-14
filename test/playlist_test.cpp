// PC check of common/playlist_file.cpp: names, and paths written and read
// back the ways .m3u8 files hold them
#include <cstdio>
#include <string>
#include "../common/playlist_file.h"

static int bad = 0;
static void eq(const std::string& got, const std::string& want, const char* what) {
    if (got != want) { printf("FAILED %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str()); bad++; }
}

int main() {
    eq(playlistPath("Rainy day"), "sdmc:/config/sys-melody/playlists/Rainy day.m3u8", "name");
    eq(playlistPath("  Mix 2  "), "sdmc:/config/sys-melody/playlists/Mix 2.m3u8", "trimmed");
    eq(playlistPath("old.m3u"), "sdmc:/config/sys-melody/playlists/old.m3u", ".m3u kept");
    eq(playlistPath("a/b"), "", "slash");
    eq(playlistPath("why?"), "", "question mark");
    eq(playlistPath(""), "", "empty");
    eq(playlistPath(".hidden"), "", "dot first");
    const std::string pl = "sdmc:/config/sys-melody/playlists/x.m3u8";
    eq(playlistLinePath(pl, "/Music/A/01.flac\r\n"), "sdmc:/Music/A/01.flac", "root path");
    eq(playlistLinePath(pl, "sdmc:/Music/A/01.flac"), "sdmc:/Music/A/01.flac", "sdmc path");
    eq(playlistLinePath(pl, "\\Music\\B\\02.opus"), "sdmc:/Music/B/02.opus", "backslashes");
    eq(playlistLinePath(pl, "../../../Music/C/03.flac"), "sdmc:/Music/C/03.flac", "relative");
    eq(playlistLinePath(pl, "local.flac"), "sdmc:/config/sys-melody/playlists/local.flac", "next to it");
    eq(playlistLinePath(pl, "#EXTINF:123,Title"), "", "comment");
    eq(playlistLinePath(pl, "   "), "", "blank");
    eq(playlistLinePath(pl, "\xEF\xBB\xBF/Music/D.flac"), "sdmc:/Music/D.flac", "BOM");
    // write and read back (in a local file)
    std::vector<std::string> paths = {"sdmc:/Music/A/01 - One.flac", "sdmc:/Music/B/Two.opus"}, back;
    if (!writePlaylist("media/rt.m3u8", paths) || !readPlaylist("media/rt.m3u8", back, 100)) { printf("FAILED file io\n"); bad++; }
    // (read relative to "media/": the lines are root paths, so that doesn't matter)
    if (back != paths) { printf("FAILED round trip (%zu read)\n", back.size()); bad++; }
    FILE* f = fopen("media/rt.m3u8", "rb");
    char first[64] = "";
    if (f) { fgets(first, sizeof(first), f); fgets(first, sizeof(first), f); fclose(f); }
    eq(first, "/Music/A/01 - One.flac\n", "written as root paths");
    printf(bad ? "playlists: FAILED %d\n" : "playlists: all ok\n", bad);
    return bad != 0;
}
