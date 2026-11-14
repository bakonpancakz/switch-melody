#include "track_queue.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

TrackQueue::~TrackQueue() {
    free(pool_);
    free(e_);
}

bool TrackQueue::init(size_t pool_bytes, size_t max_tracks) {
    pool_ = (char*)malloc(pool_bytes);
    e_ = (Entry*)malloc(sizeof(Entry) * max_tracks);
    if (!pool_ || !e_) {
        free(pool_);
        free(e_);
        pool_ = nullptr;
        e_ = nullptr;
        return false;
    }
    pool_cap_ = pool_bytes;
    cap_ = max_tracks;
    return true;
}

uint32_t TrackQueue::hashOf(std::string_view s) {
    uint32_t h = 2166136261u;  // (FNV-1a)
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

int TrackQueue::indexOf(uint32_t id) const {
    for (size_t i = 0; i < n_; i++)
        if (e_[i].id == id) return (int)i;
    return -1;
}

bool TrackQueue::contains(std::string_view p) const {
    uint32_t h = hashOf(p);
    for (size_t i = 0; i < n_; i++)
        if (e_[i].hash == h && path(i) == p) return true;
    return false;
}

uint32_t TrackQueue::add(std::string_view p) {
    if (!pool_ || n_ >= cap_ || p.empty() || p.size() > 0xFFFF || contains(p)) return 0;
    if (pool_used_ + p.size() > pool_cap_ && holes_) compact();
    if (pool_used_ + p.size() > pool_cap_) return 0;
    memcpy(pool_ + pool_used_, p.data(), p.size());
    e_[n_] = {next_id_++, (uint32_t)pool_used_, hashOf(p), (uint16_t)p.size()};
    pool_used_ += p.size();
    return e_[n_++].id;
}

void TrackQueue::erase(size_t i) {
    if (i >= n_) return;
    holes_ += e_[i].len;
    memmove(&e_[i], &e_[i + 1], sizeof(Entry) * (n_ - i - 1));
    n_--;
    if (n_ == 0) pool_used_ = holes_ = 0;
}

void TrackQueue::move(size_t from, size_t to) {
    if (from >= n_ || to >= n_ || from == to) return;
    Entry e = e_[from];
    if (from < to)
        memmove(&e_[from], &e_[from + 1], sizeof(Entry) * (to - from));
    else
        memmove(&e_[to + 1], &e_[to], sizeof(Entry) * (from - to));
    e_[to] = e;
}

void TrackQueue::clear() { n_ = pool_used_ = holes_ = 0; }

bool TrackQueue::full() const {
    size_t avg = n_ ? (pool_used_ - holes_) / n_ : 64;
    return n_ >= cap_ || pool_cap_ - (pool_used_ - holes_) < avg;
}

// Close the gaps: the paths slide down the pool in the order they're in it.
// (The entries are visited by their paths' places through a sort of their
// indices; the slide only ever moves a path to a lower place, so nothing
// is overwritten before it's moved.)
void TrackQueue::compact() {
    uint16_t* by_off = (uint16_t*)malloc(sizeof(uint16_t) * n_);
    if (!by_off) return;
    for (size_t i = 0; i < n_; i++) by_off[i] = (uint16_t)i;
    std::sort(by_off, by_off + n_, [this](uint16_t a, uint16_t b) { return e_[a].off < e_[b].off; });
    size_t at = 0;
    for (size_t k = 0; k < n_; k++) {
        Entry& e = e_[by_off[k]];
        if (e.off != at) memmove(pool_ + at, pool_ + e.off, e.len);
        e.off = (uint32_t)at;
        at += e.len;
    }
    free(by_off);
    pool_used_ = at;
    holes_ = 0;
}
