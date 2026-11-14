#include "client.h"

#include <cstring>

#include "texts.h"

namespace client {
namespace {

Service g_srv;
bool g_open = false;
u32 g_version = 0;

// A call that failed: the sysmodule went away (or was never there)
bool check(Result rc) {
    if (R_FAILED(rc)) close();
    return R_SUCCEEDED(rc);
}

void sendU32(u32 cmd, u32 v) {
    if (g_open) check(serviceDispatchIn(&g_srv, cmd, v));
}

}  // namespace

bool connect() {
    if (g_open) return true;
    // sm would wait for a service nobody registered: ask pm first whether it runs
    u64 pid = 0;
    if (R_FAILED(pmdmntGetProcessId(&pid, MELODY_TITLE_ID))) return false;
    g_open = R_SUCCEEDED(smGetService(&g_srv, MELODY_SERVICE));
    // (a sysmodule from before GetVersion answers it with an error: version 0)
    g_version = 0;
    if (g_open && R_FAILED(serviceDispatchOut(&g_srv, MelodyCmd_GetVersion, g_version))) g_version = 0;
    return g_open;
}

void close() {
    if (g_open) serviceClose(&g_srv);
    g_open = false;
}

bool connected() { return g_open; }
bool outdated() { return g_open && g_version < MELODY_PROTOCOL; }

bool status(MelodyStatus* s) { return g_open && check(serviceDispatchOut(&g_srv, MelodyCmd_GetStatus, *s)); }

bool track(MelodyTrack* t) {
    return g_open && check(serviceDispatch(&g_srv, MelodyCmd_GetTrack,
                                           .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                                           .buffers = {{t, sizeof(*t)}}, ));
}

void control(u32 what) { sendU32(MelodyCmd_Control, what); }
void seek(u32 ms) { sendU32(MelodyCmd_Seek, ms); }
void setVolume(u32 v) { sendU32(MelodyCmd_SetVolume, v); }
void setShuffle(bool on) { sendU32(MelodyCmd_SetShuffle, on ? 1 : 0); }

// (an older sysmodule doesn't know these: its error would close the connection)
u32 gameVolume() {
    u32 v = MELODY_NO_VOLUME;
    if (g_open && !outdated()) check(serviceDispatchOut(&g_srv, MelodyCmd_GetGameVolume, v));
    return v;
}

void setGameVolume(u32 v) {
    if (g_open && !outdated()) sendU32(MelodyCmd_SetGameVolume, v);
}

void setRepeat(u32 mode) { sendU32(MelodyCmd_SetRepeat, mode); }
void playIndex(u32 index) { sendU32(MelodyCmd_PlayIndex, index); }

void clear() {
    if (g_open) check(serviceDispatch(&g_srv, MelodyCmd_Clear));
}

u32 enqueue(const char* path) {
    u32 added = 0;
    if (g_open)
        check(serviceDispatchOut(&g_srv, MelodyCmd_Enqueue, added,
                                 .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                                 .buffers = {{path, strlen(path) + 1}}, ));
    return added;
}

u32 queue(u32 first, MelodyName* out, u32 max) {
    u32 n = 0;
    if (g_open)
        check(serviceDispatchInOut(&g_srv, MelodyCmd_GetQueue, first, n,
                                   .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                                   .buffers = {{out, max * sizeof(MelodyName)}}, ));
    return n;
}

u32 moveTrack(u32 index, s32 by) {
    MelodyMove m = {index, by};
    u32 now = index;
    if (g_open) check(serviceDispatchInOut(&g_srv, MelodyCmd_MoveTrack, m, now));
    return now;
}

void removeTrack(u32 index) { sendU32(MelodyCmd_RemoveTrack, index); }

u32 savePlaylist(const char* name) {
    u32 r = 0;
    if (g_open)
        check(serviceDispatchOut(&g_srv, MelodyCmd_SavePlaylist, r,
                                 .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                                 .buffers = {{name, strlen(name) + 1}}, ));
    return r;
}

const char* saveError(u32 r) {
    if (r == MelodySave_BadName) return txt::SAVE_BAD_NAME;
    if (r == MelodySave_NoCard) return txt::SAVE_NO_CARD;
    if (r == MelodySave_WriteFailed) return txt::SAVE_WRITE_FAILED;
    return txt::SAVE_NO_ANSWER;
}

u32 loadPlaylist(const char* name, bool append) {
    u32 added = 0, a = append ? 1 : 0;
    if (g_open)
        check(serviceDispatchInOut(&g_srv, MelodyCmd_LoadPlaylist, a, added,
                                   .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                                   .buffers = {{name, strlen(name) + 1}}, ));
    return added;
}

MelodyEqResult setEqualizer(const char* name) {
    MelodyEqResult r = {-2, 0};
    if (g_open && !outdated())  // (an older sysmodule doesn't know it: its error would close the connection)
        check(serviceDispatchOut(&g_srv, MelodyCmd_SetEqualizer, r,
                                 .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                                 .buffers = {{name, strlen(name) + 1}}, ));
    return r;
}

std::string equalizer() {
    MelodyName n = {};
    if (!g_open || outdated() || !check(serviceDispatch(&g_srv, MelodyCmd_GetEqualizer,
                                         .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                                         .buffers = {{&n, sizeof(n)}}, )))
        return "";
    n.name[sizeof(n.name) - 1] = 0;
    return n.name;
}

}  // namespace client
