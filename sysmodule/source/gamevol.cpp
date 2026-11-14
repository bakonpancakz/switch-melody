// Mixing: the game's own sound can be turned down while the music stays as it
// is. The audio server keeps a master volume for each process; aud:a sets it
// (sys-tune does the same, with the game's process id from pm). It's set when
// the volume changes, and for a new game: that isn't listening yet when it
// starts, so it's set again quickly for its first seconds. After that it stays
// (sleep and the HOME menu keep it): only pm is asked now and then which game
// runs. Nothing at all is done while the volume is at 100% and nothing was
// lowered. (The HOME overlay's ducking sets it back to 100% when it ends:
// melody_audio_disable_attenuation keeps that from happening.)
#include "gamevol.h"

#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "aud.h"
#include "melody.h"
#include "sdcard.h"

namespace gamevol {
namespace {

constexpr u64 FAST_NS = 200'000'000ULL;          // just after another game came up...
constexpr u64 FAST_FOR_NS = 15'000'000'000ULL;   // ...for this long
constexpr u64 SLOW_NS = 2'000'000'000ULL;        // then pm's asked this often whether another game came up
constexpr u64 SAVE_DELAY_NS = 2'000'000'000ULL;  // saved this long after a change (it's dragged about)
const char* VOLUME_FILE = MELODY_CONFIG_DIR "/game_volume.txt";

std::atomic<u32> g_pct{100};
std::atomic<u64> g_save_at{0};  // when to save (ns on the system tick); 0: nothing to save
std::atomic<bool> g_available{false};
UEvent g_wake;  // set(): look at it now, not when the wait ends
Thread g_thread;
alignas(0x1000) u8 g_stack[0x4000];  // (not from the heap: that's counted)

u64 nowNs() { return armTicksToNs(armGetSystemTick()); }

// The same law as the music's volume: each of the 10 steps is 4 dB, 0 is silence
float gainOf(u32 pct) {
    float v = pct / 100.0f;
    return v <= 0 ? 0.0f : std::pow(10.0f, (v - 1.0f) * 40.0f / 20.0f);
}

void load() {
    Card c;
    if (!c.ok) return;
    if (FILE* f = fopen(VOLUME_FILE, "rb")) {
        unsigned v = 100;
        if (fscanf(f, "%u", &v) == 1) g_pct = std::min(v, 100u);
        fclose(f);
    }
}

void saveIfDue(u64 now) {
    u64 at = g_save_at;
    if (!at || now < at) return;
    g_save_at = 0;
    Card c;
    if (!c.ok) return;
    mkdir("sdmc:/config", 0777);
    mkdir(MELODY_CONFIG_DIR, 0777);
    if (FILE* f = fopen(VOLUME_FILE, "wb")) {
        fprintf(f, "%u\n", (unsigned)g_pct.load());
        fclose(f);
    }
}

// The game in front (0: none). pm is only open for the call.
u64 gamePid() {
    u64 pid = 0;
    if (R_FAILED(pmdmntInitialize())) return 0;
    if (R_FAILED(pmdmntGetApplicationProcessId(&pid))) pid = 0;
    pmdmntExit();
    return pid;
}

bool apply(u64 pid, float gain) {
    if (R_FAILED(audaInitialize())) return false;  // (busy, or not there: the next round tries again)
    Result rc = audaSetAudioOutputProcessMasterVolume(pid, 0, gain);
    if (R_SUCCEEDED(rc)) rc = audaSetAudioInputProcessMasterVolume(pid, 0, gain);
    audaExit();
    return R_SUCCEEDED(rc);
}

void threadMain(void*) {
    u64 lowered_pid = 0;  // a game whose volume was set below full, and not given back yet
    u64 seen_pid = 0, fast_until = 0;
    u32 applied_pct = 100;  // what lowered_pid was last given
    bool failed = false;    // the last try didn't take (aud:a busy): tried again next round
    for (;;) {
        u64 now = nowNs();
        saveIfDue(now);
        u32 pct = g_pct;
        u64 wait = UINT64_MAX;  // (nothing to watch: asleep until the volume changes)
        if (g_save_at) wait = SAVE_DELAY_NS;
        if (pct < 100 || lowered_pid) {
            wait = SLOW_NS;
            u64 pid = gamePid();
            if (!pid) {  // (no game: nothing to keep applied; the next one starts at full)
                lowered_pid = 0;
                seen_pid = 0;
            } else {
                if (pid != seen_pid) {
                    seen_pid = pid;
                    fast_until = now + FAST_FOR_NS;
                    if (pid != lowered_pid) lowered_pid = 0;  // (a new process starts at full)
                }
                bool fresh = now < fast_until;
                // (aud:a only when something's to change: a new game's first seconds, the volume moved, a
                // try that didn't take. Not over and over: it stays set)
                bool due = fresh || failed || (pct < 100 ? (pid != lowered_pid || pct != applied_pct) : pid == lowered_pid);
                if (due) {
                    if (pct < 100) {
                        failed = !apply(pid, gainOf(pct));
                        if (!failed) lowered_pid = pid, applied_pct = pct;
                    } else if (pid == lowered_pid) {  // back to 100%: give it back once
                        failed = !apply(pid, 1.0f);
                        if (!failed) lowered_pid = 0, applied_pct = 100;
                    } else {
                        failed = false;
                    }
                }
                if (fresh) wait = FAST_NS;
            }
        }
        waitSingle(waiterForUEvent(&g_wake), wait == UINT64_MAX ? -1 : (s64)wait);
    }
}

}  // namespace

void start() {
    g_available = !hosversionBefore(11, 0, 0);
    if (!g_available) return;
    load();
    ueventCreate(&g_wake, true);
    // (priority and core as the player's thread)
    if (R_SUCCEEDED(threadCreate(&g_thread, threadMain, nullptr, g_stack, sizeof(g_stack), 0x30, -2))) threadStart(&g_thread);
}

u32 get() { return g_available ? g_pct.load() : MELODY_NO_VOLUME; }

void set(u32 pct) {
    if (!g_available) return;
    g_pct = std::min(pct, 100u);
    g_save_at = nowNs() + SAVE_DELAY_NS;
    ueventSignal(&g_wake);
}

}  // namespace gamevol
