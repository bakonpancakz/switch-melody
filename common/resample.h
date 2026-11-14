// Stereo sample-rate conversion to the Switch's 48 kHz: a windowed sinc
// (Kaiser), its kernel tabulated at track start. Frames are interleaved floats.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Resampler {
    double step;  // input frames per output frame
    double pos;   // the next output's time, in frames of buf
    int width;    // kernel half-width, in input frames
    int phases;   // table entries per input frame
    float cutoff;
    float* table; // h(d) for d = i / phases, 0 <= i <= width * phases
    float* buf;   // input frames (2 floats each)
    int have, cap;
    int passthrough;
} Resampler;

int rs_init(Resampler* r, uint32_t in_rate, uint32_t out_rate);  // 0 = ok
void rs_free(Resampler* r);
void rs_reset(Resampler* r);  // forget the input so far (after a seek)
// Take up to frames input frames: how many were taken (the rest when there's room again)
size_t rs_push(Resampler* r, const float* in, size_t frames);
// Up to max output frames
size_t rs_pull(Resampler* r, float* out, size_t max);
// At the end of the input: silence after it, so its last frames come out
void rs_drain(Resampler* r);

#ifdef __cplusplus
}
#endif
