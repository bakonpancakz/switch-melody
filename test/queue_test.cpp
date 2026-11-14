// PC check of common/track_queue.cpp: random adds, removes and moves, side
// by side with a plain vector of strings; a small pool, so the gaps removes
// leave are closed again and again
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "../common/track_queue.h"

int main() {
    TrackQueue q;
    if (!q.init(4096, 200)) return 1;
    std::vector<std::pair<uint32_t, std::string>> ref;
    srand(12345);
    int bad = 0, adds = 0, full = 0;
    auto same = [&](int step) {
        if (q.size() != ref.size()) { printf("step %d: size %zu, want %zu\n", step, q.size(), ref.size()); return false; }
        for (size_t i = 0; i < ref.size(); i++)
            if (q.id(i) != ref[i].first || q.path(i) != ref[i].second) { printf("step %d: entry %zu differs\n", step, i); return false; }
        return true;
    };
    for (int step = 0; step < 200000 && !bad; step++) {
        int op = rand() % 10;
        if (op < 5) {  // add (some already there)
            std::string p = "sdmc:/Music/Artist " + std::to_string(rand() % 40) + "/Album/" + std::to_string(rand() % 300) + " - Song.flac";
            bool there = false;
            for (auto& r : ref) there |= r.second == p;
            uint32_t id = q.add(p);
            if (there && id) { printf("step %d: added a duplicate\n", step); bad++; }
            if (!there && id) ref.push_back({id, p}), adds++;
            if (!there && !id) full++;
            if (!id && !there && ref.size() < 20) { printf("step %d: refused with room\n", step); bad++; }
        } else if (op < 8 && !ref.empty()) {  // remove
            size_t i = rand() % ref.size();
            q.erase(i);
            ref.erase(ref.begin() + i);
        } else if (!ref.empty()) {  // move
            size_t a = rand() % ref.size(), b = rand() % ref.size();
            q.move(a, b);
            auto e = ref[a];
            ref.erase(ref.begin() + a);
            ref.insert(ref.begin() + b, e);
        }
        if (!same(step)) bad++;
        for (size_t i = 0; i < ref.size() && !bad; i += 7)
            if (q.indexOf(ref[i].first) != (int)i || !q.contains(ref[i].second)) { printf("step %d: lookup\n", step); bad++; }
    }
    printf("queue: %d adds, %d refused when full, %s\n", adds, full, bad ? "FAILED" : "all ok");
    return bad != 0;
}
