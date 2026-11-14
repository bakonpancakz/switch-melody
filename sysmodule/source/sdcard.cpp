#include "sdcard.h"

#include <cstdlib>
#include <cstring>

namespace card {

static Mutex g_lock;
static int g_users = 0;

bool acquire() {
    mutexLock(&g_lock);
    bool ok = true;
    if (g_users == 0) {
        ok = R_SUCCEEDED(fsInitialize());
        if (ok && R_FAILED(fsdevMountSdmc())) {
            fsExit();
            ok = false;
        }
    }
    if (ok) g_users++;
    mutexUnlock(&g_lock);
    return ok;
}

void release() {
    mutexLock(&g_lock);
    if (g_users > 0 && --g_users == 0) {
        fsdevUnmountDevice("sdmc");
        fsExit();
    }
    mutexUnlock(&g_lock);
}

}  // namespace card

bool SdFile::open(const char* path) {
    close();
    rc_ = 0;
    if (strncmp(path, "sdmc:", 5) != 0) return false;
    if (!card::acquire()) return false;
    card_ = true;
    char p[FS_MAX_PATH] = {};
    strncpy(p, path + 5, sizeof(p) - 1);
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    s64 size = 0;
    if (!fs || R_FAILED(rc_ = fsFsOpenFile(fs, p, FsOpenMode_Read, &f_))) {
        close();
        return false;
    }
    open_ = true;
    if (R_FAILED(fsFileGetSize(&f_, &size)) || !(buf_ = (u8*)malloc(cap_))) {
        close();
        return false;
    }
    buf_off_ = 0;
    buf_len_ = 0;
    pos_ = 0;
    stream_.ctx = this;
    stream_.read = sread;
    stream_.seek = sseek;
    stream_.tell = stell;
    stream_.size = size;
    return true;
}

void SdFile::close() {
    if (open_) fsFileClose(&f_);
    open_ = false;
    free(buf_);
    buf_ = nullptr;
    if (card_) card::release();
    card_ = false;
}

size_t SdFile::sread(void* ctx, void* dst, size_t n) {
    SdFile* f = (SdFile*)ctx;
    u8* d = (u8*)dst;
    size_t got = 0;
    while (got < n && f->pos_ < f->stream_.size) {
        if (f->pos_ < f->buf_off_ || f->pos_ >= f->buf_off_ + (s64)f->buf_len_) {
            u64 r = 0;
            if (R_FAILED(fsFileRead(&f->f_, f->pos_, f->buf_, f->cap_, FsReadOption_None, &r)) || r == 0) break;
            f->buf_off_ = f->pos_;
            f->buf_len_ = r;
        }
        size_t at = (size_t)(f->pos_ - f->buf_off_);
        size_t k = f->buf_len_ - at;
        if (k > n - got) k = n - got;
        memcpy(d + got, f->buf_ + at, k);
        got += k;
        f->pos_ += k;
    }
    return got;
}

int SdFile::sseek(void* ctx, int64_t off) {
    SdFile* f = (SdFile*)ctx;
    if (off < 0 || off > f->stream_.size) return -1;
    f->pos_ = off;
    return 0;
}

int64_t SdFile::stell(void* ctx) { return ((SdFile*)ctx)->pos_; }
