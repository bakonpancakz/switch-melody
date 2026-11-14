// Covers are decoded small: JPEGs at 1/2..1/8 scale by libjpeg-turbo, PNGs a
// row at a time, both box-filtered down to a square (the middle of the
// picture). The overlay has a few MB in all, so nothing full-size is kept.
#include "cover.h"

#include <png.h>
#include <setjmp.h>
#include <turbojpeg.h>
#include <dirent.h>
#include <strings.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "stream.h"
#include "tags.h"

namespace {

constexpr size_t MAX_PICTURE = 3 << 20;  // bigger embedded pictures aren't read

// Box-filters rows of RGB (3 bytes a pixel) from w x h down to n x n, from the
// middle square of the picture
class Shrink {
public:
    Shrink(int w, int h, int n, std::vector<uint8_t>& out) : w_(w), h_(h), n_(n), out_(out) {
        side_ = w < h ? w : h;
        x0_ = (w - side_) / 2;
        y0_ = (h - side_) / 2;
        acc_.assign((size_t)n * 3, 0);
        cnt_.assign((size_t)n, 0);
        out_.assign((size_t)n * n * 2, 0);
        written_.assign((size_t)n, false);
    }

    void row(int y, const uint8_t* rgb) {
        if (y < y0_ || y >= y0_ + side_) return;
        int oy = (int)((int64_t)(y - y0_) * n_ / side_);
        if (oy != cur_) flush(), cur_ = oy;
        for (int x = 0; x < side_; x++) {
            int ox = (int)((int64_t)x * n_ / side_);
            const uint8_t* p = rgb + 3 * (x0_ + x);
            acc_[3 * ox] += p[0];
            acc_[3 * ox + 1] += p[1];
            acc_[3 * ox + 2] += p[2];
            cnt_[ox]++;
        }
    }

    // (when the picture is smaller than n: rows nothing landed in copy the one above)
    void finish() {
        flush();
        for (int oy = 1; oy < n_; oy++)
            if (!written_[oy]) memcpy(&out_[(size_t)oy * n_ * 2], &out_[(size_t)(oy - 1) * n_ * 2], (size_t)n_ * 2);
    }

private:
    void flush() {
        if (cur_ < 0) return;
        for (int ox = 0; ox < n_; ox++) {
            if (!cnt_[ox] && ox > 0) {  // (likewise columns)
                memcpy(&out_[((size_t)cur_ * n_ + ox) * 2], &out_[((size_t)cur_ * n_ + ox - 1) * 2], 2);
                continue;
            }
            uint32_t c = cnt_[ox] ? cnt_[ox] : 1;
            uint8_t r = (uint8_t)(acc_[3 * ox] / c), g = (uint8_t)(acc_[3 * ox + 1] / c), b = (uint8_t)(acc_[3 * ox + 2] / c);
            uint8_t* o = &out_[((size_t)cur_ * n_ + ox) * 2];
            o[0] = (uint8_t)((r & 0xF0) | (g >> 4));
            o[1] = (uint8_t)((b & 0xF0) | 0x0F);
        }
        std::fill(acc_.begin(), acc_.end(), 0);
        std::fill(cnt_.begin(), cnt_.end(), 0);
        written_[cur_] = true;
        cur_ = -1;
    }

