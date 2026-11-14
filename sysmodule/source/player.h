// The music player: the queue, what's playing, and the thread that decodes
// into audout. Everything here is safe to call from the IPC thread.
#pragma once
#include <switch.h>

#include "melody.h"

namespace player {

void start();  // the saved queue and state, and the playback thread (idle until Play)

void status(MelodyStatus* out);
void track(MelodyTrack* out);
void control(u32 what);  // MelodyControl
void seek(u32 ms);
void setVolume(u32 v);
void setShuffle(bool on);
void setRepeat(u32 mode);
u32 enqueue(const char* path);  // a file or a folder (everything in it); how many were added
void clear();
u32 queueNames(u32 first, MelodyName* out, u32 max);
void playIndex(u32 index);
u32 moveTrack(u32 index, s32 by);  // its index now
void removeTrack(u32 index);
u32 savePlaylist(const char* name);  // the queue, as a playlist (replacing one of that name): a MelodySave
u32 loadPlaylist(const char* name, bool append);  // how many were added
MelodyEqResult setEqualizer(const char* name);    // a config in MELODY_EQ_DIR, or "" for none
void equalizer(MelodyName* out);                  // the one in use ("" for none)

}  // namespace player
