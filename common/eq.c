#include "eq.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { PEAK, LOW_SHELF, HIGH_SHELF, LOW_PASS, HIGH_PASS, NOTCH, BAND_PASS, ALL_PASS };

void eq_off(Eq* eq) {
    memset(eq, 0, sizeof(*eq));
    eq->preamp = 1.0f;
}

void eq_reset(Eq* eq) {
    for (int i = 0; i < eq->count; i++) {
        memset(eq->f[i].z1, 0, sizeof(eq->f[i].z1));
        memset(eq->f[i].z2, 0, sizeof(eq->f[i].z2));
    }
}

// The cookbook's coefficients (Robert Bristow-Johnson's), divided through by a0
static int make(EqBiquad* q, int type, double fc, double gain_db, double Q, double rate) {
    if (fc <= 0 || fc >= rate / 2 || Q <= 0) return 0;
    double A = pow(10.0, gain_db / 40.0), w = 2 * M_PI * fc / rate, cs = cos(w), sn = sin(w);
    double al = sn / (2 * Q), sa = 2 * sqrt(A) * al;
    double b0, b1, b2, a0, a1, a2;
    switch (type) {
        case PEAK:
            b0 = 1 + al * A, b1 = -2 * cs, b2 = 1 - al * A, a0 = 1 + al / A, a1 = -2 * cs, a2 = 1 - al / A;
            break;
        case LOW_SHELF:
            b0 = A * ((A + 1) - (A - 1) * cs + sa), b1 = 2 * A * ((A - 1) - (A + 1) * cs), b2 = A * ((A + 1) - (A - 1) * cs - sa);
            a0 = (A + 1) + (A - 1) * cs + sa, a1 = -2 * ((A - 1) + (A + 1) * cs), a2 = (A + 1) + (A - 1) * cs - sa;
            break;
        case HIGH_SHELF:
            b0 = A * ((A + 1) + (A - 1) * cs + sa), b1 = -2 * A * ((A - 1) + (A + 1) * cs), b2 = A * ((A + 1) + (A - 1) * cs - sa);
            a0 = (A + 1) - (A - 1) * cs + sa, a1 = 2 * ((A - 1) - (A + 1) * cs), a2 = (A + 1) - (A - 1) * cs - sa;
            break;
        case LOW_PASS:
            b0 = (1 - cs) / 2, b1 = 1 - cs, b2 = (1 - cs) / 2, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
            break;
        case HIGH_PASS:
            b0 = (1 + cs) / 2, b1 = -(1 + cs), b2 = (1 + cs) / 2, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
            break;
        case NOTCH:
            b0 = 1, b1 = -2 * cs, b2 = 1, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
            break;
        case BAND_PASS:
            b0 = al, b1 = 0, b2 = -al, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
            break;
        default:  // ALL_PASS
            b0 = 1 - al, b1 = -2 * cs, b2 = 1 + al, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
            break;
    }
    memset(q, 0, sizeof(*q));
    q->b0 = (float)(b0 / a0), q->b1 = (float)(b1 / a0), q->b2 = (float)(b2 / a0);
    q->a1 = (float)(a1 / a0), q->a2 = (float)(a2 / a0);
    return 1;
}

