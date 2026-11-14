#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// A CD, COVER pixels across (in the 2 bytes a pixel tsl's drawBitmapRGBA4444 takes), for a track with no cover: made once, pixel by
// pixel. Silver, with the rainbow a CD shows in two opposite wedges (its
// colours going round from the middle outwards), a bright band inside the
// data, the clear hub with its ring, and the hole. Edges are smoothed, and
// the colours dithered so 4 bits a channel don't show as steps.
inline const std::vector<uint8_t>& cdPicture(int COVER) {
    static std::vector<uint8_t> px;
    if (!px.empty()) return px;
    px.resize((size_t)COVER * COVER * 2);
    static const float bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const float c = COVER / 2.0f, R = c - 2.0f;
    auto clamp01 = [](float v) { return v < 0 ? 0.0f : v > 1 ? 1.0f : v; };
    for (int y = 0; y < COVER; y++) {
        for (int x = 0; x < COVER; x++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c;
            float d = std::sqrt(dx * dx + dy * dy), a = std::atan2(dy, dx);
            float cr, cg, cb, alpha = clamp01(R - d + 0.5f) * clamp01(d - 6.5f);  // (outside and the hole: none)
            if (d < 17.0f) {  // the hub: clear plastic, a ring in it
                bool ring = d > 13.0f && d < 15.5f;
                cr = cg = ring ? 0.80f : 0.62f, cb = ring ? 0.84f : 0.66f;
                alpha *= ring ? 0.85f : 0.45f;
            } else {
                float t = (d - 17.0f) / (R - 17.0f);  // 0 inside .. 1 at the rim
                cr = 0.78f, cg = 0.79f, cb = 0.82f;   // silver
                if (d < 19.5f) cr = cg = cb = 0.86f;  // the mirror band inside the data
                // the rainbow: strongest along one diagonal, both ways
                float wedge = std::pow(std::max(0.0f, std::cos(2.0f * (a + 0.75f))), 3.0f);
                float h = std::fmod(t * 1.6f + 0.15f, 1.0f) * 6.0f;
                float rr = clamp01(std::fabs(h - 3.0f) - 1.0f), gg = clamp01(2.0f - std::fabs(h - 2.0f)),
                      bb = clamp01(2.0f - std::fabs(h - 4.0f));
                float k = wedge * 0.45f;
                cr = cr * (1 - k) + rr * k, cg = cg * (1 - k) + gg * k, cb = cb * (1 - k) + bb * k;
                float shade = 0.9f + 0.1f * std::cos(a - 2.3f);  // (light from the top left)
                if (d > R - 1.5f) shade *= 0.8f;                 // (the rim)
                cr *= shade, cg *= shade, cb *= shade;
            }
            float dith = (bayer[y & 3][x & 3] + 0.5f) / 16.0f - 0.5f;
            auto q = [&](float v) { return (uint8_t)std::min(15.0f, std::max(0.0f, v * 15.0f + dith + 0.5f)); };
            uint8_t* o = &px[((size_t)y * COVER + x) * 2];
            o[0] = (uint8_t)(q(cr) << 4 | q(cg));
            o[1] = (uint8_t)(q(cb) << 4 | q(alpha));
        }
    }
    return px;
}
