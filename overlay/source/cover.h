// The cover of a track, as a size x size picture for the overlay to draw:
// the one in the file, or a picture in its folder (cover.jpg and the like first).
#pragma once
#include <cstdint>
#include <vector>

// pixels: 2 bytes each, as tsl's drawBitmapRGBA4444 takes them (r<<4|g, b<<4|a).
// false when there's no cover it can read.
bool coverLoad(const char* track_path, int size, std::vector<uint8_t>& pixels);