static int filterType(const char* t) {
    static const struct { const char* name; int type; } types[] = {
        {"pk", PEAK},        {"peq", PEAK},        {"modal", PEAK},     {"ls", LOW_SHELF}, {"lsc", LOW_SHELF},
        {"lsq", LOW_SHELF},  {"hs", HIGH_SHELF},   {"hsc", HIGH_SHELF}, {"hsq", HIGH_SHELF}, {"lp", LOW_PASS},
        {"lpq", LOW_PASS},   {"hp", HIGH_PASS},    {"hpq", HIGH_PASS},  {"no", NOTCH},     {"bp", BAND_PASS},
        {"ap", ALL_PASS},
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        if (!strcmp(t, types[i].name)) return types[i].type;
    return -1;
}

// One line, lower-cased and cut into words (at spaces, tabs, colons and commas)
static int words(char* line, char* w[], int max) {
    int n = 0;
    for (char* p = line; *p && n < max;) {
        while (*p && strchr(" \t:,", *p)) *p++ = 0;
        if (!*p) break;
        w[n++] = p;
        while (*p && !strchr(" \t:,", *p)) *p = (char)tolower((unsigned char)*p), p++;
    }
    return n;
}

static void parseLine(Eq* eq, char* line, double rate, double* preamp_db) {
    char* w[32];
    int n = words(line, w, 32);
    if (n == 0 || w[0][0] == '#') return;
    if (!strcmp(w[0], "preamp")) {
        if (n >= 2) *preamp_db += atof(w[1]);
        return;
    }
    if (strcmp(w[0], "filter")) {  // (Channel, Device, Include, GraphicEQ, ...)
        eq->skipped++;
        return;
    }
    int i = 1;
    if (i < n && isdigit((unsigned char)w[i][0])) i++;  // ("Filter 3:")
    if (i < n && !strcmp(w[i], "off")) return;
    if (i < n && !strcmp(w[i], "on")) i++;
    int type = i < n ? filterType(w[i++]) : -1;
    if (type < 0) {
        eq->skipped++;
        return;
    }
    double fc = 0, gain = 0, Q = type == PEAK ? 1.0 : 0.7071, bw = 0;
    for (; i < n; i++) {
        if (!strcmp(w[i], "fc") && i + 1 < n) fc = atof(w[++i]);
        else if (!strcmp(w[i], "gain") && i + 1 < n) gain = atof(w[++i]);
        else if (!strcmp(w[i], "q") && i + 1 < n) Q = atof(w[++i]);
        else if (!strcmp(w[i], "bw") && i + 2 < n && !strcmp(w[i + 1], "oct")) bw = atof(w[i + 2]), i += 2;
    }
    if (bw > 0) Q = 1.0 / (2 * sinh(log(2.0) / 2 * bw));  // (a bandwidth in octaves, as a Q)
    if (eq->count >= EQ_MAX_FILTERS || !make(&eq->f[eq->count], type, fc, gain, Q, rate)) {
        eq->skipped++;
        return;
    }
    eq->count++;
}

int eq_parse(Eq* eq, const char* text, double rate) {
    eq_off(eq);
    double preamp_db = 0;
    char line[512];
    while (*text) {
        size_t len = strcspn(text, "\r\n");
        size_t k = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, text, k);
        line[k] = 0;
        parseLine(eq, line, rate, &preamp_db);
        text += len;
        while (*text == '\r' || *text == '\n') text++;
    }
    eq->preamp = (float)pow(10.0, preamp_db / 20.0);
    return eq->count;
}

void eq_process(Eq* eq, float* lr, size_t frames) {
    if (eq->count == 0 && eq->preamp == 1.0f) return;
    for (int i = 0; i < eq->count; i++) {
        EqBiquad* q = &eq->f[i];
        for (int c = 0; c < 2; c++) {
            float z1 = q->z1[c], z2 = q->z2[c];
            for (size_t k = 0; k < frames; k++) {  // (transposed direct form II)
                float x = lr[2 * k + c], y = q->b0 * x + z1;
                z1 = q->b1 * x - q->a1 * y + z2;
                z2 = q->b2 * x - q->a2 * y;
                lr[2 * k + c] = y;
            }
            q->z1[c] = z1, q->z2[c] = z2;
        }
    }
    if (eq->preamp != 1.0f)
        for (size_t k = 0; k < 2 * frames; k++) lr[k] *= eq->preamp;
}

double eq_response_db(const Eq* eq, double freq, double rate) {
    double w = 2 * M_PI * freq / rate, db = 20 * log10(eq->preamp);
    for (int i = 0; i < eq->count; i++) {
        const EqBiquad* q = &eq->f[i];
        // |b0 + b1 e^-jw + b2 e^-2jw| / |1 + a1 e^-jw + a2 e^-2jw|
        double nr = q->b0 + q->b1 * cos(w) + q->b2 * cos(2 * w), ni = -(q->b1 * sin(w) + q->b2 * sin(2 * w));
        double dr = 1 + q->a1 * cos(w) + q->a2 * cos(2 * w), di = -(q->a1 * sin(w) + q->a2 * sin(2 * w));
        db += 10 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    return db;
}
