// Melody: a music player that keeps going whatever runs in front (a game, an
// app, the HOME menu). It starts with the Switch and stays quiet until told
// to play; its overlay talks to it over the "melody" service (common/melody.h).
//
// Two threads: this one answers IPC, player.cpp's decodes into audout. The
// SD card and audout are let go of while it's paused for a while.
#include <switch.h>

#include <cstring>

#include "gamevol.h"
#include "ipc_msg.h"
#include "melody.h"
#include "player.h"

#define INNER_HEAP_SIZE 0xE0000  // 896 KB: the queue (252 KB at most), 128 KB of sound queued, a 64 KB read buffer,
                                 // the playback thread's stack (64 KB), a decoder (FLAC: ~150 KB), the resampler

extern "C" {

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void) {
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
    // (the file system and audout are opened when they're needed, in player.cpp)
}

void __appExit(void) { smExit(); }

}  // extern "C"

namespace {

constexpr int MAX_SESSIONS = 6;
const Result ERR_UNKNOWN_COMMAND = MAKERESULT(Module_Libnx, LibnxError_NotFound);
const Result ERR_BAD_ARGS = MAKERESULT(Module_Libnx, LibnxError_BadInput);

// Write a CMIF reply into the TLS
void reply(Result rc, const void* data, u32 size) { ipcReply(armGetTls(), rc, data, size); }

u32 inU32(const HipcParsedRequest& req) { return ipcInU32(armGetTls(), &req); }

// One request: false when the session is to be closed
bool handle() {
    void* tls = armGetTls();
    HipcParsedRequest req = hipcParseRequest(tls);
    if (req.meta.type == CmifCommandType_Close) return false;
    const CmifInHeader* h = (const CmifInHeader*)cmifGetAlignedDataStart(req.data.data_words, tls);
    if (req.meta.type == CmifCommandType_Control) {
        if (h->command_id == 3) {  // QueryPointerBufferSize: none here (buffers are mapped)
            u16 size = 0;
            reply(0, &size, sizeof(size));
        } else {
            reply(ERR_UNKNOWN_COMMAND, nullptr, 0);
        }
        return true;
    }
    if (req.meta.type != CmifCommandType_Request || h->magic != CMIF_IN_HEADER_MAGIC) {
        reply(ERR_UNKNOWN_COMMAND, nullptr, 0);
        return true;
    }
    auto sendBuf = [&](u32 i, size_t* size) -> void* {
        if (i >= req.meta.num_send_buffers) return nullptr;
        *size = hipcGetBufferSize(&req.data.send_buffers[i]);
        return hipcGetBufferAddress(&req.data.send_buffers[i]);
    };
    auto recvBuf = [&](u32 i, size_t* size) -> void* {
        if (i >= req.meta.num_recv_buffers) return nullptr;
        *size = hipcGetBufferSize(&req.data.recv_buffers[i]);
        return hipcGetBufferAddress(&req.data.recv_buffers[i]);
    };
    size_t size = 0;
    switch (h->command_id) {
        case MelodyCmd_GetStatus: {
            MelodyStatus s;
            player::status(&s);
            reply(0, &s, sizeof(s));
            break;
        }
        case MelodyCmd_GetTrack: {
            void* p = recvBuf(0, &size);
            if (!p || size < sizeof(MelodyTrack)) {
                reply(ERR_BAD_ARGS, nullptr, 0);
                break;
            }
            MelodyTrack t;
            player::track(&t);
            memcpy(p, &t, sizeof(t));
            reply(0, nullptr, 0);
            break;
        }
        case MelodyCmd_Control:
            player::control(inU32(req));
            reply(0, nullptr, 0);
            break;
        case MelodyCmd_Seek:
            player::seek(inU32(req));
            reply(0, nullptr, 0);
            break;
        case MelodyCmd_SetVolume:
            player::setVolume(inU32(req));
            reply(0, nullptr, 0);
            break;
        case MelodyCmd_SetShuffle:
            player::setShuffle(inU32(req) != 0);
            reply(0, nullptr, 0);
            break;
        case MelodyCmd_SetRepeat:
            player::setRepeat(inU32(req));
            reply(0, nullptr, 0);
            break;
        case MelodyCmd_Enqueue: {
            const char* p = (const char*)sendBuf(0, &size);
            if (!p || size == 0 || size > MELODY_PATH_MAX) {
                reply(ERR_BAD_ARGS, nullptr, 0);
                break;
            }
            char path[MELODY_PATH_MAX];
            memcpy(path, p, size);
            path[size - 1] = 0;
            // (the reply is built after: the scan's IPC to the file system uses the TLS)
            u32 added = player::enqueue(path);
            reply(0, &added, sizeof(added));
            break;
        }
        case MelodyCmd_Clear:
            player::clear();
            reply(0, nullptr, 0);
            break;
        case MelodyCmd_GetQueue: {
            u32 first = inU32(req);
            MelodyName* out = (MelodyName*)recvBuf(0, &size);
            u32 n = out ? player::queueNames(first, out, (u32)(size / sizeof(MelodyName))) : 0;
            reply(0, &n, sizeof(n));
            break;
        }
        case MelodyCmd_PlayIndex:
            player::playIndex(inU32(req));
            reply(0, nullptr, 0);
            break;
        case MelodyCmd_MoveTrack: {
            MelodyMove m;
            memcpy(&m, ipcInData(armGetTls(), &req), sizeof(m));
            u32 now = player::moveTrack(m.index, m.by);
            reply(0, &now, sizeof(now));
            break;
        }
        case MelodyCmd_SetEqualizer: {
            const char* p = (const char*)sendBuf(0, &size);
            if (!p || size == 0 || size > MELODY_NAME_MAX) {
                reply(ERR_BAD_ARGS, nullptr, 0);
                break;
            }
            char name[MELODY_NAME_MAX];
            memcpy(name, p, size);
            name[size - 1] = 0;
            MelodyEqResult r = player::setEqualizer(name);  // (the request's all read: the card's IPC uses the TLS)
            reply(0, &r, sizeof(r));
            break;
        }
        case MelodyCmd_GetEqualizer: {
            void* p = recvBuf(0, &size);
            if (!p || size < sizeof(MelodyName)) {
                reply(ERR_BAD_ARGS, nullptr, 0);
                break;
            }
            MelodyName n;
            player::equalizer(&n);
            memcpy(p, &n, sizeof(n));
            reply(0, nullptr, 0);
            break;
        }
        case MelodyCmd_GetGameVolume: {
            u32 pct = gamevol::get();
            reply(0, &pct, sizeof(pct));
            break;
        }
        case MelodyCmd_SetGameVolume:
            gamevol::set(inU32(req));
            reply(0, nullptr, 0);
            break;
        case MelodyCmd_GetVersion: {
            u32 v = MELODY_PROTOCOL;
            reply(0, &v, sizeof(v));
            break;
        }
        case MelodyCmd_RemoveTrack:
            player::removeTrack(inU32(req));
            reply(0, nullptr, 0);
            break;
        case MelodyCmd_SavePlaylist:
        case MelodyCmd_LoadPlaylist: {
            u32 cmd = h->command_id;
            u32 append = cmd == MelodyCmd_LoadPlaylist ? inU32(req) : 0;
            const char* p = (const char*)sendBuf(0, &size);
            if (!p || size == 0 || size > MELODY_TEXT_MAX) {
                reply(ERR_BAD_ARGS, nullptr, 0);
                break;
            }
            char name[MELODY_TEXT_MAX];
            memcpy(name, p, size);
            name[size - 1] = 0;
            // (read all of the request before this: the file system's IPC uses the TLS)
            u32 out = cmd == MelodyCmd_SavePlaylist ? player::savePlaylist(name) : player::loadPlaylist(name, append != 0);
            reply(0, &out, sizeof(out));
            break;
        }
        default:
            reply(ERR_UNKNOWN_COMMAND, nullptr, 0);
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    // the service first: the overlay checks the process is there, then asks sm
    // for it, which waits for it to be registered
    Handle handles[1 + MAX_SESSIONS];
    s32 count = 1;
    if (R_FAILED(smRegisterService(&handles[0], smEncodeName(MELODY_SERVICE), false, MAX_SESSIONS))) {
        for (;;) svcSleepThread(UINT64_MAX);  // (the player can't be reached; there's nothing else to do)
    }
    player::start();
    gamevol::start();
    Handle reply_to = INVALID_HANDLE;
    for (;;) {
        s32 idx = -1;
        Result rc = svcReplyAndReceive(&idx, handles, count, reply_to, UINT64_MAX);
        reply_to = INVALID_HANDLE;
        if (R_FAILED(rc)) {
            // a session went away (or the reply couldn't be sent): close it
            if (idx > 0 && idx < count) {
                svcCloseHandle(handles[idx]);
                handles[idx] = handles[--count];
            }
            continue;
        }
        if (idx == 0) {
            Handle s;
            if (R_SUCCEEDED(svcAcceptSession(&s, handles[0]))) {
                if (count < 1 + MAX_SESSIONS)
                    handles[count++] = s;
                else
                    svcCloseHandle(s);
            }
            continue;
        }
        if (idx < 0 || idx >= count) continue;
        if (handle()) {
            reply_to = handles[idx];
        } else {
            svcCloseHandle(handles[idx]);
            handles[idx] = handles[--count];
        }
    }
}
