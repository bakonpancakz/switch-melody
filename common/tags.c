#include "tags.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ogg.h"

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// Where comments come from: a FLAC block (part of the file) or an Ogg packet
typedef struct Src {
    size_t (*read)(void* ctx, void* dst, size_t n);
    void (*skip)(void* ctx, uint64_t n);
    void* ctx;
} Src;

typedef struct Block {
    Stream* s;
    uint64_t left;
} Block;

static size_t block_read(void* ctx, void* dst, size_t n) {
    Block* b = (Block*)ctx;
    if (n > b->left) n = (size_t)b->left;
    size_t m = stream_read(b->s, dst, n);
    b->left -= m;
    return m;
}

static void block_skip(void* ctx, uint64_t n) {
    Block* b = (Block*)ctx;
    if (n > b->left) n = b->left;
    stream_skip(b->s, (int64_t)n);
    b->left -= n;
}

static size_t packet_read(void* ctx, void* dst, size_t n) { return ogg_packet_read((OggReader*)ctx, dst, n); }
static void packet_skip(void* ctx, uint64_t n) { ogg_packet_skip((OggReader*)ctx, n); }

static int src_u32(Src* s, uint32_t* v) {
    uint8_t b[4];
    if (s->read(s->ctx, b, 4) != 4) return -1;
    *v = le32(b);
    return 0;
}

// The picture kept so far: its type (3: front cover), -1 for none
typedef struct PicState {
    TagsPicture* out;
    size_t max;
    int type;
} PicState;

// Consider a PICTURE block (as in FLAC; Opus's base64 decodes to the same)
static void take_picture(PicState* ps, uint8_t* blk, size_t len) {
    if (len < 32) goto drop;
    const uint8_t* p = blk;
    const uint8_t* end = blk + len;
    uint32_t type = be32(p);
    p += 4;
    uint32_t mime = be32(p);
    p += 4;
    if ((size_t)(end - p) < (size_t)mime + 4) goto drop;
    p += mime;
    uint32_t desc = be32(p);
    p += 4;
    if ((size_t)(end - p) < (size_t)desc + 20) goto drop;
    p += desc + 16;  // (width, height, depth, colors)
    uint32_t size = be32(p);
    p += 4;
    if ((size_t)(end - p) < size || size == 0) goto drop;
    if (ps->type == 3 || (ps->type >= 0 && type != 3)) goto drop;  // not better than what we have
    free(ps->out->data);
    memmove(blk, p, size);
    ps->out->data = blk;
    ps->out->size = size;
    ps->type = (int)type;
    return;
drop:
    free(blk);
}

static int b64(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// A base64 value of len characters, decoded as it's read
static void take_b64_picture(PicState* ps, Src* s, uint32_t len) {
    size_t cap = (size_t)len / 4 * 3 + 3;
    if (cap > ps->max + 4096 || ps->type == 3) {
        s->skip(s->ctx, len);
        return;
    }
    uint8_t* out = (uint8_t*)malloc(cap);
    if (!out) {
        s->skip(s->ctx, len);
        return;
    }
    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;
    char chunk[1024];
    while (len) {
        size_t k = s->read(s->ctx, chunk, len < sizeof(chunk) ? len : sizeof(chunk));
        if (!k) break;
        len -= (uint32_t)k;
        for (size_t i = 0; i < k; i++) {
            int v = b64((unsigned char)chunk[i]);
            if (v < 0) continue;
            acc = (acc << 6) | (uint32_t)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                if (n < cap) out[n++] = (uint8_t)(acc >> bits);
            }
        }
    }
    if (len) s->skip(s->ctx, len);
    take_picture(ps, out, n);
}

static void copy_value(Src* s, char* dst, uint32_t len) {
    uint32_t n = len < 255 ? len : 255;
    n = (uint32_t)s->read(s->ctx, dst, n);
    dst[n] = 0;
    if (len > n) s->skip(s->ctx, len - n);
}

// Vorbis comments: vendor, count, then "KEY=value" strings
static void read_comments(Src* s, Tags* t, PicState* ps) {
    uint32_t vendor, count;
    if (src_u32(s, &vendor)) return;
    s->skip(s->ctx, vendor);
    if (src_u32(s, &count)) return;
    char albumartist[256] = "";
    for (uint32_t c = 0; c < count && c < 100000; c++) {
        uint32_t len;
        if (src_u32(s, &len)) break;
        char key[32];
        uint32_t k = 0;
        int eq = 0;
        while (k < len) {
            char ch;
            if (s->read(s->ctx, &ch, 1) != 1) return;
            k++;
            if (ch == '=') {
                eq = 1;
                break;
            }
            if (k < sizeof(key)) key[k - 1] = (char)toupper((unsigned char)ch);
        }
        uint32_t keylen = eq ? k - 1 : k;
        key[keylen < sizeof(key) ? keylen : sizeof(key) - 1] = 0;
        uint32_t rest = len - k;
        if (!eq) continue;
        if (!strcmp(key, "TITLE") && !t->title[0])
            copy_value(s, t->title, rest);
        else if (!strcmp(key, "ARTIST") && !t->artist[0])
            copy_value(s, t->artist, rest);
        else if (!strcmp(key, "ALBUM") && !t->album[0])
            copy_value(s, t->album, rest);
        else if (!strcmp(key, "ALBUMARTIST") && !albumartist[0])
            copy_value(s, albumartist, rest);
        else if (!strcmp(key, "METADATA_BLOCK_PICTURE") && ps->out)
            take_b64_picture(ps, s, rest);
        else
            s->skip(s->ctx, rest);
    }
    if (!t->artist[0]) memcpy(t->artist, albumartist, sizeof(albumartist));
}

