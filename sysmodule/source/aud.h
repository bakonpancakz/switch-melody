// aud:a, the audio server's calls for applets (firmware 11.0.0 and up): the
// master volume of one process's sound. Taken from sys-tune's aud.c
// (HookedBehemoth/sys-tune, by TotalJustice), cut down to what Melody uses.
//
// aud:a takes one session at a time, so it is opened for a call and closed
// again (gamevol.cpp), not held.
#pragma once
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

Result audaInitialize(void);  // IncompatSysVer before 11.0.0
void audaExit(void);

// The volume (1.0 is as the game made it) of a process's sound going out. delay
// is a fade time in ns (0: at once). sys-tune found the output volume only
// sticks when the input one is set too, so both are.
Result audaSetAudioOutputProcessMasterVolume(u64 pid, u64 delay, float volume);
Result audaSetAudioInputProcessMasterVolume(u64 pid, u64 delay, float volume);

#ifdef __cplusplus
}
#endif
