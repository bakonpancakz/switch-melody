// Saved playlists: .m3u8 files in sdmc:/config/sys-melody/playlists (one path a
// line, "#" lines are comments). Written with card-root paths ("/Music/..."),
// as other players read them; read back also with "sdmc:/" paths and paths
// relative to the playlist's folder. Both a line at a time, so a playlist is
// never all in memory at once.
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The file for a playlist name ("Rainy day", or a file name ending in .m3u8 or
// .m3u, as listed): "" when the name can't be a file name
std::string playlistPath(const char* name);
// Write a playlist: next(out) puts the next path in out, false after the last
bool writePlaylist(const std::string& file, const std::function<bool(std::string& out)>& next);
// Read one: each(path) for its tracks (an sdmc:/ path), until it returns false
bool readPlaylist(const std::string& file, const std::function<bool(const std::string& path)>& each);
// An sdmc:/ path for a line of the playlist at file ("" for a comment or blank)
std::string playlistLinePath(const std::string& file, std::string line);

// (the same with vectors, for the PC tests)
bool writePlaylist(const std::string& file, const std::vector<std::string>& paths);
bool readPlaylist(const std::string& file, std::vector<std::string>& paths, size_t max);
