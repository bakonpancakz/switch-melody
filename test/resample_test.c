// PC check of common/resample.c: a sine converted to 48 kHz against the ideal
// sine at 48 kHz (error in dB below the signal), in small uneven pieces
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/resample.h"

static void run(unsigned in_rate, double freq) {
    Resampler r;
    if (rs_init(&r, in_rate, 48000)) { printf("init failed\n"); return; }
    const int N = in_rate * 2;
    float* in = malloc(sizeof(float) * 2 * N);
    for (int i = 0; i < N; i++) in[2 * i] = in[2 * i + 1] = 0.5f * (float)sin(2 * M_PI * freq * i / in_rate);
    float* out = malloc(sizeof(float) * 2 * 200000);
    size_t got = 0, fed = 0;
    while (fed < (size_t)N) {
        size_t piece = 1 + (fed * 7919) % 900;
        if (piece > N - fed) piece = N - fed;
        fed += rs_push(&r, in + 2 * fed, piece);
        got += rs_pull(&r, out + 2 * got, 200000 - got);
    }
    double err = 0, sig = 0;
    size_t from = 4800, to = got - 4800;  // away from the edges
    for (size_t i = from; i < to; i++) {
        double ideal = 0.5 * sin(2 * M_PI * freq * i / 48000.0);
        err += (out[2 * i] - ideal) * (out[2 * i] - ideal);
        sig += ideal * ideal;
    }
    printf("%6u Hz -> 48000, %5.0f Hz sine: %zu frames out (expect ~%d), error %.1f dB\n", in_rate, freq, got,
           (int)((double)N * 48000 / in_rate), 10 * log10(err / sig));
    rs_free(&r);
    free(in);
    free(out);
}

int main(void) {
    run(44100, 1000);
    run(44100, 15000);
    run(96000, 1000);
    run(88200, 10000);
    run(32000, 440);
    run(192000, 5000);
}
