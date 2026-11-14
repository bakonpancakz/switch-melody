// PC check of common/tags.c and common/ogg.c: prints what a file says and
// writes its cover to <file>.cover
#include <stdio.h>
#include <stdlib.h>
#include "../common/tags.h"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        FILE* f = fopen(argv[i], "rb");
        if (!f) { printf("%s: can't open\n", argv[i]); continue; }
        Stream s;
        stream_from_file(&s, f);
        Tags t;
        TagsPicture pic;
        int rc = tags_read(&s, &t, &pic, 4 << 20);
        printf("%s: rc=%d format=%d rate=%u ch=%u frames=%llu (%.3f s) preskip=%u data_start=%lld\n  title='%s' artist='%s' album='%s' picture=%zu bytes\n",
               argv[i], rc, t.format, t.rate, t.channels, (unsigned long long)t.frames, t.rate ? (double)t.frames / t.rate : 0.0,
               t.preskip, (long long)t.data_start, t.title, t.artist, t.album, pic.size);
        if (pic.data) {
            char out[512];
            snprintf(out, sizeof(out), "%s.cover", argv[i]);
            FILE* o = fopen(out, "wb");
            fwrite(pic.data, 1, pic.size, o);
            fclose(o);
            free(pic.data);
        }
        Tags t2;
        tags_read(&s, &t2, NULL, 0);
        printf("  without picture: title='%s' frames=%llu\n", t2.title, (unsigned long long)t2.frames);
        fclose(f);
    }
}
