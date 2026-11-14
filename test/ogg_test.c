// PC check of common/ogg.c on Ogg Opus files: packets' sample counts (from
// their TOC bytes) must add up to every page's granule, and seeking must land
// on a page at or just before the target.
#include <stdio.h>
#include <stdlib.h>
#include "../common/ogg.h"
#include "../common/tags.h"

static int samples(const unsigned char* p, long len) {
    if (len < 1) return 0;
    int cfg = p[0] >> 3, per;
    if (cfg < 12) per = (int[]){480, 960, 1920, 2880}[cfg & 3];
    else if (cfg < 16) per = (cfg & 1) ? 960 : 480;
    else per = (int[]){120, 240, 480, 960}[cfg & 3];
    int c = p[0] & 3, n = c == 0 ? 1 : c < 3 ? 2 : (len > 1 ? (p[1] & 0x3F) : 0);
    return per * n;
}

int main(int argc, char** argv) {
    int bad = 0;
    for (int a = 1; a < argc; a++) {
        FILE* f = fopen(argv[a], "rb");
        Stream s;
        stream_from_file(&s, f);
        Tags t;
        tags_read(&s, &t, NULL, 0);
        OggReader r;
        ogg_init(&r, &s);
        r.serial = t.serial;
        r.have_serial = 1;
        stream_seek(&s, t.data_start);
        static unsigned char pkt[65536];
        long long pos = 0;
        int pages = 0, packets = 0;
        long len;
        while ((len = ogg_packet(&r, pkt, sizeof(pkt))) >= 0) {
            packets++;
            pos += samples(pkt, len);
            if (r.end_of_page) {
                pages++;
                if (pos != r.end_granule && !r.end_of_stream) { printf("  page %d: counted %lld, granule %lld\n", pages, pos, (long long)r.end_granule); bad++; }
            }
        }
        printf("%s: %d packets, %d pages, %lld samples counted, last granule %lld\n", argv[a], packets, pages, pos, (long long)r.end_granule);
        long long targets[] = {0, 1000, 48000, 300000, (long long)t.frames / 2, (long long)t.frames - 100, (long long)t.frames + 50000};
        for (int i = 0; i < 7; i++) {
            long long g = ogg_seek_granule(&r, t.data_start, targets[i]);
            long long at = stream_tell(&s);
            // what's the next page's granule?
            ogg_next_page(&r);
            long long next = r.granule;
            int ok = g <= targets[i] && (next > targets[i] || next == -1 || targets[i] > (long long)t.frames);
            if (!ok) bad++;
            printf("  seek %8lld -> page granule %8lld at %7lld (next page %8lld) %s\n", targets[i], g, at, next, ok ? "ok" : "WRONG");
        }
        fclose(f);
    }
    printf(bad ? "FAILED %d\n" : "all ok\n", bad);
    return bad != 0;
}
