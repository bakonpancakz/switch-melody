// A file to read from, for code shared by the sysmodule (its buffered SD
// reader), the overlay (stdio) and the PC tests (stdio).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Stream {
    void* ctx;
    size_t (*read)(void* ctx, void* dst, size_t n);  // fewer than n: the end (or an error)
    int (*seek)(void* ctx, int64_t offset);          // from the start; 0 = ok
    int64_t (*tell)(void* ctx);
    int64_t size;
} Stream;

static inline size_t stream_read(Stream* s, void* dst, size_t n) { return s->read(s->ctx, dst, n); }
static inline int stream_seek(Stream* s, int64_t off) { return s->seek(s->ctx, off); }
static inline int64_t stream_tell(Stream* s) { return s->tell(s->ctx); }
static inline int stream_skip(Stream* s, int64_t n) { return s->seek(s->ctx, s->tell(s->ctx) + n); }

// A Stream over an open FILE (the size is measured here)
void stream_from_file(Stream* s, FILE* f);

#ifdef __cplusplus
}
#endif
