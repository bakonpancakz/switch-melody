// PC check of common/eq.c: each kind of filter does what its line says (its
// response where it should show), a config as AutoEq writes them, and the
// filters run over a sine come out at the gain the response says
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/eq.h"

static int bad = 0;
static void near(double got, double want, double tol, const char* what) {
    int ok = fabs(got - want) <= tol;
    if (!ok) bad++;
    printf("  %-44s %7.2f dB (want %6.2f) %s\n", what, got, want, ok ? "ok" : "WRONG");
}

static void one(const char* line, double f, double want, double tol, const char* what) {
    Eq eq;
    if (eq_parse(&eq, line, 48000) != 1) {
        printf("  %s: not parsed\n", what);
        bad++;
        return;
    }
    near(eq_response_db(&eq, f, 48000), want, tol, what);
}

int main(void) {
    one("Filter 1: ON PK Fc 1000 Hz Gain 6.0 dB Q 1.41", 1000, 6, 0.01, "peak +6 dB at 1 kHz, at 1 kHz");
    one("Filter 1: ON PK Fc 1000 Hz Gain -4.5 dB Q 2.0", 1000, -4.5, 0.01, "peak -4.5 dB at 1 kHz, at 1 kHz");
    one("Filter 1: ON PK Fc 1000 Hz Gain 6.0 dB Q 1.41", 10000, 0, 0.3, "peak +6 dB at 1 kHz, at 10 kHz");
    one("Filter 2: ON LSC Fc 105 Hz Gain 5.0 dB Q 0.71", 20, 5, 0.3, "low shelf +5 dB at 105 Hz, at 20 Hz");
    one("Filter 2: ON LSC Fc 105 Hz Gain 5.0 dB Q 0.71", 5000, 0, 0.1, "low shelf +5 dB at 105 Hz, at 5 kHz");
    one("Filter 3: ON HSC Fc 8000 Hz Gain -3.0 dB Q 0.71", 20000, -3, 0.3, "high shelf -3 dB at 8 kHz, at 20 kHz");
    one("Filter: ON HP Fc 30 Hz", 10, -19, 1.5, "high pass at 30 Hz, at 10 Hz");
    one("Filter: ON HP Fc 30 Hz", 1000, 0, 0.1, "high pass at 30 Hz, at 1 kHz");
    one("Filter: ON LP Fc 16000 Hz", 1000, 0, 0.1, "low pass at 16 kHz, at 1 kHz");
    one("Filter: ON NO Fc 50 Hz Q 30", 50, -60, 40, "notch at 50 Hz, at 50 Hz (deep)");
    one("Filter 1: ON PK Fc 1000 Hz Gain 6.0 dB BW Oct 1.0", 1000, 6, 0.01, "peak with BW Oct, at 1 kHz");

    // as AutoEq writes them (a preamp, peaks and shelves, and a line it can't use)
    const char* autoeq =
        "Preamp: -6.4 dB\r\n"
        "Filter 1: ON LSC Fc 105 Hz Gain 6.2 dB Q 0.70\r\n"
        "Filter 2: ON PK Fc 2700 Hz Gain -3.1 dB Q 1.83\r\n"
        "Filter 3: OFF PK Fc 5000 Hz Gain 9.0 dB Q 2.00\r\n"
        "Filter 4: ON HSC Fc 10000 Hz Gain 2.4 dB Q 0.70\r\n"
        "GraphicEQ: 20 -1.0; 100 0.5\r\n"
        "# a comment\r\n";
    Eq eq;
    int n = eq_parse(&eq, autoeq, 48000);
    printf("AutoEq config: %d filters, %d lines skipped, preamp %.2f dB\n", n, eq.skipped, 20 * log10(eq.preamp));
    if (n != 3 || eq.skipped != 1) bad++;
    near(eq_response_db(&eq, 1000, 48000), -6.4, 0.6, "config at 1 kHz (about the preamp)");
    near(eq_response_db(&eq, 20, 48000), -6.4 + 6.2, 0.5, "config at 20 Hz (preamp + low shelf)");

    // a sine through the filters: its level out should be the response
    double f = 2700, rate = 48000;
    static float lr[2 * 48000];
    for (int i = 0; i < 48000; i++) lr[2 * i] = lr[2 * i + 1] = (float)(0.25 * sin(2 * M_PI * f * i / rate));
    eq_reset(&eq);
    for (int at = 0; at < 48000; at += 1000) eq_process(&eq, lr + 2 * at, 1000);  // (in pieces, as it's used)
    double peak = 0;
    for (int i = 24000; i < 48000; i++) peak = fmax(peak, fabs(lr[2 * i]));
    near(20 * log10(peak / 0.25), eq_response_db(&eq, f, rate), 0.05, "a 2.7 kHz sine through it");
    printf(bad ? "eq: FAILED %d\n" : "eq: all ok\n", bad);
    return bad != 0;
}
