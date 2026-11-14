// The sysmodule's queue, kept small: every path in one pool of text, and a
// 16-byte entry a track (its id, where its path is, how long, a hash to find
// it again quickly). Both are allocated once, at their full size, so adding
// tracks never asks for memory (in a sysmodule, running out of it is fatal).
// A removed track leaves a gap in the pool, taken back when the pool is full.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

class TrackQueue {
public:
    ~TrackQueue();
    bool init(size_t pool_bytes, size_t max_tracks);  // false: no memory for it

    size_t size() const { return n_; }
    uint32_t id(size_t i) const { return e_[i].id; }
    std::string_view path(size_t i) const { return {pool_ + e_[i].off, e_[i].len}; }
    int indexOf(uint32_t id) const;  // -1: not queued
    bool contains(std::string_view path) const;
    // Add at the end: its id, or 0 when it's queued already or there's no room
    uint32_t add(std::string_view path);
    void erase(size_t i);
    void move(size_t from, size_t to);  // (the others shift up or down a place)
    void clear();
    bool full() const;  // (no room for another track of an average path's length)

private:
    struct Entry {
        uint32_t id, off, hash;
        uint16_t len;
    };
    static uint32_t hashOf(std::string_view s);
    void compact();

    char* pool_ = nullptr;
    size_t pool_cap_ = 0, pool_used_ = 0, holes_ = 0;
    Entry* e_ = nullptr;
    size_t n_ = 0, cap_ = 0;
    uint32_t next_id_ = 1;
};
