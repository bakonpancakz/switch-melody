#include "ogg.h"

#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static int64_t le64(const uint8_t* p) { return (int64_t)((uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32)); }

void ogg_init(OggReader* r, Stream* s) {
    memset(r, 0, sizeof(*r));
    r->s = s;
    r->granule = -1;
    r->end_granule = -1;
}

int ogg_next_page(OggReader* r) {
    for (;;) {
        uint8_t h[27];
        int64_t at = stream_tell(r->s);
        if (stream_read(r->s, h, 27) != 27) return 0;
        if (memcmp(h, "OggS", 4) != 0 || h[4] != 0) return 0;
        int n = h[26];
        uint8_t lace[255];
        if (stream_read(r->s, lace, n) != (size_t)n) return 0;
        uint32_t serial = le32(h + 14);
        if (r->have_serial && serial != r->serial) {
            int64_t body = 0;
            for (int i = 0; i < n; i++) body += lace[i];
            if (stream_skip(r->s, body) != 0) return 0;
            continue;
        }
        r->serial = serial;
        r->have_serial = 1;
        memcpy(r->lace, lace, n);
        r->nlace = n;
        r->lacei = 0;
        r->seg_left = 0;
        r->flags = h[5];
        r->granule = le64(h + 6);
        r->page_start = at;
        return 1;
    }
}

// Start the next segment of the current page
static void start_segment(OggReader* r) {
    int lace = r->lace[r->lacei++];
    r->seg_left = lace;
    r->seg_last = lace < 255;
    if (r->seg_last) {
        r->end_granule = r->granule;
        r->end_of_page = r->lacei == r->nlace;
        r->end_of_stream = r->end_of_page && (r->flags & 4);
    }
}

// Move to the packet's next segment, across pages; 0: the packet is over
static int next_segment(OggReader* r) {
    if (r->seg_last) return 0;
    if (r->lacei >= r->nlace) {
        if (!ogg_next_page(r)) return 0;
        if (!(r->flags & 1)) {  // a new packet, not the rest of this one (a broken file)
            r->seg_last = 1;
            return 0;
        }
        if (r->nlace == 0) return next_segment(r);
    }
    start_segment(r);
    return 1;
}

static void finish_packet(OggReader* r) {
    while (r->in_packet) {
        if (r->seg_left) {
            stream_skip(r->s, r->seg_left);
            r->seg_left = 0;
        }
        if (!next_segment(r)) r->in_packet = 0;
    }
}

int ogg_packet_begin(OggReader* r) {
    finish_packet(r);
    for (;;) {
        if (r->lacei >= r->nlace) {
            if (!ogg_next_page(r)) return 0;
            if (r->flags & 1) {  // the rest of a packet from the page before: skip it
                while (r->lacei < r->nlace) {
                    int lace = r->lace[r->lacei++];
                    stream_skip(r->s, lace);
                    if (lace < 255) break;
                }
                continue;
            }
            if (r->nlace == 0) continue;
        }
        start_segment(r);
        r->in_packet = 1;
        return 1;
    }
}

size_t ogg_packet_read(OggReader* r, void* dst, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    size_t got = 0;
    while (got < n && r->in_packet) {
        if (r->seg_left == 0) {
            if (!next_segment(r)) {
                r->in_packet = 0;
                break;
            }
            continue;
        }
        size_t k = n - got < (size_t)r->seg_left ? n - got : (size_t)r->seg_left;
        size_t m = stream_read(r->s, d + got, k);
        got += m;
        r->seg_left -= (int)m;
        if (m < k) {  // the file ended
            r->in_packet = 0;
            r->seg_left = 0;
            break;
        }
    }
    return got;
}

void ogg_packet_skip(OggReader* r, uint64_t n) {
    while (n && r->in_packet) {
        if (r->seg_left == 0) {
            if (!next_segment(r)) r->in_packet = 0;
            continue;
        }
        uint64_t k = n < (uint64_t)r->seg_left ? n : (uint64_t)r->seg_left;
        stream_skip(r->s, (int64_t)k);
        r->seg_left -= (int)k;
        n -= k;
    }
}