    int w_, h_, n_, side_, x0_, y0_;
    int cur_ = -1;
    std::vector<uint32_t> acc_, cnt_;
    std::vector<bool> written_;
    std::vector<uint8_t>& out_;
};

bool decodeJpeg(const uint8_t* data, size_t size, int n, std::vector<uint8_t>& out) {
    tjhandle tj = tjInitDecompress();
    if (!tj) return false;
    int w = 0, h = 0, sub = 0, cs = 0;
    bool ok = false;
    if (tjDecompressHeader3(tj, data, (unsigned long)size, &w, &h, &sub, &cs) == 0 && w > 0 && h > 0) {
        // the smallest scale that still gives at least n pixels across the square
        int count = 0;
        tjscalingfactor* f = tjGetScalingFactors(&count);
        tjscalingfactor best = {1, 1};
        int side = w < h ? w : h;
        for (int i = 0; i < count; i++) {
            if (f[i].num > f[i].denom) continue;
            if (TJSCALED(side, f[i]) >= n && f[i].num * best.denom < best.num * f[i].denom) best = f[i];
        }
        int sw = TJSCALED(w, best), sh = TJSCALED(h, best);
        uint8_t* rgb = (uint8_t*)malloc((size_t)sw * sh * 3);
        if (rgb && tjDecompress2(tj, data, (unsigned long)size, rgb, sw, sw * 3, sh, TJPF_RGB, TJFLAG_FASTDCT) == 0) {
            Shrink s(sw, sh, n, out);
            for (int y = 0; y < sh; y++) s.row(y, rgb + (size_t)y * sw * 3);
            s.finish();
            ok = true;
        }
        free(rgb);
    }
    tjDestroy(tj);
    return ok;
}

struct MemRead {
    const uint8_t* p;
    size_t left;
};

void pngRead(png_structp png, png_bytep dst, png_size_t n) {
    MemRead* m = (MemRead*)png_get_io_ptr(png);
    if (n > m->left) png_error(png, "short");
    memcpy(dst, m->p, n);
    m->p += n;
    m->left -= n;
}

// (only plain data across setjmp: libpng's errors longjmp back here)
bool decodePng(const uint8_t* data, size_t size, int n, std::vector<uint8_t>& out) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    uint8_t* volatile row = nullptr;
    Shrink* volatile shrink = nullptr;
    volatile bool ok = false;
    MemRead m = {data, size};
    if (info && !setjmp(png_jmpbuf(png))) {
        png_set_read_fn(png, &m, pngRead);
        png_read_info(png, info);
        png_uint_32 w = png_get_image_width(png, info), h = png_get_image_height(png, info);
        if (png_get_interlace_type(png, info) == PNG_INTERLACE_NONE && w && h && w <= 8192 && h <= 8192) {
            png_set_expand(png);
            png_set_strip_16(png);
            png_set_strip_alpha(png);
            png_set_gray_to_rgb(png);
            png_read_update_info(png, info);
            if (png_get_rowbytes(png, info) == (size_t)w * 3) {
                row = (uint8_t*)malloc((size_t)w * 3);
                shrink = new Shrink((int)w, (int)h, n, out);
                for (png_uint_32 y = 0; y < h; y++) {
                    png_read_row(png, row, nullptr);
                    shrink->row((int)y, row);
                }
                shrink->finish();
                ok = true;
            }
        }
    }
    delete shrink;
    free(row);
    png_destroy_read_struct(&png, info ? &info : nullptr, nullptr);
    return ok;
}

bool decodeImage(const uint8_t* data, size_t size, int n, std::vector<uint8_t>& out) {
    if (size > 3 && data[0] == 0xFF && data[1] == 0xD8) return decodeJpeg(data, size, n, out);
    if (size > 8 && !memcmp(data, "\x89PNG", 4)) return decodePng(data, size, n, out);
    return false;
}

bool decodeFile(const std::string& path, int n, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = false;
    if (size > 0 && (size_t)size <= MAX_PICTURE) {
        uint8_t* data = (uint8_t*)malloc((size_t)size);
        if (data && fread(data, 1, (size_t)size, f) == (size_t)size) ok = decodeImage(data, (size_t)size, n, out);
        free(data);
    }
    fclose(f);
    return ok;
}

// How likely a file is the album's cover: 0 best; -1 not a picture. Names
// saying cover / folder / front / album first, then any picture, then the
// ones that sound like another part of the case.
int imageRank(const char* name) {
    const char* dot = strrchr(name, '.');
    if (!dot || (strcasecmp(dot, ".jpg") && strcasecmp(dot, ".jpeg") && strcasecmp(dot, ".png"))) return -1;
    std::string n(name, dot);
    for (char& c : n) c = (char)tolower((unsigned char)c);
    auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    if (has("back") || has("disc") || has("cd") || has("inlay") || has("inside") || has("booklet")) return 3;
    if (n == "cover" || n == "folder" || n == "front") return 0;
    if (has("cover") || has("folder") || has("front") || has("album")) return 1;
    return 2;
}

}  // namespace

bool coverLoad(const char* track_path, int size, std::vector<uint8_t>& pixels) {
    if (FILE* f = fopen(track_path, "rb")) {
        static char buf[32 << 10];
        setvbuf(f, buf, _IOFBF, sizeof(buf));
        Stream s;
        stream_from_file(&s, f);
        Tags t;
        TagsPicture pic = {};
        tags_read(&s, &t, &pic, MAX_PICTURE);
        fclose(f);
        bool ok = pic.data && decodeImage(pic.data, pic.size, size, pixels);
        free(pic.data);
        if (ok) return true;
    }
    std::string dir = track_path;
    size_t slash = dir.find_last_of('/');
    if (slash == std::string::npos) return false;
    dir.resize(slash + 1);
    // any picture in the track's folder, the likeliest covers first
    std::vector<std::pair<int, std::string>> found;
    if (DIR* d = opendir(dir.c_str())) {
        while (struct dirent* e = readdir(d)) {
            if (e->d_type == DT_DIR || e->d_name[0] == '.') continue;
            int rank = imageRank(e->d_name);
            if (rank >= 0) found.emplace_back(rank, e->d_name);
            if (found.size() >= 32) break;
        }
        closedir(d);
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : strcasecmp(a.second.c_str(), b.second.c_str()) < 0;
    });
    for (size_t i = 0; i < found.size() && i < 4; i++)  // (a few tries: one that won't decode isn't the end)
        if (decodeFile(dir + found[i].second, size, pixels)) return true;
    return false;
}
