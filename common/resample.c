#include "resample.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BASE_WIDTH 16  // half-width when upsampling; wider by the ratio when downsampling
#define PHASES 256
#define BLOCK 1024     // input frames held besides the kernel's reach (pushes are at most this)

static double bessel_i0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 32; k++) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
        if (term < sum * 1e-12) break;
    }
    return sum;
}

int rs_init(Resampler* r, uint32_t in_rate, uint32_t out_rate) {
    memset(r, 0, sizeof(*r));
    if (in_rate == out_rate) {
        r->passthrough = 1;
        return 0;
    }
    if (!in_rate || in_rate > out_rate * 4) return -1;
    r->step = (double)in_rate / out_rate;
    double ratio = r->step > 1 ? r->step : 1;
    r->width = (int)ceil(BASE_WIDTH * ratio);
    r->phases = PHASES;
    r->cutoff = (float)(0.95 / ratio);  // of the input's Nyquist
    int n = r->width * r->phases + 1;
    r->table = (float*)malloc(sizeof(float) * (n + 1));
    r->cap = BLOCK + 2 * r->width + 2;
    r->buf = (float*)malloc(sizeof(float) * 2 * r->cap);
    if (!r->table || !r->buf) {
        rs_free(r);
        return -1;
    }
    const double beta = 8.0, i0b = bessel_i0(beta);
    for (int i = 0; i < n; i++) {
        double d = (double)i / r->phases;
        double x = M_PI * r->cutoff * d;
        double sinc = d == 0 ? 1 : sin(x) / x;
        double t = d / r->width;
        double win = t >= 1 ? 0 : bessel_i0(beta * sqrt(1 - t * t)) / i0b;
        r->table[i] = (float)(r->cutoff * sinc * win);
    }
    r->table[n] = 0;
    rs_reset(r);
    return 0;
}

void rs_free(Resampler* r) {
    free(r->table);
    free(r->buf);
    r->table = r->buf = NULL;
}

void rs_reset(Resampler* r) {
    if (r->passthrough) return;
    // silence before the first frame, so its first output is at time 0
    r->have = r->width;
    memset(r->buf, 0, sizeof(float) * 2 * r->have);
    r->pos = r->width;
}

size_t rs_push(Resampler* r, const float* in, size_t frames) {
    // drop what the kernel can't reach any more
    int keep_from = (int)r->pos - r->width;
    if (keep_from > 0) {
        memmove(r->buf, r->buf + 2 * keep_from, sizeof(float) * 2 * (r->have - keep_from));
        r->have -= keep_from;
        r->pos -= keep_from;
    }
    size_t room = (size_t)(r->cap - r->have);
    if (frames > room) frames = room;
    memcpy(r->buf + 2 * r->have, in, sizeof(float) * 2 * frames);
    r->have += (int)frames;
    return frames;
}

static inline float tap(const Resampler* r, double d) {
    double a = fabs(d) * r->phases;
    int i = (int)a;
    float f = (float)(a - i);
    return r->table[i] + (r->table[i + 1] - r->table[i]) * f;
}

size_t rs_pull(Resampler* r, float* out, size_t max) {
    size_t n = 0;
    while (n < max) {
        int c = (int)floor(r->pos);
        if (c + r->width >= r->have) break;
        double frac = r->pos - c;
        float L = 0, R = 0;
        for (int k = -r->width + 1; k <= r->width; k++) {
            float h = tap(r, k - frac);
            const float* s = r->buf + 2 * (c + k);
            L += s[0] * h;
            R += s[1] * h;
        }
        out[2 * n] = L;
        out[2 * n + 1] = R;
        n++;
        r->pos += r->step;
    }
    return n;
}

void rs_drain(Resampler* r) {
    float zero[2 * 64] = {0};
    for (int left = r->width + 1; left > 0;) {
        size_t k = rs_push(r, zero, left < 64 ? (size_t)left : 64);
        if (!k) break;
        left -= (int)k;
    }
}
