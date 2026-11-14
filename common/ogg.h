// A small Ogg reader: one logical stream, read packet by packet without
// holding whole packets in memory (an Opus file's tags packet can carry a
// cover of a few MB). Pages of other streams are skipped.
#pragma once
#include "stream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OggReader {
    Stream* s;
    uint32_t serial;
    int have_serial;    // 0: follow the first stream seen
    uint8_t lace[255];  // the current page's segment table
    int nlace, lacei;   // its length, the next segment to start
    int seg_left;       // bytes left in the current segment
    int seg_last;       // the current segment ends the packet
    int in_packet;
    int64_t granule;     // the current page's
    uint8_t flags;       // the current page's header type (1: continued, 2: first, 4: last)
    int64_t page_start;  // where the current page starts
    // the packet being read ends on a page with this granule; ends that page (the
    // granule counts up to its end); that page is the stream's last
    int64_t end_granule;
    int end_of_page, end_of_stream;
} OggReader;

void ogg_init(OggReader* r, Stream* s);
// Load the next page of the stream at the current file position (1), or the end (0)
int ogg_next_page(OggReader* r);
// Start the next packet (skipping what's left of the current one): 1, or 0 at the end.
// A packet continued from a page before (after a seek) is skipped.
int ogg_packet_begin(OggReader* r);
// Up to n bytes more of the current packet (0: it's over)
size_t ogg_packet_read(OggReader* r, void* dst, size_t n);
// Skip n bytes of the current packet, without reading them where it can
void ogg_packet_skip(OggReader* r, uint64_t n);
// A whole packet into buf: its length, or -1 at the end. Longer than cap: -2 (skipped)
long ogg_packet(OggReader* r, void* buf, size_t cap);
// Find the first page of the stream starting at or after offset and load it:
// 1, or 0 when there's none before the end
int ogg_seek_page(OggReader* r, int64_t offset);
// The granule of the stream's last page (from the file's end), or -1
int64_t ogg_last_granule(Stream* s, uint32_t serial);
// Where the current page ends
int64_t ogg_page_end(const OggReader* r);
// Go to the end of the last page (of the stream's pages from data_start) whose
// granule is no more than target, by bisection: reading goes on from there.
// Its granule (0 for data_start itself).
int64_t ogg_seek_granule(OggReader* r, int64_t data_start, int64_t target);

#ifdef __cplusplus
}
#endif
