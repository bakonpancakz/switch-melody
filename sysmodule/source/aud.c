#define NX_SERVICE_ASSUME_NON_DOMAIN
#include "aud.h"

static Service g_audaSrv;

// IAudioSystemManagerForApplet
Result audaInitialize(void) {
    if (hosversionBefore(11, 0, 0)) return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
    return smGetService(&g_audaSrv, "aud:a");
}

void audaExit(void) { serviceClose(&g_audaSrv); }

Result audaSetAudioOutputProcessMasterVolume(u64 pid, u64 delay, float volume) {
    const struct {
        float volume;
        u64 pid;
        u64 timespan;
    } in = {volume, pid, delay};
    return serviceDispatchIn(&g_audaSrv, 5, in);
}

Result audaSetAudioInputProcessMasterVolume(u64 pid, u64 delay, float volume) {
    const struct {
        float volume;
        u64 pid;
        u64 timespan;
    } in = {volume, pid, delay};
    return serviceDispatchIn(&g_audaSrv, 7, in);
}
