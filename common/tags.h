// What's in a FLAC or Ogg Opus file besides the sound: its format, length,
// Title / Artist / Album (Vorbis comments, used by both) and, when asked
// for, the cover picture (FLAC's PICTURE block, Opus's METADATA_BLOCK_PICTURE).
#pragma once
#include "stream.h"

#ifdef __cplusplus
extern "C" {
#endif

enum TagsFormat { TagsFormat_Unknown = 0, TagsFormat_Flac = 1, TagsFormat_Opus = 2 };

typedef struct Tags {
    int format;
    uint32_t rate;      // FLAC: the file's; Opus: always 48000
    uint32_t channels;
    uint64_t frames;    // the length in frames at that rate (0: unknown)
    // Opus only
    uint32_t serial;
    uint16_t preskip;
    int16_t gain;       // Q7.8 dB
    uint8_t mapping;
    int64_t data_start;  // where the first audio page starts
    char title[256], artist[256], album[256];
} Tags;

typedef struct TagsPicture {
    uint8_t* data;  // malloc'd (free it), the image file's bytes: JPEG or PNG
    size_t size;
} TagsPicture;

// Read s from its start. pic: NULL for no picture; else the front cover
// (or the first picture) when it's no bigger than pic_max. 0 = ok.
int tags_read(Stream* s, Tags* t, TagsPicture* pic, size_t pic_max);

#ifdef __cplusplus
}
#endif