static int read_flac(Stream* s, Tags* t, PicState* ps) {
    for (int last = 0; !last;) {
        uint8_t h[4];
        if (stream_read(s, h, 4) != 4) return -1;
        last = h[0] & 0x80;
        int type = h[0] & 0x7F;
        uint32_t len = (h[1] << 16) | (h[2] << 8) | h[3];
        int64_t next = stream_tell(s) + len;
        if (type == 0 && len >= 18) {
            uint8_t si[18];
            if (stream_read(s, si, 18) != 18) return -1;
            t->rate = (si[10] << 12) | (si[11] << 4) | (si[12] >> 4);
            t->channels = ((si[12] >> 1) & 7) + 1;
            t->frames = ((uint64_t)(si[13] & 0x0F) << 32) | be32(si + 14);
        } else if (type == 4) {
            Block b = {s, len};
            Src src = {block_read, block_skip, &b};
            read_comments(&src, t, ps);
        } else if (type == 6 && ps->out && len <= ps->max + 4096 && ps->type != 3) {
            uint8_t* blk = (uint8_t*)malloc(len);
            if (blk && stream_read(s, blk, len) == len)
                take_picture(ps, blk, len);
            else
                free(blk);
        }
        if (stream_seek(s, next) != 0) return -1;
    }
    return t->rate ? 0 : -1;
}

static int read_opus(Stream* s, Tags* t, PicState* ps) {
    OggReader r;
    ogg_init(&r, s);
    uint8_t head[64];
    long n = ogg_packet(&r, head, sizeof(head));
    if (n < 19 || memcmp(head, "OpusHead", 8) != 0) return -1;
    t->channels = head[9];
    t->preskip = (uint16_t)(head[10] | (head[11] << 8));
    t->gain = (int16_t)(head[16] | (head[17] << 8));
    t->mapping = head[18];
    t->rate = 48000;
    t->serial = r.serial;
    if (!ogg_packet_begin(&r)) return -1;
    char magic[8];
    if (ogg_packet_read(&r, magic, 8) != 8 || memcmp(magic, "OpusTags", 8) != 0) return -1;
    Src src = {packet_read, packet_skip, &r};
    read_comments(&src, t, ps);
    ogg_packet_skip(&r, UINT64_MAX);  // (to the end of the tags, which end their page)
    t->data_start = stream_tell(s);
    int64_t g = ogg_last_granule(s, t->serial);
    t->frames = g > t->preskip ? (uint64_t)(g - t->preskip) : 0;
    return 0;
}

int tags_read(Stream* s, Tags* t, TagsPicture* pic, size_t pic_max) {
    memset(t, 0, sizeof(*t));
    PicState ps = {pic, pic_max, -1};
    if (pic) pic->data = NULL, pic->size = 0;
    if (stream_seek(s, 0) != 0) return -1;
    uint8_t m[10];
    if (stream_read(s, m, 10) != 10) return -1;
    int64_t at = 0;
    if (!memcmp(m, "ID3", 3)) {  // an ID3v2 tag in front of a FLAC file
        at = 10 + (((m[6] & 0x7F) << 21) | ((m[7] & 0x7F) << 14) | ((m[8] & 0x7F) << 7) | (m[9] & 0x7F));
        if (m[5] & 0x10) at += 10;
        if (stream_seek(s, at) != 0 || stream_read(s, m, 4) != 4) return -1;
    }
    int rc = -1;
    if (!memcmp(m, "fLaC", 4)) {
        t->format = TagsFormat_Flac;
        if (stream_seek(s, at + 4) == 0) rc = read_flac(s, t, &ps);
    } else if (!memcmp(m, "OggS", 4)) {
        t->format = TagsFormat_Opus;
        if (stream_seek(s, 0) == 0) rc = read_opus(s, t, &ps);
    }
    if (rc != 0) {
        t->format = TagsFormat_Unknown;
        if (pic) {
            free(pic->data);
            pic->data = NULL;
            pic->size = 0;
        }
    }
    return rc;
}
