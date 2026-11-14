#include "stream.h"

static size_t file_read(void* ctx, void* dst, size_t n) { return fread(dst, 1, n, (FILE*)ctx); }
static int file_seek(void* ctx, int64_t off) { return fseeko((FILE*)ctx, (off_t)off, SEEK_SET) == 0 ? 0 : -1; }
static int64_t file_tell(void* ctx) { return (int64_t)ftello((FILE*)ctx); }

void stream_from_file(Stream* s, FILE* f) {
    s->ctx = f;
    s->read = file_read;
    s->seek = file_seek;
    s->tell = file_tell;
    fseeko(f, 0, SEEK_END);
    s->size = (int64_t)ftello(f);
    fseeko(f, 0, SEEK_SET);
}
