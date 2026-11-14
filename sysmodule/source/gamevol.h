// The volume of the game in front, apart from the music's (the music plays
// through its own audout, so lowering the game leaves it alone)
#pragma once
#include <switch.h>

namespace gamevol {

void start();      // the saved volume, and the thread that keeps it applied
u32 get();         // 0..100, or MELODY_NO_VOLUME (before firmware 11.0.0, which has no aud:a)
void set(u32 pct); // 0..100 (saved a moment later)

}  // namespace gamevol