long ogg_packet(OggReader* r, void* buf, size_t cap) {
    if (!ogg_packet_begin(r)) return -1;
    size_t n = ogg_packet_read(r, buf, cap);
    if (n == cap && r->in_packet) {
        uint8_t probe;
        if (ogg_packet_read(r, &probe, 1)) {
            finish_packet(r);
            return -2;
        }
    }
    if (r->in_packet && r->seg_left == 0 && r->seg_last) r->in_packet = 0;
    return (long)n;
}

int ogg_seek_page(OggReader* r, int64_t offset) {
    uint8_t buf[4096];
    int64_t pos = offset;
    r->in_packet = 0;
    r->seg_left = 0;
    r->nlace = r->lacei = 0;
    for (;;) {
        if (stream_seek(r->s, pos) != 0) return 0;
        size_t n = stream_read(r->s, buf, sizeof(buf));
        if (n < 27) return 0;
        size_t i = 0;
        for (; i + 27 <= n; i++) {
            if (buf[i] != 'O' || memcmp(buf + i, "OggS", 4) != 0 || buf[i + 4] != 0) continue;
            if (r->have_serial && le32(buf + i + 14) != r->serial) continue;
            if (stream_seek(r->s, pos + (int64_t)i) != 0) return 0;
            return ogg_next_page(r);
        }
        pos += (int64_t)i;  // (a header cut off at the end is read again from its start)
    }
}

int64_t ogg_page_end(const OggReader* r) {
    int64_t body = 0;
    for (int i = 0; i < r->nlace; i++) body += r->lace[i];
    return r->page_start + 27 + r->nlace + body;
}

// The next page from offset that ends some packet (granule != -1): 1, or 0
static int seek_timed_page(OggReader* r, int64_t offset) {
    if (!ogg_seek_page(r, offset)) return 0;
    while (r->granule == -1) {
        if (stream_seek(r->s, ogg_page_end(r)) != 0 || !ogg_next_page(r)) return 0;
    }
    return 1;
}

int64_t ogg_seek_granule(OggReader* r, int64_t data_start, int64_t target) {
    int64_t best_off = data_start, best = 0;
    int64_t lo = data_start, hi = r->s->size;
    while (hi - lo > (64 << 10)) {
        int64_t mid = lo + (hi - lo) / 2;
        if (!seek_timed_page(r, mid) || r->page_start >= hi) {
            hi = mid;
            continue;
        }
        if (r->granule <= target) {
            best = r->granule;
            best_off = lo = ogg_page_end(r);
        } else {
            hi = mid;
        }
    }
    // the last stretch page by page (headers only)
    if (stream_seek(r->s, best_off) == 0) {
        while (ogg_next_page(r)) {
            if (r->granule != -1) {
                if (r->granule > target) break;
                best = r->granule;
                best_off = ogg_page_end(r);
            }
            if (stream_seek(r->s, ogg_page_end(r)) != 0) break;
        }
    }
    r->in_packet = 0;
    r->seg_left = 0;
    r->nlace = r->lacei = 0;
    stream_seek(r->s, best_off);
    return best;
}

// Backwards from the end, 16 KB at a time (overlapping by a header's length,
// so one cut in two is still seen), at most 1 MB back
int64_t ogg_last_granule(Stream* s, uint32_t serial) {
    enum { CHUNK = 16 << 10, LIMIT = 1 << 20, HEADER = 27 };
    uint8_t* buf = (uint8_t*)malloc(CHUNK);
    if (!buf) return -1;
    int64_t found = -1, end = s->size;
    while (end > 0 && s->size - end < LIMIT && found < 0) {
        int64_t from = end > CHUNK ? end - CHUNK : 0;
        size_t len = (size_t)(end - from);
        if (stream_seek(s, from) != 0 || stream_read(s, buf, len) != len) break;
        for (int64_t i = (int64_t)len - HEADER; i >= 0; i--) {
            if (buf[i] != 'O' || memcmp(buf + i, "OggS", 4) != 0 || buf[i + 4] != 0) continue;
            if (le32(buf + i + 14) != serial) continue;
            int64_t g = le64(buf + i + 6);
            if (g == -1) continue;
            found = g;
            break;
        }
        if (from == 0) break;
        end = from + HEADER;  // (the next chunk overlaps this one's start)
    }
    free(buf);
    return found;
}
