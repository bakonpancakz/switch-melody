// Renders overlay/source/cd_picture.h on the overlay's black, as the Switch
// would (4-bit colour), to a PPM: 3x enlarged beside actual size
#include <cstdio>
#include "../overlay/source/cd_picture.h"

int main(int argc, char** argv) {
    const int N = 112, S = 3, W = N * S + 20 + N, H = N * S;
    const std::vector<uint8_t>& px = cdPicture(N);
    FILE* f = fopen(argc > 1 ? argv[1] : "cd.ppm", "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int sx, sy;
            if (x < N * S) sx = x / S, sy = y / S;
            else if (x >= N * S + 20 && y < N) sx = x - N * S - 20, sy = y;
            else { fputc(16, f); fputc(16, f); fputc(16, f); continue; }
            const uint8_t* p = &px[(sy * N + sx) * 2];
            float r = (p[0] >> 4) / 15.f, g = (p[0] & 15) / 15.f, b = (p[1] >> 4) / 15.f, a = (p[1] & 15) / 15.f;
            float bg = 16 / 255.f;  // (the overlay: black, a little see-through)
            fputc((int)((r * a + bg * (1 - a)) * 255), f);
            fputc((int)((g * a + bg * (1 - a)) * 255), f);
            fputc((int)((b * a + bg * (1 - a)) * 255), f);
        }
    fclose(f);
}
