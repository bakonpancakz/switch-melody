#include "player.h"

#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include <string>
#include <string_view>
#include <vector>

#include "decoder.h"
#include "eq.h"
#include "playlist_file.h"
#include "resample.h"
#include "sdcard.h"
#include "tags.h"
#include "track_queue.h"

namespace player {

MelodyEqResult setEq(const char* name, bool save);  // (setEqualizer; save: into the state file)

namespace {

// (memory: this all lives in a fixed heap, main.cpp's INNER_HEAP_SIZE)
constexpr int BUFFERS = 8;           // queued in audout: 0.7 s, for when the card is slow
constexpr size_t FRAMES = 4096;      // per buffer (85 ms; 16 KB)
constexpr size_t TMP_FRAMES = 1024;  // converted to 16 bits at a time
constexpr size_t TMP_IN = 1024;      // decoded frames handed to the resampler at a time
constexpr size_t FILE_BUFFER = 64 << 10;    // a read of the card (a few a second, for FLAC)
constexpr size_t POOL_BYTES = 192 << 10;    // all the queue's paths together (~2,400 tracks)
constexpr size_t MAX_TRACKS = 3072;
constexpr u64 IDLE_RELEASE_NS = 10'000'000'000ULL;  // paused this long: let go of the card and audout
constexpr u64 SAVE_DELAY_NS = 2'000'000'000ULL;     // saving what changes a lot (volume, moves): this long after
const char* QUEUE_FILE = MELODY_CONFIG_DIR "/queue.txt";
const char* STATE_FILE = MELODY_CONFIG_DIR "/state.txt";
const char* LOG_FILE = MELODY_CONFIG_DIR "/log.txt";  // seeks and what went wrong (kept under 64 KB)

// --- shared with the IPC thread, under g_mx ---
Mutex g_mx;
CondVar g_cv;
// The queue. Each track has an id that stays with it while it's moved
// around or others are removed, so the playback thread (and its buffers) can
// say which track they mean without indices shifting under them. 0 is none.
TrackQueue g_q;
std::vector<u32> g_order;  // track ids in play order (room for MAX_TRACKS made at the start)
u8 g_state = MelodyState_Stopped;
u8 g_shuffle = 0, g_repeat = MelodyRepeat_Off, g_volume = 80;
bool g_volume_dirty = true;
u32 g_cur = 0;      // the track playing (or paused, or next to play)
u32 g_cur_ms = 0;   // where in it
u32 g_track_id = 0; // of the decoding whose buffers are playing (a new one each time a track starts)
u32 g_dec_shared = 0;  // the track the playback thread is decoding (it may be ahead of g_cur)
u32 g_error = 0;
// what the playback thread is to do next
bool g_jump = false;
bool g_jump_play = false;  // false: stop sound (pause, clear) and wait
u32 g_jump_id = 0, g_jump_ms = 0;
bool g_drop_decoder = false;  // the track being decoded is gone from the queue: don't reuse it
u64 g_save_state_at = 0, g_save_queue_at = 0;  // (0: nothing to save)
Eq g_eq_next;              // an equalizer for the playback thread to take up
bool g_eq_dirty = false;
std::string g_eq_name;     // the config it's from ("" for none)

struct Meta {
    u32 id = 0;  // 0: unused
    MelodyTrack info;
    u32 dur_ms = 0;
};
Meta g_meta[4];  // what the last few tracks' tags said
u32 g_meta_next = 0;

u32 g_rng = 0x1234567;
u32 rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

u64 nowNs() { return armTicksToNs(armGetSystemTick()); }

std::string_view baseName(std::string_view p) {
    size_t s = p.find_last_of('/');
    return s == std::string_view::npos ? p : p.substr(s + 1);
}

// (all of these under g_mx)
int indexOf(u32 id) { return g_q.indexOf(id); }

std::string pathOf(u32 id) {
    int i = indexOf(id);
    return i < 0 ? std::string() : std::string(g_q.path(i));
}

u32 firstId() { return g_order.empty() ? 0 : g_order[0]; }

Meta* findMeta(u32 id) {
    if (!id) return nullptr;
    for (Meta& m : g_meta)
        if (m.id == id) return &m;
    return nullptr;
}

void putMeta(u32 id, const std::string& path, const Tags& t) {
    Meta* m = findMeta(id);
    if (!m) m = &g_meta[g_meta_next++ % 4];
    m->id = id;
    memset(&m->info, 0, sizeof(m->info));
    strncpy(m->info.path, path.c_str(), sizeof(m->info.path) - 1);
    if (t.title[0])
        memcpy(m->info.title, t.title, sizeof(t.title));
    else {  // the file name without its extension
        std::string n(baseName(path));
        size_t dot = n.find_last_of('.');
        if (dot != std::string::npos && dot > 0) n.resize(dot);
        strncpy(m->info.title, n.c_str(), sizeof(m->info.title) - 1);
    }
    memcpy(m->info.artist, t.artist, sizeof(t.artist));
    memcpy(m->info.album, t.album, sizeof(t.album));
    m->dur_ms = t.rate ? (u32)(t.frames * 1000 / t.rate) : 0;
}

int orderPos(u32 id) {
    for (size_t i = 0; i < g_order.size(); i++)
        if (g_order[i] == id) return (int)i;
    return -1;
}

// The track after id in play order; natural: because it ended (not Next).
// 0: the end of the queue.
u32 nextId(u32 id, bool natural) {
    if (g_order.empty()) return 0;
    if (natural && g_repeat == MelodyRepeat_One && indexOf(id) >= 0) return id;
    int p = orderPos(id);
    if (p >= 0 && p + 1 < (int)g_order.size()) return g_order[p + 1];
    if (g_repeat != MelodyRepeat_Off || p < 0) return g_order[0];
    return 0;
}

u32 prevId(u32 id) {
    if (g_order.empty()) return 0;
    int p = orderPos(id);
    if (p > 0) return g_order[p - 1];
    if (g_repeat == MelodyRepeat_All) return g_order.back();
    return p == 0 ? id : g_order[0];
}

// The play order again: the queue's, or shuffled with the track playing first
void makeOrder() {
    size_t n = g_q.size();
    g_order.resize(n);
    for (size_t i = 0; i < n; i++) g_order[i] = g_q.id(i);
    if (!g_shuffle || n < 2) return;
    for (size_t i = n - 1; i > 0; i--) std::swap(g_order[i], g_order[rnd() % (i + 1)]);
    int p = orderPos(g_cur);
    if (p > 0) std::swap(g_order[0], g_order[p]);
}

void jump(u32 id, u32 ms, bool play) {
    g_jump = true;
    g_jump_id = id;
    g_jump_ms = ms;
    g_jump_play = play;
    condvarWakeAll(&g_cv);
}

// --- the config files ---

// A line in the log (only for rare things: a seek, a track that won't play)
void note(const char* fmt, ...) {
    char line[256 + MELODY_PATH_MAX];  // (a path fits)
    va_list a;
    va_start(a, fmt);
    vsnprintf(line, sizeof(line), fmt, a);
    va_end(a);
    Card c;
    if (!c.ok) return;
    struct stat st;
    bool big = stat(LOG_FILE, &st) == 0 && st.st_size > (64 << 10);
    if (FILE* f = fopen(LOG_FILE, big ? "wb" : "ab")) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

// The most of the heap in use so far, logged when it's 32 KB past the last
// line (a few lines, ever): to see how near the heap's size it gets
void notePeak(const char* when) {
    static size_t peak = 0;
    size_t used = mallinfo().uordblks;
    if (used < peak + (32 << 10)) return;
    peak = used;
    note("memory: %u KB in use, the most so far (%s)", (unsigned)(used >> 10), when);
}

void configDir() {
    mkdir("sdmc:/config", 0777);
    mkdir(MELODY_CONFIG_DIR, 0777);
}

void saveState() {
    mutexLock(&g_mx);
    char line[96 + MELODY_NAME_MAX];
    int i = indexOf(g_cur);
    snprintf(line, sizeof(line), "%u %u %u %u %u\n%s\n", g_volume, g_shuffle, g_repeat, i < 0 ? 0u : (u32)i, g_cur_ms,
             g_eq_name.c_str());
    mutexUnlock(&g_mx);
    Card c;
    if (!c.ok) return;
    configDir();
    FILE* f = fopen(STATE_FILE, "wb");
    if (!f) return;
    fputs(line, f);
    fclose(f);
}

// The queue's paths from index i on into buf, as lines (under g_mx): the index after the last
size_t queueLines(size_t i, std::string& buf, size_t max) {
    buf.clear();
    for (; i < g_q.size(); i++) {
        std::string_view p = g_q.path(i);
        if (!buf.empty() && buf.size() + p.size() + 1 > max) break;
        buf.append(p);
        buf += '\n';
    }
    return i;
}

// (a few KB at a time, so the queue is never copied whole, and the playback
// thread isn't kept waiting while the card writes)
void saveQueue() {
    Card c;
    if (!c.ok) return;
    configDir();
    FILE* f = fopen(QUEUE_FILE, "wb");
    if (!f) return;
    std::string buf;
    buf.reserve(4096 + MELODY_PATH_MAX);
    for (size_t i = 0;;) {
        mutexLock(&g_mx);
        size_t done = g_q.size();
        i = queueLines(i, buf, 4096);
        mutexUnlock(&g_mx);
        if (!buf.empty()) fwrite(buf.data(), 1, buf.size(), f);
        if (i >= done) break;
    }
    fclose(f);
}

void load() {
    Card c;
    if (!c.ok) return;
    if (FILE* f = fopen(QUEUE_FILE, "rb")) {
        static char line[MELODY_PATH_MAX + 2];
        while (fgets(line, sizeof(line), f)) {
            size_t n = strcspn(line, "\r\n");
            line[n] = 0;
            if (n) g_q.add(std::string_view(line, n));
        }
        fclose(f);
    }
    if (FILE* f = fopen(STATE_FILE, "rb")) {
        unsigned v = 80, s = 0, r = 0, i = 0, ms = 0;
        if (fscanf(f, "%u %u %u %u %u", &v, &s, &r, &i, &ms) == 5) {
            g_volume = (u8)std::min(v, 100u);
            g_shuffle = s ? 1 : 0;
            g_repeat = (u8)std::min(r, 2u);
            if (i < g_q.size()) {
                g_cur = g_q.id(i);
                g_cur_ms = ms;
            }
            char eq[MELODY_NAME_MAX + 2] = "";
            if (fgets(eq, sizeof(eq), f) && eq[0] == '\n') fgets(eq, sizeof(eq), f);  // (the rest of line 1, then line 2)
            eq[strcspn(eq, "\r\n")] = 0;
            g_eq_name = eq;
        }
        fclose(f);
    }
    if (!g_cur && g_q.size()) g_cur = g_q.id(0);
    makeOrder();
    g_state = g_q.size() ? MelodyState_Paused : MelodyState_Stopped;
}

// --- the playback thread ---

struct Buf {
    AudioOutBuffer ab;  // (first: a released AudioOutBuffer* is its Buf*)
    bool queued;
    u32 track, decoding, end_ms;  // its track's id, the decoding's id, where in the track it ends
};

Buf g_bufs[BUFFERS];
int g_in_flight = 0;
bool g_out_open = false;

SdFile* g_file = nullptr;
Decoder* g_dec = nullptr;
Resampler g_rs = {};
u32 g_dec_id = 0, g_dec_decoding = 0;
u32 g_dec_start_ms = 0;
u64 g_dec_frames = 0;  // 48 kHz frames given out since g_dec_start_ms
bool g_dec_eof = false;
int g_pad_left = 0;    // silence still to push through the resampler at the end
bool g_end_of_queue = false;
u32 g_next_decoding = 1;

float g_tmp[2 * TMP_FRAMES];
Eq g_eq;  // the equalizer in use (the playback thread's own copy)
float g_tmp_in[2 * TMP_IN];

// The volume as ears hear it: each step of the slider (10 of them) is the same
// change in loudness, 4 dB, over 40 dB in all; 0 is silence
void applyVolume() {
    mutexLock(&g_mx);
    float v = g_volume / 100.0f;
    g_volume_dirty = false;
    mutexUnlock(&g_mx);
    float gain = v <= 0 ? 0.0f : std::pow(10.0f, (v - 1.0f) * 40.0f / 20.0f);
    if (g_out_open) audoutSetAudioOutVolume(gain);
}

bool outOpen() {
    if (g_out_open) return true;
    if (R_FAILED(audoutInitialize())) return false;
    if (R_FAILED(audoutStartAudioOut())) {
        audoutExit();
        return false;
    }
    g_out_open = true;
    g_in_flight = 0;
    for (Buf& b : g_bufs) b.queued = false;
    applyVolume();
    return true;
}

// A buffer came back: playing, what it held is what's playing now
void released(Buf* b, bool playing) {
    if (!b->queued) return;
    b->queued = false;
    g_in_flight--;
    if (!playing) return;
    mutexLock(&g_mx);
    if (g_state == MelodyState_Playing && !g_jump && indexOf(b->track) >= 0) {
        if (b->decoding != g_track_id) g_save_state_at = nowNs();  // (another track: save soon)
        g_cur = b->track;
        g_cur_ms = b->end_ms;
        g_track_id = b->decoding;
    }
    mutexUnlock(&g_mx);
}

void collect(bool playing) {
    for (;;) {
        AudioOutBuffer* rel = nullptr;
        u32 count = 0;
        if (R_FAILED(audoutGetReleasedAudioOutBuffer(&rel, &count)) || count == 0 || !rel) break;
        released((Buf*)rel, playing);
    }
}

void waitReleased(u64 timeout_ns) {
    AudioOutBuffer* rel = nullptr;
    u32 count = 0;
    if (R_SUCCEEDED(audoutWaitPlayFinish(&rel, &count, timeout_ns)) && count && rel) released((Buf*)rel, true);
    collect(true);
}

// Stop the sound now: everything queued comes back unplayed. How many
// buffers didn't come back by themselves (0: all did).
int outFlush() {
    if (!g_out_open || g_in_flight == 0) return 0;
    bool flushed = false;
    audoutFlushAudioOutBuffers(&flushed);
    for (int tries = 0; g_in_flight > 0 && tries < 50; tries++) {
        AudioOutBuffer* rel = nullptr;
        u32 count = 0;
        audoutWaitPlayFinish(&rel, &count, 10'000'000ULL);
        if (count && rel) released((Buf*)rel, false);
        collect(false);
    }
    int left = g_in_flight;
    if (left > 0) {  // (they didn't all come back: start audout over, so none of them plays)
        audoutStopAudioOut();
        audoutExit();
        g_out_open = false;
        outOpen();
    }
    return left;
}

void outClose() {
    if (!g_out_open) return;
    outFlush();
    audoutStopAudioOut();
    audoutExit();
    g_out_open = false;
}

void decClose() {
    delete g_dec;
    g_dec = nullptr;
    rs_free(&g_rs);
    if (g_file) g_file->close();
    g_dec_id = 0;
    mutexLock(&g_mx);
    g_dec_shared = 0;
    mutexUnlock(&g_mx);
}

// Open a track, at ms into it: false if it can't be played
bool decOpen(u32 id, u32 ms) {
    decClose();
    mutexLock(&g_mx);
    std::string path = pathOf(id);
    mutexUnlock(&g_mx);
    if (path.empty()) return false;
    if (!g_file) g_file = new SdFile(FILE_BUFFER);
    Tags t;
    if (!g_file->open(path.c_str())) {
        note("can't play %s: it can't be opened (0x%x)", path.c_str(), g_file->openError());
        decClose();
        return false;
    }
    if (tags_read(g_file->stream(), &t, nullptr, 0) != 0) {
        note("can't play %s: not FLAC or Ogg Opus", path.c_str());
        decClose();
        return false;
    }
    mutexLock(&g_mx);
    putMeta(id, path, t);
    mutexUnlock(&g_mx);
    g_dec = decoderOpen(g_file->stream(), t);
    if (!g_dec || rs_init(&g_rs, g_dec->rate, 48000) != 0) {
        note("can't play %s: %s (%u Hz, %u channels)", path.c_str(), g_dec ? "its sample rate" : "the decoder won't start",
             t.rate, t.channels);
        decClose();
        return false;
    }
    if (ms && !g_dec->seek((u64)ms * g_dec->rate / 1000)) ms = 0, g_dec->seek(0);
    g_dec_id = id;
    g_dec_decoding = g_next_decoding++;
    g_dec_start_ms = ms;
    g_dec_frames = 0;
    g_dec_eof = false;
    g_pad_left = g_rs.passthrough ? 0 : g_rs.width + 1;
    mutexLock(&g_mx);
    g_dec_shared = id;
    mutexUnlock(&g_mx);
    notePeak("a track opened");
    return true;
}

// The same track, somewhere else in it
bool decSeek(u32 ms) {
    if (!g_dec || !g_dec->seek((u64)ms * g_dec->rate / 1000)) return false;
    rs_reset(&g_rs);
    g_dec_start_ms = ms;
    g_dec_frames = 0;
    g_dec_eof = false;
    g_pad_left = g_rs.passthrough ? 0 : g_rs.width + 1;
    return true;
}

// Open id (or what follows it, when it can't be played): false: nothing can
bool decOpenFrom(u32 id, u32 ms) {
    mutexLock(&g_mx);
    size_t n = g_q.size();
    mutexUnlock(&g_mx);
    for (size_t tries = 0; tries < n; tries++) {
        if (decOpen(id, ms)) return true;
        mutexLock(&g_mx);
        int i = indexOf(id);
        g_error = i + 1;
        u32 next = nextId(id, false);
        mutexUnlock(&g_mx);
        if (!next) return false;
        id = next;
        ms = 0;
    }
    return false;
}

void toS16(const float* in, s16* out, size_t frames) {
    for (size_t i = 0; i < 2 * frames; i++) {
        float v = in[i] * 32767.0f;
        out[i] = (s16)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : lrintf(v));
    }
}

// Frames of the open track into out (one track per buffer): fewer than max at its end
size_t produce(s16* out, size_t max) {
    size_t n = 0;
    while (n < max) {
        size_t want = std::min(max - n, TMP_FRAMES);
        if (g_rs.passthrough) {
            size_t k = g_dec->read(g_tmp, want);
            if (!k) break;
            eq_process(&g_eq, g_tmp, k);
            toS16(g_tmp, out + 2 * n, k);
            n += k;
            continue;
        }
        size_t k = rs_pull(&g_rs, g_tmp, want);
        if (k) {
            eq_process(&g_eq, g_tmp, k);
            toS16(g_tmp, out + 2 * n, k);
            n += k;
            continue;
        }
        if (!g_dec_eof) {
            size_t m = g_dec->read(g_tmp_in, TMP_IN);
            if (m) {
                rs_push(&g_rs, g_tmp_in, m);
                continue;
            }
            g_dec_eof = true;
        }
        if (g_pad_left <= 0) break;
        memset(g_tmp_in, 0, sizeof(float) * 2 * 64);
        g_pad_left -= (int)rs_push(&g_rs, g_tmp_in, std::min(g_pad_left, 64));
    }
    g_dec_frames += n;
    return n;
}

// Queue sound until audout holds all the buffers (or the queue ends)
void fill() {
    for (Buf& b : g_bufs) {
        if (b.queued) continue;
        size_t n = 0;
        while (g_dec && n == 0) {
            n = produce((s16*)b.ab.buffer, FRAMES);
            if (n) break;
            // the track ended: on to the next (gapless)
            mutexLock(&g_mx);
            u32 next = nextId(g_dec_id, true);
            mutexUnlock(&g_mx);
            decClose();
            if (!next || !decOpenFrom(next, 0)) g_end_of_queue = true;
        }
        if (!n) return;
        b.ab.data_size = n * 4;
        b.ab.data_offset = 0;
        b.track = g_dec_id;
        b.decoding = g_dec_decoding;
        b.end_ms = g_dec_start_ms + (u32)(g_dec_frames * 1000 / 48000);
        b.queued = true;
        if (R_FAILED(audoutAppendAudioOutBuffer(&b.ab))) {
            b.queued = false;
            return;
        }
        g_in_flight++;
    }
}

// While music plays, the Switch is told now and then that someone's using it:
// its Auto-Sleep counts from the last button press, so it would sleep (and
// stop the music) with nobody touching it, as with Display Off. omm's idle:sys
// (IPolicyManagerSystem) 5 is ReportUserIsActive, what am sends for input.
Service g_idle;
u64 g_idle_reported = 0;
constexpr u64 AWAKE_EVERY_NS = 20'000'000'000ULL;  // (the shortest Auto-Sleep is a minute)

void keepAwake(u64 now) {
    if (g_idle_reported && now - g_idle_reported < AWAKE_EVERY_NS) return;
    g_idle_reported = now;
    if (!serviceIsActive(&g_idle) && R_FAILED(smGetService(&g_idle, "idle:sys"))) return;
    Result rc = serviceDispatch(&g_idle, 5);
    static bool told = false;
    if (R_FAILED(rc) && !told) note("keep awake: idle:sys ReportUserIsActive failed (0x%x)", rc), told = true;
}

void letSleep() {
    if (serviceIsActive(&g_idle)) serviceClose(&g_idle);
    g_idle_reported = 0;
}

void threadMain(void*) {
    u64 idle_since = 0;
    for (;;) {
        mutexLock(&g_mx);
        bool jumped = g_jump, play = g_jump_play, drop = g_drop_decoder;
        u32 jid = g_jump_id, jms = g_jump_ms;
        g_jump = false;
        g_drop_decoder = false;
        u8 state = g_state;
        bool vol = g_volume_dirty;
        if (g_eq_dirty) {  // (a new equalizer: taken up between buffers)
            g_eq = g_eq_next;
            g_eq_dirty = false;
        }
        u64 now = nowNs();
        bool save_state = g_save_state_at && now >= g_save_state_at + SAVE_DELAY_NS;
        bool save_queue = g_save_queue_at && now >= g_save_queue_at + SAVE_DELAY_NS;
        if (save_state) g_save_state_at = 0;
        if (save_queue) g_save_queue_at = 0;
        mutexUnlock(&g_mx);

        if (vol) applyVolume();
        if (save_queue) saveQueue();
        if (save_state) saveState();
        if (jumped) {
            int left = outFlush();
            eq_reset(&g_eq);  // (the filters' past is another place's)
            g_end_of_queue = false;
            if (drop) decClose();
            if (play) {
                bool same = g_dec && g_dec_id == jid;
                bool ok = same && decSeek(jms);
                if (!ok) ok = decOpenFrom(jid, jms);
                if (jms)
                    note("seek: to %u ms: %s%s, now at %u ms; %d buffers not flushed", jms, same ? "same track" : "opened",
                         same && !ok ? " (seek failed)" : "", ok ? g_dec_start_ms : 0, left);
                if (!ok) {
                    mutexLock(&g_mx);
                    g_state = MelodyState_Stopped;
                    mutexUnlock(&g_mx);
                    continue;
                }
                mutexLock(&g_mx);
                g_cur = g_dec_id;
                g_cur_ms = g_dec_start_ms;
                g_track_id = g_dec_decoding;
                mutexUnlock(&g_mx);
            }
        }

        if (state != MelodyState_Playing) {
            if (!idle_since) idle_since = now;
            if (now - idle_since > IDLE_RELEASE_NS && (g_dec || g_out_open)) {
                decClose();
                outClose();
            }
            letSleep();
            mutexLock(&g_mx);
            if (!g_jump && g_state != MelodyState_Playing) condvarWaitTimeout(&g_cv, &g_mx, 1'000'000'000ULL);
            mutexUnlock(&g_mx);
            continue;
        }
        idle_since = 0;
        keepAwake(now);

        if (!outOpen()) {
            svcSleepThread(100'000'000);
            continue;
        }
        if (!g_dec && !g_end_of_queue) {  // (playing again after the card was let go)
            mutexLock(&g_mx);
            u32 id = g_cur, ms = g_cur_ms;
            mutexUnlock(&g_mx);
            if (!decOpenFrom(id, ms)) g_end_of_queue = true;
        }
        fill();
        if (g_in_flight > 0) {
            waitReleased(20'000'000ULL);
        } else if (g_end_of_queue) {  // everything's played
            mutexLock(&g_mx);
            g_state = MelodyState_Stopped;
            g_cur = firstId();
            g_cur_ms = 0;
            mutexUnlock(&g_mx);
            g_end_of_queue = false;
            decClose();
            saveState();
        } else {
            svcSleepThread(20'000'000);
        }
    }
}

Thread g_thread;

bool isAudio(const char* name) {
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    return !strcasecmp(dot, ".flac") || !strcasecmp(dot, ".opus") || !strcasecmp(dot, ".ogg") || !strcasecmp(dot, ".oga");
}

// Empty the queue (under g_mx)
void clearLocked() {
    g_q.clear();
    g_order.clear();
    for (Meta& m : g_meta) m.id = 0;
    g_state = MelodyState_Stopped;
    g_cur = 0;
    g_cur_ms = 0;
    g_error = 0;
    g_drop_decoder = true;
    jump(0, 0, false);
}

// Adding to the queue (under g_mx): what's queued already isn't added again
// (adding the folder of the track playing adds the rest of it). Count what
// was added, and where in the play order it starts.
struct Adding {
    size_t from = 0;  // its first place in the play order
    u32 added = 0;
    bool full = false;
};

void beginAdding(Adding& a) {
    a.from = g_order.size();
    a.added = 0;
    a.full = false;
}

void addOne(Adding& a, std::string_view path) {
    if (a.full || path.size() >= MELODY_PATH_MAX) return;
    if (g_q.full()) {
        a.full = true;
        return;
    }
    if (u32 id = g_q.add(path)) {
        g_order.push_back(id);
        a.added++;
    }
}

// The new tracks into the play order (shuffled among themselves when
// shuffling); when nothing was playing, the first of them plays. (under g_mx)
void endAdding(Adding& a) {
    if (!a.added) return;
    if (g_shuffle)
        for (size_t i = g_order.size() - 1; i > a.from; i--) std::swap(g_order[i], g_order[a.from + rnd() % (i - a.from + 1)]);
    if (g_state == MelodyState_Stopped) {
        g_cur = g_order[a.from];
        g_cur_ms = 0;
        g_state = MelodyState_Playing;
        jump(g_cur, 0, true);
    }
}

// The music under dir, a folder at a time (files sorted, then its folders,
// at most a few levels down): only one folder's names are held at once
void scan(const std::string& dir, Adding& a, int depth) {
    // (the file system's own listing, not readdir: it has the sizes, and an empty file can't play)
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    char p[FS_MAX_PATH] = {};
    strncpy(p, dir.c_str() + 5, sizeof(p) - 1);
    FsDir d;
    if (!fs || R_FAILED(fsFsOpenDirectory(fs, p, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d))) return;
    std::vector<std::string> files, dirs;
    static FsDirectoryEntry got[4];  // (only the IPC thread scans, and a folder is read before the next one)
    s64 n = 0;
    // (a folder's names are held at once: not without end)
    while (files.size() + dirs.size() < 512 && R_SUCCEEDED(fsDirRead(&d, &n, 4, got)) && n > 0) {
        for (s64 i = 0; i < n; i++) {
            const FsDirectoryEntry& e = got[i];
            if (e.name[0] == '.') continue;
            if (e.type == FsDirEntryType_Dir)
                dirs.emplace_back(e.name);
            else if (e.file_size > 0 && isAudio(e.name))
                files.emplace_back(e.name);
        }
    }
    fsDirClose(&d);
    auto byName = [](const std::string& x, const std::string& y) { return strcasecmp(x.c_str(), y.c_str()) < 0; };
    std::sort(files.begin(), files.end(), byName);
    std::sort(dirs.begin(), dirs.end(), byName);
    mutexLock(&g_mx);
    for (const std::string& f : files) addOne(a, dir + "/" + f);
    bool full = a.full;
    mutexUnlock(&g_mx);
    files.clear();
    files.shrink_to_fit();
    if (full || depth >= 8) return;
    for (const std::string& sub : dirs) scan(dir + "/" + sub, a, depth + 1);
}

}  // namespace

void start() {
    g_rng ^= (u32)armGetSystemTick();
    randomGet(&g_rng, sizeof(g_rng));
    if (!g_rng) g_rng = 1;
    g_q.init(POOL_BYTES, MAX_TRACKS);
    g_order.reserve(MAX_TRACKS);
    eq_off(&g_eq);
    load();
    if (!g_eq_name.empty()) setEq(std::string(g_eq_name).c_str(), false);  // (the one from last time)
    for (Buf& b : g_bufs) {
        b.ab.buffer = aligned_alloc(0x1000, FRAMES * 4);
        b.ab.buffer_size = FRAMES * 4;
        b.ab.next = nullptr;
        b.queued = false;
    }
    if (R_SUCCEEDED(threadCreate(&g_thread, threadMain, nullptr, nullptr, 0x10000, 0x2C, -2))) threadStart(&g_thread);
}

void status(MelodyStatus* out) {
    mutexLock(&g_mx);
    memset(out, 0, sizeof(*out));
    out->state = g_state;
    out->shuffle = g_shuffle;
    out->repeat = g_repeat;
    out->volume = g_volume;
    out->count = (u32)g_q.size();
    int i = indexOf(g_cur);
    out->index = i < 0 ? 0 : (u32)i;
    int p = orderPos(g_cur);
    out->order = p < 0 ? 0 : (u32)p;
    out->position_ms = g_cur_ms;
    Meta* m = findMeta(g_cur);
    out->duration_ms = m ? m->dur_ms : 0;
    out->track_id = g_track_id ^ (g_cur << 16);  // (also changes with the track picked while paused)
    out->error = g_error;
    mutexUnlock(&g_mx);
}

void track(MelodyTrack* out) {
    memset(out, 0, sizeof(*out));
    mutexLock(&g_mx);
    u32 id = g_cur;
    if (Meta* m = findMeta(id)) {
        *out = m->info;
        mutexUnlock(&g_mx);
        return;
    }
    std::string path = pathOf(id);
    mutexUnlock(&g_mx);
    if (path.empty()) return;
    // not played yet (paused since the Switch started): read its tags now
    Tags t = {};
    {
        SdFile f(16 << 10);
        if (f.open(path.c_str())) tags_read(f.stream(), &t, nullptr, 0);
    }
    mutexLock(&g_mx);
    if (indexOf(id) >= 0) {
        putMeta(id, path, t);
        *out = findMeta(id)->info;
    }
    mutexUnlock(&g_mx);
}

void control(u32 what) {
    bool save = false;
    mutexLock(&g_mx);
    if (!g_q.size()) {
        mutexUnlock(&g_mx);
        return;
    }
    bool playing = g_state == MelodyState_Playing;
    if (what == MelodyControl_Toggle) what = playing ? MelodyControl_Pause : MelodyControl_Play;
    switch (what) {
        case MelodyControl_Play:
            if (!playing) {
                if (indexOf(g_cur) < 0) g_cur = firstId(), g_cur_ms = 0;
                g_state = MelodyState_Playing;
                jump(g_cur, g_cur_ms, true);
            }
            break;
        case MelodyControl_Pause:
            if (playing) {
                g_state = MelodyState_Paused;
                jump(g_cur, g_cur_ms, false);
                save = true;
            }
            break;
        case MelodyControl_Next:
        case MelodyControl_Prev: {
            u32 id;
            if (what == MelodyControl_Prev && g_cur_ms > 3000)
                id = g_cur;
            else
                id = what == MelodyControl_Next ? nextId(g_cur, false) : prevId(g_cur);
            if (!id) {  // past the end, with repeat off
                g_state = MelodyState_Stopped;
                g_cur = firstId();
                g_cur_ms = 0;
                jump(g_cur, 0, false);
            } else {
                g_cur = id;
                g_cur_ms = 0;
                if (g_state == MelodyState_Stopped) g_state = MelodyState_Paused;
                jump(g_cur, 0, playing);
            }
            save = true;
            break;
        }
    }
    mutexUnlock(&g_mx);
    if (save) saveState();
}

void seek(u32 ms) {
    mutexLock(&g_mx);
    if (g_q.size()) {
        g_cur_ms = ms;
        if (g_state == MelodyState_Stopped) g_state = MelodyState_Paused;
        jump(g_cur, ms, g_state == MelodyState_Playing);
    }
    mutexUnlock(&g_mx);
}

void setVolume(u32 v) {
    mutexLock(&g_mx);
    g_volume = (u8)std::min(v, 100u);
    g_volume_dirty = true;
    g_save_state_at = nowNs();
    condvarWakeAll(&g_cv);
    mutexUnlock(&g_mx);
}

void setShuffle(bool on) {
    mutexLock(&g_mx);
    g_shuffle = on;
    makeOrder();
    mutexUnlock(&g_mx);
    saveState();
}

void setRepeat(u32 mode) {
    mutexLock(&g_mx);
    g_repeat = (u8)std::min(mode, 2u);
    mutexUnlock(&g_mx);
    saveState();
}

u32 enqueue(const char* path) {
    Card c;
    if (!c.ok) return 0;
    std::string p = path;
    while (p.size() > 6 && p.back() == '/') p.pop_back();
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return 0;
    Adding a;
    mutexLock(&g_mx);
    beginAdding(a);
    if (!S_ISDIR(st.st_mode)) {
        if (st.st_size > 0 && isAudio(p.c_str())) addOne(a, p);  // (an empty file can't play)
    } else {
        mutexUnlock(&g_mx);
        scan(p, a, 0);
        mutexLock(&g_mx);
    }
    endAdding(a);
    mutexUnlock(&g_mx);
    notePeak("added to the queue");
    if (a.added) {
        saveQueue();
        saveState();
    }
    return a.added;
}

void clear() {
    mutexLock(&g_mx);
    clearLocked();
    mutexUnlock(&g_mx);
    saveQueue();
    saveState();
}

u32 queueNames(u32 first, MelodyName* out, u32 max) {
    mutexLock(&g_mx);
    u32 n = 0;
    for (u32 i = first; i < g_q.size() && n < max; i++, n++) {
        memset(out[n].name, 0, sizeof(out[n].name));
        std::string_view b = baseName(g_q.path(i));
        memcpy(out[n].name, b.data(), std::min(b.size(), sizeof(out[n].name) - 1));
    }
    mutexUnlock(&g_mx);
    return n;
}

void playIndex(u32 index) {
    mutexLock(&g_mx);
    if (index < g_q.size()) {
        g_cur = g_q.id(index);
        g_cur_ms = 0;
        g_state = MelodyState_Playing;
        jump(g_cur, 0, true);
    }
    mutexUnlock(&g_mx);
    saveState();
}

u32 moveTrack(u32 index, s32 delta) {
    mutexLock(&g_mx);
    s64 to = (s64)index + delta;
    if (index >= g_q.size() || to < 0 || to >= (s64)g_q.size()) {
        mutexUnlock(&g_mx);
        return index;
    }
    g_q.move(index, (size_t)to);  // (the ids go with the tracks: what's playing and what's decoded don't change)
    if (!g_shuffle) makeOrder();  // (shuffled, the play order stays as it was)
    g_save_queue_at = nowNs();
    mutexUnlock(&g_mx);
    return (u32)to;
}

void removeTrack(u32 index) {
    mutexLock(&g_mx);
    if (index >= g_q.size()) {
        mutexUnlock(&g_mx);
        return;
    }
    u32 id = g_q.id(index);
    if (g_q.size() == 1) {  // the last one: as Clear
        clearLocked();
        mutexUnlock(&g_mx);
        saveQueue();
        saveState();
        return;
    }
    bool playing = g_state == MelodyState_Playing;
    u32 next = id == g_cur ? nextId(id, false) : 0;  // (before it's gone from the order)
    g_q.erase(index);
    g_order.erase(std::remove(g_order.begin(), g_order.end(), id), g_order.end());
    if (Meta* m = findMeta(id)) m->id = 0;
    if (id == g_cur) {  // the track playing: on to the next one
        g_drop_decoder = true;
        g_cur_ms = 0;
        if (next && next != id) {
            g_cur = next;
            jump(next, 0, playing);
        } else {  // (it was the last, with repeat off)
            g_cur = firstId();
            if (playing) g_state = MelodyState_Stopped;
            jump(g_cur, 0, false);
        }
    } else if (id == g_dec_shared) {
        // the next track, decoded ahead: back to where the one playing is
        g_drop_decoder = true;
        jump(g_cur, g_cur_ms, playing);
    }
    g_save_queue_at = nowNs();
    mutexUnlock(&g_mx);
    if (id == g_cur) saveState();
}

u32 savePlaylist(const char* name) {
    std::string file = playlistPath(name);
    if (file.empty()) {
        note("save playlist \"%s\": not a name a file can have", name);
        return MelodySave_BadName;
    }
    Card c;
    if (!c.ok) {
        note("save playlist \"%s\": the SD card couldn't be opened", name);
        return MelodySave_NoCard;
    }
    configDir();
    errno = 0;
    if (mkdir(MELODY_PLAYLIST_DIR, 0777) != 0 && errno != EEXIST)
        note("save playlist: can't make %s (errno %d)", MELODY_PLAYLIST_DIR, errno);
    errno = 0;
    size_t i = 0;
    auto next = [&i](std::string& out) {  // (one path at a time, under the lock only for that)
        mutexLock(&g_mx);
        bool more = i < g_q.size();
        if (more) out.assign(g_q.path(i++));
        mutexUnlock(&g_mx);
        return more;
    };
    if (!writePlaylist(file, next)) {
        note("save playlist: can't write %s (errno %d)", file.c_str(), errno);
        return MelodySave_WriteFailed;
    }
    return MelodySave_Ok;
}

u32 loadPlaylist(const char* name, bool append) {
    std::string file = playlistPath(name);
    if (file.empty()) return 0;
    Card c;
    if (!c.ok) return 0;
    struct stat st;
    if (stat(file.c_str(), &st) != 0) return 0;
    Adding a;
    mutexLock(&g_mx);
    if (!append) clearLocked();
    beginAdding(a);
    mutexUnlock(&g_mx);
    readPlaylist(file, [&a](const std::string& p) {
        mutexLock(&g_mx);
        addOne(a, p);
        bool more = !a.full;
        mutexUnlock(&g_mx);
        return more;
    });
    mutexLock(&g_mx);
    endAdding(a);
    mutexUnlock(&g_mx);
    if (a.added || !append) {
        saveQueue();
        saveState();
    }
    return a.added;
}

MelodyEqResult setEqualizer(const char* name) { return setEq(name, true); }

MelodyEqResult setEq(const char* name, bool save) {
    MelodyEqResult r = {0, 0};
    static Eq eq;  // (1 KB or so: kept off the IPC thread's stack)
    eq_off(&eq);
    std::string n = name;
    if (!n.empty()) {
        if (n.find('/') != std::string::npos || n.find('\\') != std::string::npos || n[0] == '.') {
            r.filters = -1;
            return r;
        }
        std::string path = std::string(MELODY_EQ_DIR) + "/" + n;
        Card c;
        FILE* f = c.ok ? fopen(path.c_str(), "rb") : nullptr;
        char* text = nullptr;
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (size >= 0 && size <= (32 << 10) && (text = (char*)malloc((size_t)size + 1))) {
                size_t got = fread(text, 1, (size_t)size, f);
                text[got] = 0;
            }
            fclose(f);
        }
        if (!text) {
            note("equalizer \"%s\": couldn't read it", name);
            r.filters = -1;
            return r;
        }
        r.filters = eq_parse(&eq, text, 48000);
        r.skipped = (u32)eq.skipped;
        free(text);
    }
    mutexLock(&g_mx);
    g_eq_next = eq;
    g_eq_dirty = true;
    g_eq_name = n;
    condvarWakeAll(&g_cv);
    mutexUnlock(&g_mx);
    if (save) saveState();
    return r;
}

void equalizer(MelodyName* out) {
    memset(out, 0, sizeof(*out));
    mutexLock(&g_mx);
    strncpy(out->name, g_eq_name.c_str(), sizeof(out->name) - 1);
    mutexUnlock(&g_mx);
}

}  // namespace player
