// A track being decoded: stereo float frames at the file's own rate
#pragma once
#include <cstddef>
#include <cstdint>

#include "stream.h"
#include "tags.h"

class Decoder {
public:
    virtual ~Decoder() {}
    // Up to max frames (2 floats each) into out: 0 at the end (or on an error)
    virtual size_t read(float* out, size_t max) = 0;
    // Go to a frame (at the file's rate): false if it can't
    virtual bool seek(uint64_t frame) = 0;
    uint32_t rate = 0;
};

// For what tags_read found in s (nullptr: not a stream it can play)
Decoder* decoderOpen(Stream* s, const Tags& t);
