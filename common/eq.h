// An equalizer from an Equalizer APO config (AutoEq writes them, as do
// others): its Preamp and Filter lines, as biquads (the RBJ "cookbook" ones)
// run over stereo floats. Other lines (Channel, Include, GraphicEQ, ...) are
// skipped, and counted, so the overlay can say a config wasn't all used.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EQ_MAX_FILTERS 32

typedef struct EqBiquad {
    float b0, b1, b2, a1, a2;
    float z1[2], z2[2];  // (the state, a pair for each channel)
} EqBiquad;

typedef struct Eq {
    int count;           // filters in use (0 and preamp 1: the sound as it is)
    float preamp;        // as a factor
    int skipped;         // lines that were neither (and not comments or blank)
    EqBiquad f[EQ_MAX_FILTERS];
} Eq;

// An equalizer from a config's text, for a rate (48000 here): the filters it
// could make (0 with no filters: just its preamp, maybe)
int eq_parse(Eq* eq, const char* text, double rate);
void eq_off(Eq* eq);
void eq_reset(Eq* eq);  // (forget the filters' past: after a seek, say)
void eq_process(Eq* eq, float* lr, size_t frames);
// How much it changes a frequency, in dB (for the PC tests)
double eq_response_db(const Eq* eq, double freq, double rate);

#ifdef __cplusplus
}
#endif
