// The SD card, open only while it's used: an idle sysmodule holding a file
// system connection can keep an app from starting. Reads are big (one refill
// of the buffer at a time, a few a second for FLAC), as the card serves one
// request at a time and the app in front needs it too.
#pragma once
#include <switch.h>

#include "stream.h"

namespace card {
bool acquire();  // open the card (counted); false: it can't be used now
void release();
}  // namespace card

// While one is around the card stays open
struct Card {
    bool ok;
    Card() : ok(card::acquire()) {}
    ~Card() {
        if (ok) card::release();
    }
    Card(const Card&) = delete;
    Card& operator=(const Card&) = delete;
};

class SdFile {
public:
    explicit SdFile(size_t buffer_size) : cap_(buffer_size) {}
    ~SdFile() { close(); }
    bool open(const char* path);  // an sdmc:/ path
    void close();
    bool isOpen() const { return open_; }
    Result openError() const { return rc_; }  // why the last open() failed (0: the card itself, or ok)
    Stream* stream() { return &stream_; }

private:
    static size_t sread(void* ctx, void* dst, size_t n);
    static int sseek(void* ctx, int64_t off);
    static int64_t stell(void* ctx);

    FsFile f_ = {};
    bool open_ = false;
    Result rc_ = 0;
    bool card_ = false;
    size_t cap_;
    u8* buf_ = nullptr;
    s64 buf_off_ = 0;   // where buf_ starts in the file
    size_t buf_len_ = 0;
    s64 pos_ = 0;
    Stream stream_ = {};
};
