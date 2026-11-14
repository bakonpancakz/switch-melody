// What the Melody sysmodule and its overlay say to each other over IPC
// (service "melody", plain CMIF requests). Raw data is small and fixed size;
// paths and names travel in mapped buffers (HipcMapAlias).
#pragma once
#include <stdint.h>

#define MELODY_SERVICE "melody"
#define MELODY_TITLE_ID 0x420000000000AD10ULL
#define MELODY_PATH_MAX 768
#define MELODY_TEXT_MAX 256
#define MELODY_NAME_MAX 128
#define MELODY_QUEUE_MAX 4096
#define MELODY_MUSIC_DIR "sdmc:/Music"
#define MELODY_CONFIG_DIR "sdmc:/config/sys-melody"
#define MELODY_PLAYLIST_DIR MELODY_CONFIG_DIR "/playlists"

enum MelodyCmd {
    MelodyCmd_GetStatus = 0,   // out MelodyStatus
    MelodyCmd_GetTrack = 1,    // out buffer MelodyTrack (the track playing now)
    MelodyCmd_Control = 2,     // in u32 MelodyControl
    MelodyCmd_Seek = 3,        // in u32 position (ms)
    MelodyCmd_SetVolume = 4,   // in u32 0..100
    MelodyCmd_SetShuffle = 5,  // in u32 0/1
    MelodyCmd_SetRepeat = 6,   // in u32 MelodyRepeat
    MelodyCmd_Enqueue = 7,     // in buffer path (file or folder, folders recursively); out u32 added
    MelodyCmd_Clear = 8,       //
    MelodyCmd_GetQueue = 9,    // in u32 first; out buffer MelodyName[]; out u32 written
    MelodyCmd_PlayIndex = 10,  // in u32 queue index
    MelodyCmd_MoveTrack = 11,     // in MelodyMove; out u32 its queue index now
    MelodyCmd_RemoveTrack = 12,   // in u32 queue index
    MelodyCmd_SavePlaylist = 13,  // in buffer name; out u32 MelodySave (as MELODY_PLAYLIST_DIR/<name>.m3u8)
    MelodyCmd_LoadPlaylist = 14,  // in u32 1 = add to the queue, 0 = replace it; in buffer name; out u32 added
    MelodyCmd_GetVersion = 15,    // out u32 MELODY_PROTOCOL (a sysmodule older than that hasn't this command)
    MelodyCmd_SetEqualizer = 16,  // in buffer name (a file in MELODY_EQ_DIR; "" = off); out MelodyEqResult
    MelodyCmd_GetEqualizer = 17,  // out buffer MelodyName (the one in use; "" = off)
    MelodyCmd_GetGameVolume = 18, // out u32 0..100: the game's own volume, apart from the music's (MELODY_NO_VOLUME: can't)
    MelodyCmd_SetGameVolume = 19, // in u32 0..100
};

// GetGameVolume's answer before firmware 11.0.0 (no aud:a there)
#define MELODY_NO_VOLUME 0xFFFFFFFFu

// Raised when the commands change. The overlay says so when the sysmodule
// running is older (it's only loaded again when the Switch restarts).
#define MELODY_PROTOCOL 4

// Equalizer APO configs (.txt), for the equalizer
#define MELODY_EQ_DIR MELODY_CONFIG_DIR "/eq"

typedef struct MelodyEqResult {
    int32_t filters;   // how many it made (-1: the file couldn't be read)
    uint32_t skipped;  // lines it couldn't use (GraphicEQ, Channel, ...)
} MelodyEqResult;

enum MelodySave {
    MelodySave_Ok = 1,
    MelodySave_BadName = 2,   // (it can't be a file name)
    MelodySave_NoCard = 3,
    MelodySave_WriteFailed = 4,
};

enum MelodyControl {
    MelodyControl_Toggle = 0,
    MelodyControl_Play = 1,
    MelodyControl_Pause = 2,
    MelodyControl_Next = 3,
    MelodyControl_Prev = 4,
};

enum MelodyState {
    MelodyState_Stopped = 0,  // nothing queued, or the end of the queue
    MelodyState_Playing = 1,
    MelodyState_Paused = 2,
};

enum MelodyRepeat {
    MelodyRepeat_Off = 0,
    MelodyRepeat_All = 1,
    MelodyRepeat_One = 2,
};

typedef struct MelodyStatus {
    uint8_t state;     // MelodyState
    uint8_t shuffle;   // 0/1
    uint8_t repeat;    // MelodyRepeat
    uint8_t volume;    // 0..100
    uint32_t index;    // queue index of the track playing (count when none)
    uint32_t order;    // its place in the play order (shuffled or not), from 0
    uint32_t count;    // tracks queued
    uint32_t position_ms;
    uint32_t duration_ms;  // 0 when unknown
    uint32_t track_id;     // changes whenever another track starts (overlay: reload the cover)
    uint32_t error;        // last track that couldn't be played (0: none), for the overlay to say so
} MelodyStatus;

typedef struct MelodyTrack {
    char path[MELODY_PATH_MAX];
    char title[MELODY_TEXT_MAX];  // file name when the file has no TITLE
    char artist[MELODY_TEXT_MAX];
    char album[MELODY_TEXT_MAX];
} MelodyTrack;

typedef struct MelodyMove {
    uint32_t index;
    int32_t by;  // -1: up a place, 1: down
} MelodyMove;

typedef struct MelodyName {
    char name[MELODY_NAME_MAX];  // the file name, without folders
} MelodyName;
