// PC check of the sysmodule's message handling (sysmodule/source/ipc_msg.h)
// against libnx's own client side: a request made by serviceMakeRequest, read
// the way main.cpp reads it, answered by ipcReply, parsed by
// serviceParseResponse. The "kernel" just copies the 0x100-byte message.
#define armGetTls armGetTls_hw
#include <switch/arm/tls.h>
#undef armGetTls
static unsigned char g_tls[0x100] __attribute__((aligned(16)));
#define armGetTls() ((void*)g_tls)

#include <stdio.h>
#include <string.h>
#include <switch/sf/service.h>
#include "../sysmodule/source/ipc_msg.h"
#include "../common/melody.h"

static int bad = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAILED: %s (line %d)\n", #c, __LINE__); bad++; } } while (0)

static unsigned char server[0x100] __attribute__((aligned(16)));

// One round trip: in raw data, one buffer, out raw data
static void roundtrip(const char* name, u32 cmd, const void* in, u32 in_size, u32 attr, void* buf, size_t buf_size,
                      const void* out, u32 out_size, void* got, u32 server_type) {
    Service s = {0};
    s.session = 1;
    s.own_handle = 1;
    SfBuffer bufs[8] = {{0}};
    SfBufferAttrs attrs = {0};
    bufs[0].ptr = buf;
    bufs[0].size = buf_size;
    attrs.attr0 = attr;
    memset(g_tls, 0xCC, sizeof(g_tls));
    void* raw = serviceMakeRequest(&s, cmd, 0, in_size, false, attrs, bufs, 0, NULL, 0, NULL);
    if (in_size) memcpy(raw, in, in_size);

    memcpy(server, g_tls, sizeof(server));  // (the kernel)
    HipcParsedRequest req = hipcParseRequest(server);
    const CmifInHeader* h = (const CmifInHeader*)cmifGetAlignedDataStart(req.data.data_words, server);
    CHECK(req.meta.type == server_type);
    CHECK(h->magic == CMIF_IN_HEADER_MAGIC);
    CHECK(h->command_id == cmd);
    if (in_size == 4) CHECK(ipcInU32(server, &req) == *(const u32*)in);
    if (attr & SfBufferAttr_In) {
        CHECK(req.meta.num_send_buffers == 1);
        CHECK(hipcGetBufferAddress(&req.data.send_buffers[0]) == buf);
        CHECK(hipcGetBufferSize(&req.data.send_buffers[0]) == buf_size);
    }
    if (attr & SfBufferAttr_Out) {
        CHECK(req.meta.num_recv_buffers == 1);
        CHECK(hipcGetBufferAddress(&req.data.recv_buffers[0]) == buf);
        CHECK(hipcGetBufferSize(&req.data.recv_buffers[0]) == buf_size);
    }
    memset(server, 0xDD, sizeof(server));
    ipcReply(server, 0, out, out_size);

    memcpy(g_tls, server, sizeof(g_tls));  // (the kernel)
    void* data = NULL;
    SfOutHandleAttrs oh = {0};
    Result rc = serviceParseResponse(&s, out_size, &data, 0, NULL, oh, NULL);
    CHECK(rc == 0);
    if (out_size && data) memcpy(got, data, out_size);
    if (out_size) CHECK(!memcmp(got, out, out_size));
    printf("%-12s cmd %2u: in %u bytes, buffer %zu, out %u bytes%s\n", name, cmd, in_size, buf_size, out_size, bad ? "" : " ok");
}

int main(void) {
    MelodyStatus st = {MelodyState_Playing, 1, 2, 80, 3, 4, 12, 61234, 245000, 0xABCDEF, 0};
    MelodyStatus got;
    roundtrip("GetStatus", MelodyCmd_GetStatus, NULL, 0, 0, NULL, 0, &st, sizeof(st), &got, CmifCommandType_Request);
    u32 v = 42;
    roundtrip("Seek", MelodyCmd_Seek, &(u32){123456}, 4, 0, NULL, 0, NULL, 0, NULL, CmifCommandType_Request);
    static MelodyTrack tr;
    roundtrip("GetTrack", MelodyCmd_GetTrack, NULL, 0, SfBufferAttr_HipcMapAlias | SfBufferAttr_Out, &tr, sizeof(tr), NULL, 0, NULL, CmifCommandType_Request);
    char path[] = "sdmc:/Music/Album/01 - Song.flac";
    u32 added = 0;
    roundtrip("Enqueue", MelodyCmd_Enqueue, NULL, 0, SfBufferAttr_HipcMapAlias | SfBufferAttr_In, path, sizeof(path), &v, 4, &added, CmifCommandType_Request);
    static MelodyName names[32];
    u32 n = 0;
    roundtrip("GetQueue", MelodyCmd_GetQueue, &(u32){7}, 4, SfBufferAttr_HipcMapAlias | SfBufferAttr_Out, names, sizeof(names), &v, 4, &n, CmifCommandType_Request);

    // libnx's serviceCreate asks QueryPointerBufferSize (a Control message) first
    memset(g_tls, 0, sizeof(g_tls));
    cmifMakeControlRequest(g_tls, 3, 0);
    memcpy(server, g_tls, sizeof(server));
    HipcParsedRequest req = hipcParseRequest(server);
    const CmifInHeader* h = (const CmifInHeader*)cmifGetAlignedDataStart(req.data.data_words, server);
    CHECK(req.meta.type == CmifCommandType_Control && h->command_id == 3);
    u16 zero = 0;
    ipcReply(server, 0, &zero, 2);
    memcpy(g_tls, server, sizeof(g_tls));
    CmifResponse res;
    CHECK(cmifParseResponse(&res, g_tls, false, sizeof(u16)) == 0 && *(u16*)res.data == 0);
    printf("QueryPointerBufferSize: %s\n", bad ? "" : "ok");

    // and serviceClose sends Close
    cmifMakeCloseRequest(g_tls, 0);
    CHECK(hipcParseRequest(g_tls).meta.type == CmifCommandType_Close);
    printf(bad ? "FAILED %d\n" : "all ok\n", bad);
    return bad != 0;
}
