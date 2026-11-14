// The overlay's side of the "melody" service (common/melody.h)
#pragma once
#include <switch.h>

#include <string>

#include "melody.h"

namespace client {

// Connect if the sysmodule is running (never waits for it): false if it isn't
bool connect();
void close();
bool connected();
bool outdated();  // the sysmodule running is older than this overlay (restart the Switch)

bool status(MelodyStatus* s);
bool track(MelodyTrack* t);
void control(u32 what);
void seek(u32 ms);
void setVolume(u32 v);
void setShuffle(bool on);
u32 gameVolume();  // the game's own volume 0..100 (MELODY_NO_VOLUME: not on this firmware, or an older sysmodule)
void setGameVolume(u32 v);  // 0..100
void setRepeat(u32 mode);
u32 enqueue(const char* path);
void clear();
u32 queue(u32 first, MelodyName* out, u32 max);
void playIndex(u32 index);
u32 moveTrack(u32 index, s32 by);  // its index now
void removeTrack(u32 index);
u32 savePlaylist(const char* name);  // a MelodySave (0: no answer)
const char* saveError(u32 r);  // what to say when it isn't MelodySave_Ok
u32 loadPlaylist(const char* name, bool append);
MelodyEqResult setEqualizer(const char* name);  // (filters -2: no answer)
std::string equalizer();                        // the config in use ("" for none)

}  // namespace client
