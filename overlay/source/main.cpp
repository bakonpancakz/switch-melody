// Melody's overlay (libultrahand): what's playing, with its cover, a seek bar
// and Back / Play-Pause / Next; volume, shuffle and repeat; adding files or
// folders (from sdmc:/Music on) to the playlist, and clearing it. The music
// itself is the sysmodule's: this only asks it over IPC (client.cpp).
#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <cmath>
#include <string>
#include <vector>

#include "client.h"
#include "cd_picture.h"
#include "cover.h"
#include "btpatch.h"
#include "texts.h"

namespace {

constexpr int COVER = 112;
constexpr u64 POLL_NS = 250'000'000ULL;
constexpr u64 RECONNECT_NS = 1'000'000'000ULL;

// What the sysmodule last said
struct Now {
    bool running = false;
    MelodyStatus st = {};
    MelodyTrack tr = {};
    u64 polled_ns = 0, tried_ns = 0;
    u32 track_id = ~0u;
    bool has_cover = false;
    std::vector<u8> cover;
    u64 title_since_ns = 0;  // (the title's scrolling starts again with each track)
} g_now;

void poll(bool force = false) {
    u64 now = ult::nowNs();
    if (!client::connected()) {
        if (!force && now - g_now.tried_ns < RECONNECT_NS) return;
        g_now.tried_ns = now;
        if (!client::connect()) {
            g_now.running = false;
            return;
        }
    }
    if (!force && now - g_now.polled_ns < POLL_NS) return;
    g_now.polled_ns = now;
    g_now.running = client::status(&g_now.st);
    if (!g_now.running) return;
    if (g_now.st.track_id != g_now.track_id) {
        g_now.track_id = g_now.st.track_id;
        g_now.title_since_ns = now;
        memset(&g_now.tr, 0, sizeof(g_now.tr));
        if (g_now.st.count) client::track(&g_now.tr);
        g_now.has_cover = g_now.tr.path[0] && coverLoad(g_now.tr.path, COVER, g_now.cover);
    }
}

// Where the track is now (moving on between polls while it plays)
u32 positionMs() {
    u32 p = g_now.st.position_ms;
    if (g_now.st.state == MelodyState_Playing) p += (u32)((ult::nowNs() - g_now.polled_ns) / 1'000'000ULL);
    if (g_now.st.duration_ms && p > g_now.st.duration_ms) p = g_now.st.duration_ms;
    return p;
}

std::string clock(u32 ms) {
    u32 s = ms / 1000;
    char b[16];
    if (s >= 3600)
        snprintf(b, sizeof(b), "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
    else
        snprintf(b, sizeof(b), "%u:%02u", s / 60, s % 60);
    return b;
}

using tsl::Color;
using tsl::gfx::Renderer;

// Colours from the Ultrahand theme (read when the overlay starts), so this
// looks like the menu around it
#define WHITE tsl::defaultTextColor
#define ACCENT tsl::trackBarFullColor   // (the volume bar's fill: the seek bar matches it)
#define ACCENT_TEXT tsl::onTextColor    // (as an "On" is written)
#define TRACK tsl::trackBarEmptyColor
#define TILE tsl::tableBGColor          // (see-through, as the theme has it)
const Color GREY = Color(0xC, 0xC, 0xC, 0xF);
const Color ICON = Color(0x7, 0x7, 0x7, 0xF);  // the small dark grey icons by Artist and Album

// The colour of what's picked, going back and forth as the theme's highlight does
Color pulse() {
    float t = (std::sin(ult::nowNs() / 1e9f * 2.0f * 3.14159265f * 0.6f) + 1.0f) / 2.0f;
    return tsl::lerpColor(tsl::highlightColor1, tsl::highlightColor2, t);
}

// Shapes here are centred on pixel corners, as tsl's circles of radius 4 and
// more are (smaller ones are centred on a pixel, half a pixel off): even
// sizes, and (x, y) is the corner in their middle.

// A triangle pointing right (dir 1) or left (-1), w wide and h (even) high,
// from x, its middle at cy
void triangle(Renderer* r, s32 x, s32 cy, s32 w, s32 h, int dir, Color c) {
    for (s32 i = 0; i < h; i++) {
        float d = std::fabs(i - h / 2.0f + 0.5f);
        s32 len = (s32)(w * (1.0f - d / (h / 2.0f)) + 0.5f);
        if (len <= 0) continue;
        r->drawRect(dir > 0 ? x : x + w - len, cy - h / 2 + i, len, 1, r->a(c));
    }
}

// A frame t thick around (x, y, w, h), square-cornered
void squareBorder(Renderer* r, s32 x, s32 y, s32 w, s32 h, s32 t, Color c) {
    r->drawRect(x, y, w, t, r->a(c));
    r->drawRect(x, y + h - t, w, t, r->a(c));
    r->drawRect(x, y + t, t, h - 2 * t, r->a(c));
    r->drawRect(x + w - t, y + t, t, h - 2 * t, r->a(c));
}

// A round dot d (even) pixels across
void dot(Renderer* r, s32 cx, s32 cy, s32 d, Color c) {
    float rad = d / 2.0f;
    for (s32 i = 0; i < d; i++) {
        float py = i - rad + 0.5f;
        s32 half = (s32)(std::sqrt(std::max(0.0f, rad * rad - py * py)) + 0.5f);
        if (half > 0) r->drawRect(cx - half, cy - d / 2 + i, 2 * half, 1, r->a(c));
    }
}

// A person: a head and shoulders, 14 wide
void personIcon(Renderer* r, s32 x, s32 y, Color c) {
    r->drawCircle(x + 7, y + 4, 4, true, r->a(c));
    r->drawUniformRoundedRect(x + 1, y + 9, 12, 7, r->a(c));
}

// A record: a disc with a hole, 14 wide
void discIcon(Renderer* r, s32 x, s32 y, Color c) {
    r->drawCircle(x + 7, y + 8, 7, true, r->a(c));
    dot(r, x + 7, y + 8, 4, Color(0x1, 0x1, 0x1, 0xF));
}

// text in a w-wide box at x, scrolling sideways when it doesn't fit
void scrollingText(Renderer* r, const std::string& text, s32 x, s32 baseline, s32 w, u32 size, Color c, u64 since_ns) {
    s32 tw = r->getTextDimensions(text, false, size).first;
    // the box, within the list's own clip (a scissor set here replaces the
    // list's, so text scrolled up would otherwise be drawn over the header)
    s32 top = baseline - (s32)size - 4, bottom = baseline + 8;
    top = std::max<s32>(top, ult::activeHeaderHeight);
    bottom = std::min<s32>(bottom, tsl::cfg::FramebufferHeight - 73);
    if (bottom <= top) return;
    r->enableScissoring(x, top, w, bottom - top);
    if (tw <= w) {
        r->drawString(text, false, x, baseline, size, r->a(c));
    } else {
        const float speed = 40.0f, pause = 2.0f, gap = 48.0f;
        float cycle = (tw + gap) / speed;
        float t = std::fmod((ult::nowNs() - since_ns) / 1e9f, pause + cycle);
        s32 off = t < pause ? 0 : (s32)((t - pause) * speed);
        r->drawString(text, false, x - off, baseline, size, r->a(c));
        r->drawString(text, false, x - off + tw + (s32)gap, baseline, size, r->a(c));
    }
    r->disableScissoring();
}

// text broken into lines no wider than w (at spaces; a word longer than a line stays whole)
std::vector<std::string> wrap(Renderer* r, const std::string& text, s32 w, u32 size) {
    std::vector<std::string> lines;
    std::string line;
    size_t at = 0;
    while (at < text.size()) {
        size_t sp = text.find(' ', at);
        std::string word = text.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
        std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && r->getTextDimensions(next, false, size).first > w) {
            lines.push_back(line);
            line = word;
        } else {
            line = next;
        }
        if (sp == std::string::npos) break;
        at = sp + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// The cover, and Title / Artist / Album beside it (not focusable)
class NowPlaying : public tsl::elm::Element {
public:
    NowPlaying() { m_isItem = false; }
    void layout(u16, u16, u16, u16) override {}
    void draw(Renderer* r) override {
        s32 x = getX() + 6, y = getY() + 14, w = getWidth() - 12;
        if (g_now.has_cover && g_now.cover.size() == (size_t)COVER * COVER * 2)
            r->drawBitmapRGBA4444(x, y, COVER, COVER, g_now.cover.data());
        else {  // no cover: a CD
            r->drawBitmapRGBA4444(x, y, COVER, COVER, cdPicture(COVER).data());
        }
        s32 tx = x + COVER + 16, tw = x + w - tx;
        if (!g_now.running) {
            scrollingText(r, txt::NOT_RUNNING, tx, y + 30, tw, 20, WHITE, g_now.title_since_ns);
            scrollingText(r, txt::NOT_RUNNING_1, tx, y + 60, tw, 16, GREY, g_now.title_since_ns);
            scrollingText(r, txt::NOT_RUNNING_2, tx, y + 82, tw, 16, GREY, g_now.title_since_ns);
            return;
        }
        if (!g_now.st.count) {
            scrollingText(r, txt::NOTHING_QUEUED, tx, y + 30, tw, 20, WHITE, g_now.title_since_ns);
            scrollingText(r, txt::NOTHING_QUEUED_HINT, tx, y + 60, tw, 16, GREY, g_now.title_since_ns);
            return;
        }
        scrollingText(r, g_now.tr.title, tx, y + 30, tw, 22, WHITE, g_now.title_since_ns);
        personIcon(r, tx, y + 50, ICON);
        scrollingText(r, g_now.tr.artist[0] ? g_now.tr.artist : txt::UNKNOWN_ARTIST, tx + 22, y + 64, tw - 22, 17, GREY,
                      g_now.title_since_ns);
        discIcon(r, tx, y + 78, ICON);
        scrollingText(r, g_now.tr.album[0] ? g_now.tr.album : txt::UNKNOWN_ALBUM, tx + 22, y + 92, tw - 22, 17, GREY,
                      g_now.title_since_ns);
        if (client::outdated()) {  // (the overlay was updated, the sysmodule not loaded again yet)
            scrollingText(r, txt::RESTART_TO_UPDATE, tx, y + 118, tw, 14, ACCENT_TEXT, g_now.title_since_ns);
            return;
        }
        std::string pos = fmt(txt::TRACK_COUNT, g_now.st.order + 1, g_now.st.count);
        scrollingText(r, pos, tx, y + 118, tw, 14, GREY, g_now.title_since_ns);
    }
};

// The seek bar. A picks up the playhead, left/right move it (5 s, held:
// faster), A again seeks there; moving off the bar puts it back. On the touch
// screen: drag it, and it seeks when let go of.
class SeekBar : public tsl::elm::Element {
public:
    void layout(u16, u16, u16, u16) override {}
    Element* requestFocus(Element*, tsl::FocusDirection dir) override {
        // (the controls get the focus first, not this)
        return dir == tsl::FocusDirection::None || !g_now.st.count ? nullptr : this;
    }
    bool onClick(u64) override { return false; }
    // (no box around it: the bigger knob shows it's picked)
    void drawHighlight(Renderer*) override {}
    void drawFocusBackground(Renderer*) override {}

    void setFocused(bool focused) override {
        Element::setFocused(focused);
        if (!focused) pending_ = -1;  // (not seeking after all)
    }

    bool handleInput(u64 down, u64 held, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        u64 now = ult::nowNs();
        if (down & HidNpadButton_A) {
            if (pending_ < 0)
                pending_ = positionMs();
            else
                apply();
            return true;
        }
        int dir = (held & KEY_LEFT) ? -1 : (held & KEY_RIGHT) ? 1 : 0;
        if (!dir) return false;
        if (pending_ < 0) return true;  // (not picked up: left/right do nothing here)
        bool first = down & (KEY_LEFT | KEY_RIGHT);
        if (first) held_since_ = now;
        if (first || (now - held_since_ > 400'000'000ULL && now - last_step_ > 80'000'000ULL)) {
            s64 step = (now - held_since_ > 2'000'000'000ULL) ? 20000 : 5000;
            move(pending_ + dir * step);
            last_step_ = now;
        }
        return true;
    }

    bool onTouch(tsl::elm::TouchEvent event, s32 cx, s32, s32, s32, s32 ix, s32 iy) override {
        if (!g_now.st.count || !g_now.st.duration_ms) return false;
        if (!inBounds(ix, iy)) return false;  // (a touch that started somewhere else)
        if (event == tsl::elm::TouchEvent::Release) {
            if (pending_ >= 0) apply();
            return true;
        }
        if (event == tsl::elm::TouchEvent::Touch || event == tsl::elm::TouchEvent::Hold) {
            s32 x = getX() + 10, w = getWidth() - 20;
            float f = std::clamp((float)(cx - x) / w, 0.0f, 1.0f);
            move((s64)(f * g_now.st.duration_ms));
            return true;
        }
        return false;
    }

    void draw(Renderer* r) override {
        s32 x = getX() + 10, w = getWidth() - 20, y = getY() + 22;
        u32 dur = g_now.st.duration_ms;
        bool picked = pending_ >= 0;
        u32 pos = picked ? (u32)pending_ : positionMs();
        float f = dur ? std::min(1.0f, (float)pos / dur) : 0.0f;
        r->drawUniformRoundedRect(x, y, w, 6, r->a(TRACK));
        if (f > 0) r->drawUniformRoundedRect(x, y, std::max<s32>(6, (s32)(w * f)), 6, r->a(ACCENT));
        if (picked) r->drawCircle(x + (s32)(w * f), y + 3, 12, true, r->a(ACCENT));
        r->drawCircle(x + (s32)(w * f), y + 3, m_focused ? 9 : 6, true, r->a(WHITE));
        r->drawString(clock(pos), false, x, y + 34, 15, r->a(picked ? ACCENT_TEXT : GREY));
        std::string d = dur ? clock(dur) : "--:--";
        s32 dw = r->getTextDimensions(d, false, 15).first;
        r->drawString(d, false, x + w - dw, y + 34, 15, r->a(GREY));
        if (m_focused) {
            std::string hint = picked ? txt::SEEK_PICKED_HINT : txt::SEEK_HINT;
            s32 hw = r->getTextDimensions(hint, false, 15).first;
            r->drawString(hint, false, x + (w - hw) / 2, y + 34, 15, r->a(picked ? ACCENT_TEXT : ICON));
        }
    }

private:
    void move(s64 ms) {
        u32 dur = g_now.st.duration_ms;
        pending_ = std::max<s64>(0, ms);
        if (dur) pending_ = std::min<s64>(pending_, dur > 1000 ? dur - 1000 : 0);
    }

    void apply() {
        client::seek((u32)pending_);
        g_now.st.position_ms = (u32)pending_;
        g_now.polled_ns = ult::nowNs();
        pending_ = -1;
    }

private:
    s64 pending_ = -1;
    u64 held_since_ = 0, last_step_ = 0;
};

// Back, Play/Pause, Next: left/right picks one, A presses it
class Controls : public tsl::elm::Element {
public:
    void layout(u16, u16, u16, u16) override {}
    Element* requestFocus(Element*, tsl::FocusDirection) override { return this; }
    // (no box around them: the picked button has a ring)
    void drawHighlight(Renderer*) override {}
    void drawFocusBackground(Renderer*) override {}

    bool onClick(u64 keys) override {
        if (!(keys & HidNpadButton_A)) return false;
        if (sel_ == 0) client::control(MelodyControl_Prev);
        if (sel_ == 1) client::control(MelodyControl_Toggle);
        if (sel_ == 2) client::control(MelodyControl_Next);
        poll(true);
        return true;
    }

    bool handleInput(u64 down, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        if (down & KEY_LEFT) {
            sel_ = std::max(0, sel_ - 1);
            return true;
        }
        if (down & KEY_RIGHT) {
            sel_ = std::min(2, sel_ + 1);
            return true;
        }
        return false;
    }

    void draw(Renderer* r) override {
        s32 cx = getX() + getWidth() / 2, cy = getY() + getHeight() / 2;
        const s32 xs[3] = {cx - 96, cx, cx + 96};
        for (int i = 0; i < 3; i++) {
            bool picked = m_focused && sel_ == i;
            s32 rad = i == 1 ? 28 : 22;
            if (picked) r->drawCircle(xs[i], cy, rad + 4, true, r->a(pulse()));
            r->drawCircle(xs[i], cy, rad, true, r->a(i == 1 ? WHITE : TILE));
        }
        Color dark = Color(0x1, 0x1, 0x1, 0xF);
        // Back: a bar and a triangle to the left; Next: the other way round
        r->drawRect(xs[0] - 8, cy - 8, 3, 16, r->a(WHITE));
        triangle(r, xs[0] - 5, cy, 13, 16, -1, WHITE);
        triangle(r, xs[2] - 8, cy, 13, 16, 1, WHITE);
        r->drawRect(xs[2] + 5, cy - 8, 3, 16, r->a(WHITE));
        if (g_now.st.state == MelodyState_Playing) {
            r->drawRect(xs[1] - 8, cy - 11, 6, 22, r->a(dark));
            r->drawRect(xs[1] + 2, cy - 11, 6, 22, r->a(dark));
        } else {  // (a third of the way in is the middle, to the eye)
            triangle(r, xs[1] - 6, cy, 19, 24, 1, dark);
        }
    }

private:
    int sel_ = 1;
};

const char* repeatName(u32 m) { return m == MelodyRepeat_One ? txt::REPEAT_ONE : m == MelodyRepeat_All ? txt::REPEAT_ALL : txt::REPEAT_OFF; }

// Where Add to Playlist opens: the folder picked with "Set as start folder"
// (in overlay.ini), else sdmc:/Music, else the card itself
const char* OVERLAY_INI = MELODY_CONFIG_DIR "/overlay.ini";

bool isDir(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string startFolder() {
    if (FILE* f = fopen(OVERLAY_INI, "rb")) {
        char line[MELODY_PATH_MAX + 32];
        std::string dir;
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "start_folder=", 13)) dir = line + 13;
        }
        fclose(f);
        if (!dir.empty() && isDir(dir)) return dir;
    }
    return isDir(MELODY_MUSIC_DIR) ? std::string(MELODY_MUSIC_DIR) : std::string("sdmc:/");
}

bool saveStartFolder(const std::string& dir) {
    mkdir("sdmc:/config", 0777);
    mkdir(MELODY_CONFIG_DIR, 0777);
    FILE* f = fopen(OVERLAY_INI, "wb");
    if (!f) return false;
    fprintf(f, "start_folder=%s\n", dir.c_str());
    return fclose(f) == 0;
}

bool isAudio(const char* name) {
    const char* dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".flac") || !strcasecmp(dot, ".opus") || !strcasecmp(dot, ".ogg") || !strcasecmp(dot, ".oga"));
}

// Why a file can't play, from its size and first bytes (nullptr: it looks
// fine; the sysmodule's log says more if it still won't). path is on the card,
// without "sdmc:".
const char* whyUnplayable(FsFileSystem* fs, const char* path, s64 size) {
    if (size == 0) return txt::CANT_QUEUE_EMPTY;
    FsFile f;
    if (R_FAILED(fsFsOpenFile(fs, path, FsOpenMode_Read, &f))) return nullptr;  // (the sysmodule may do better)
    u8 h[64] = {};
    u64 got = 0;
    if (R_FAILED(fsFileRead(&f, 0, h, sizeof(h), FsReadOption_None, &got))) got = 0;
    if (got >= 10 && !memcmp(h, "ID3", 3)) {  // an ID3v2 tag in front of a FLAC file
        s64 at = 10 + (((h[6] & 0x7F) << 21) | ((h[7] & 0x7F) << 14) | ((h[8] & 0x7F) << 7) | (h[9] & 0x7F));
        if (h[5] & 0x10) at += 10;
        got = 0;
        if (at < size && R_FAILED(fsFileRead(&f, at, h, 4, FsReadOption_None, &got))) got = 0;
    }
    fsFileClose(&f);
    if (got >= 4 && !memcmp(h, "fLaC", 4)) return nullptr;
    if (got >= 27 && !memcmp(h, "OggS", 4)) {  // (the first packet, after the page's segment table, is OpusHead)
        size_t at = 27 + h[26];
        if (at + 8 > got) return nullptr;  // (too long a table to see from here)
        return memcmp(h + at, "OpusHead", 8) ? txt::CANT_QUEUE_NOT_OPUS : nullptr;
    }
    return txt::CANT_QUEUE_CORRUPT;
}

std::string parentOf(const std::string& dir) {
    size_t s = dir.find_last_of('/');
    if (s == std::string::npos || s <= 5) return "sdmc:/";
    return dir.substr(0, s);
}

// What the browser remembers while the overlay runs: the entry last picked
// in each folder
std::map<std::string, std::string> g_browse_pick;

// A few words at the browser's top, for a moment (else the shortcuts)
std::string g_flash;
u64 g_flash_until = 0;
void flash(const std::string& msg) {
    g_flash = msg;
    g_flash_until = ult::nowNs() + 2'500'000'000ULL;
}

// A line at a page's top: what its buttons do (or, for a moment, what just happened)
class Hint : public tsl::elm::Element {
public:
    explicit Hint(std::string text) : text_(std::move(text)), since_(ult::nowNs()) { m_isItem = false; }
    void layout(u16, u16, u16, u16) override {}
    void draw(Renderer* r) override {
        bool msg = ult::nowNs() < g_flash_until;
        scrollingText(r, msg ? g_flash : text_, getX() + 12, getY() + 28, getWidth() - 24, 18, msg ? ACCENT_TEXT : GREY,
                      msg ? g_flash_until - 2'500'000'000ULL : since_);  // (the flash started 2.5 s before it ends)
    }

private:
    std::string text_;
    u64 since_;
};

// An entry that gets the cursor when its page opens, with the view scrolled to
// it: the list made by pickList() jumps to it. (Turning the first focus down
// in the other entries moved the cursor there but left the view at the top.)
constexpr const char* PICK_JUMP = "\x01pick";  // (no entry's text: only a picked PickEntry matches it)
class PickEntry : public tsl::elm::ListItem {
public:
    PickEntry(const std::string& text, const std::string& value, bool picked) : ListItem(text, value), picked_(picked) {}
    bool matchesJumpCriteria(const std::string& text, const std::string&, bool) const override {
        return picked_ && text == PICK_JUMP;
    }

private:
    bool picked_;
};

// A list that opens on its picked PickEntry (none picked: its first item)
tsl::elm::List* pickList() {
    auto* list = new tsl::elm::List();
    list->jumpToItem(PICK_JUMP);
    return list;
}

// A folder's entry: picked if it had the cursor the last time
class BrowseEntry : public PickEntry {
public:
    BrowseEntry(const std::string& text, const std::string& value, const std::string& dir, const std::string& key)
        : PickEntry(text, value, g_browse_pick[dir] == key), dir_(dir), key_(key) {}
    void setFocused(bool focused) override {
        PickEntry::setFocused(focused);
        if (focused && key_ != "..") g_browse_pick[dir_] = key_;
    }

private:
    std::string dir_, key_;
};

// Go up from dir, with the cursor on the folder just left. (Folders replace
// each other, swapTo: the browser is one page whatever the folder.)
void browseUp(const std::string& dir);

// A folder's music and folders, to add to the playlist. A on a folder opens
// it, A on a file adds it, ".." goes up; Y adds the whole folder, X makes it
// the start folder. B goes up too, inside the start folder; at the start
// folder (or out of it, after "..") it closes the browser.
class BrowseGui : public tsl::Gui {
public:
    explicit BrowseGui(std::string dir) : dir_(std::move(dir)), start_(startFolder()) {}

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(txt::BROWSE_TITLE, dir_.size() > 5 ? dir_.substr(5) : dir_);
        auto* list = pickList();
        std::vector<std::string> dirs, files;
        std::vector<std::pair<std::string, const char*>> bad;  // files that can't play, and why (shown dimmed)
        // (the file system's own listing, not readdir: it has the sizes)
        FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
        char p[FS_MAX_PATH] = {};
        strncpy(p, dir_.c_str() + 5, sizeof(p) - 1);
        FsDir d;
        std::string here = p;
        if (here.empty() || here.back() != '/') here += '/';
        if (fs && R_SUCCEEDED(fsFsOpenDirectory(fs, p, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d))) {
            FsDirectoryEntry got[4];
            s64 n = 0;
            while (dirs.size() + files.size() < 400 && R_SUCCEEDED(fsDirRead(&d, &n, 4, got)) && n > 0) {
                for (s64 i = 0; i < n; i++) {
                    const FsDirectoryEntry& e = got[i];
                    if (e.name[0] == '.') continue;
                    if (e.type == FsDirEntryType_Dir) {
                        dirs.emplace_back(e.name);
                    } else if (isAudio(e.name)) {
                        files.emplace_back(e.name);
                        if (const char* why = whyUnplayable(fs, (here + e.name).c_str(), e.file_size))
                            bad.emplace_back(e.name, why);
                    }
                }
            }
            fsDirClose(&d);
        }
        auto byName = [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        // the cursor goes where it was in this folder, else on its first folder or file
        bool up = dir_ != "sdmc:/";
        std::vector<std::string> keys;
        if (up) keys.push_back("..");
        for (const std::string& d : dirs) keys.push_back("d:" + d);
        for (const std::string& f : files) keys.push_back("f:" + f);
        std::string& pick = g_browse_pick[dir_];
        if (std::find(keys.begin(), keys.end(), pick) == keys.end())
            pick = keys.size() > (size_t)up ? keys[up] : (keys.empty() ? "" : keys[0]);

        list->addItem(new Hint(txt::BROWSE_HINT), 40);
        if (up) {
            auto* item = new BrowseEntry("..", txt::UP, dir_, "..");
            std::string here = dir_;
            item->setClickListener([here](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                std::string from = here;  // (this item goes with this page: nothing of it after the swap)
                browseUp(from);
                return true;
            });
            list->addItem(item);
        }
        std::string base = dir_.back() == '/' ? dir_ : dir_ + "/";
        for (const std::string& d : dirs) {
            auto* item = new BrowseEntry(d, ult::DROPDOWN_SYMBOL, dir_, "d:" + d);
            std::string sub = base + d;
            item->setClickListener([sub](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                std::string to = sub;
                tsl::swapTo<BrowseGui>(to);
                return true;
            });
            list->addItem(item);
        }
        for (const std::string& f : files) {
            auto* item = new BrowseEntry(f, "", dir_, "f:" + f);
            auto it = std::find_if(bad.begin(), bad.end(), [&](const auto& b) { return b.first == f; });
            if (it != bad.end()) {  // (dim, and A only says why)
                item->setTextColor(tsl::offTextColor);  // (the faint grey of an "Off")
                const char* why = it->second;
                item->setClickListener([why](u64 keys) {
                    if (!(keys & HidNpadButton_A)) return false;
                    flash(why);
                    return true;
                });
                list->addItem(item);
                continue;
            }
            std::string path = base + f;
            item->setClickListener([item, path](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                if (client::enqueue(path.c_str()))
                    item->setValue(ult::CHECKMARK_SYMBOL);
                else
                    item->setValue(txt::IN_PLAYLIST, true);
                return true;
            });
            list->addItem(item);
        }
        if (dirs.empty() && files.empty())
            list->addItem(new tsl::elm::ListItem(txt::NO_MUSIC_HERE));
        frame->setContent(list);
        return frame;
    }

    bool handleInput(u64 down, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        if (down & HidNpadButton_B) {
            bool inside = dir_ != start_ && dir_.size() > start_.size() && !dir_.compare(0, start_.size(), start_) &&
                          (start_.back() == '/' || dir_[start_.size()] == '/');
            if (!inside) return false;  // (tesla goes back to the menu)
            std::string from = dir_;
            browseUp(from);
            return true;
        }
        if (down & HidNpadButton_Y) {
            u32 n = client::enqueue(dir_.c_str());
            flash(n == 1 ? txt::ADDED_ONE_TRACK : n ? fmt(txt::ADDED_TRACKS, n) : txt::NOTHING_NEW);
            return true;
        }
        if (down & HidNpadButton_X) {
            flash(saveStartFolder(dir_) ? txt::START_FOLDER_SET : txt::START_FOLDER_FAILED);
            return true;
        }
        return false;
    }

private:
    std::string dir_, start_;
};

void browseUp(const std::string& dir) {
    std::string parent = parentOf(dir);
    g_browse_pick[parent] = "d:" + dir.substr(dir.find_last_of('/') + 1);
    tsl::swapTo<BrowseGui>(parent);
}

// The playlist: A plays a track, X and Y move it up and down, - removes it.
// (Each change makes the page again, with the cursor on the same track.)
class QueueGui : public tsl::Gui {
public:
    explicit QueueGui(s32 focus = -1) : focus_(focus) {}

    tsl::elm::Element* createUI() override {
        poll(true);
        auto* frame = new tsl::elm::OverlayFrame(txt::QUEUE_TITLE, fmt(txt::TRACKS, g_now.st.count));
        auto* list = pickList();
        list->addItem(new Hint(txt::QUEUE_HINT), 40);
        constexpr u32 SHOWN = 200;
        u32 cur = g_now.st.index;
        u32 at = focus_ >= 0 ? (u32)focus_ : cur;
        u32 first = at > 40 ? at - 40 : 0;
        std::vector<MelodyName> names(SHOWN);
        u32 n = client::queue(first, names.data(), SHOWN);
        u32 count = g_now.st.count;
        for (u32 i = 0; i < n; i++) {
            u32 index = first + i;
            names[i].name[sizeof(names[i].name) - 1] = 0;
            auto* item = new PickEntry(names[i].name, index == cur ? ult::INPROGRESS_SYMBOL : "", index == at);
            item->setClickListener([index, count](u64 keys) {
                u32 i = index;  // (the item goes with the page: copies, before swapTo)
                if (keys & HidNpadButton_A) {
                    client::playIndex(i);
                    poll(true);
                    tsl::goBack();
                    return true;
                }
                if (keys & (HidNpadButton_X | HidNpadButton_Y)) {
                    s32 now = (s32)client::moveTrack(i, (keys & HidNpadButton_X) ? -1 : 1);
                    tsl::swapTo<QueueGui>(now);
                    return true;
                }
                if (keys & HidNpadButton_Minus) {
                    client::removeTrack(i);
                    s32 next = count > 1 ? (s32)std::min(i, count - 2) : -1;
                    tsl::swapTo<QueueGui>(next);
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }
        if (!n) list->addItem(new tsl::elm::ListItem(txt::EMPTY));
        frame->setContent(list);
        return frame;
    }

private:
    s32 focus_;
};

// The saved playlists' file names (sorted), in MELODY_PLAYLIST_DIR
std::vector<std::string> savedPlaylists() {
    std::vector<std::string> out;
    if (DIR* d = opendir(MELODY_PLAYLIST_DIR)) {
        while (struct dirent* e = readdir(d)) {
            const char* dot = strrchr(e->d_name, '.');
            if (e->d_name[0] != '.' && dot && (!strcasecmp(dot, ".m3u8") || !strcasecmp(dot, ".m3u")))
                out.emplace_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    return out;
}

std::string withoutExtension(const std::string& file) {
    size_t dot = file.find_last_of('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

std::string g_saved_as;  // a playlist just saved, for the menu to say so

// The saved playlists: A plays one (instead of the playlist), Y adds it to
// the playlist, - deletes it (a second press, to be sure)
class SavedGui : public tsl::Gui {
public:
    explicit SavedGui(std::string focus = "") : focus_(std::move(focus)) {}

    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(txt::SAVED_TITLE, txt::SAVED_SUBTITLE);
        auto* list = pickList();
        list->addItem(new Hint(txt::SAVED_HINT), 40);
        std::vector<std::string> files = savedPlaylists();
        for (size_t i = 0; i < files.size(); i++) {
            std::string file = files[i];
            bool picked = focus_.empty() ? i == 0 : file == focus_;
            auto* item = new PickEntry(withoutExtension(file), "", picked);
            item->setClickListener([this, item, file](u64 keys) {
                std::string f = file;  // (copies, before swapTo)
                if (keys & HidNpadButton_A) {
                    client::loadPlaylist(f.c_str(), false);
                    poll(true);
                    tsl::goBack();
                    return true;
                }
                if (keys & HidNpadButton_Y) {
                    u32 n = client::loadPlaylist(f.c_str(), true);
                    item->setValue(n ? fmt(txt::ADDED_N, n) : txt::NOTHING_NEW_SHORT, !n);
                    poll(true);
                    return true;
                }
                if (keys & HidNpadButton_Minus) {
                    if (armed_ != f) {
                        armed_ = f;
                        item->setValue(txt::DELETE_AGAIN);
                        return true;
                    }
                    remove((std::string(MELODY_PLAYLIST_DIR) + "/" + f).c_str());
                    tsl::swapTo<SavedGui>();
                    return true;
                }
                return false;
            });
            list->addItem(item);
        }
        if (files.empty()) list->addItem(new tsl::elm::ListItem(txt::NO_SAVED));
        frame->setContent(list);
        return frame;
    }

private:
    std::string focus_, armed_;
};

// A name, typed on a phone keypad: the D-pad picks a key, A presses it (again
// within a second for its next letter), L / R move the cursor, Y deletes,
// + (or the Save button) saves the playlist under the name; B goes back
// without saving.
class NameGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(txt::NAME_TITLE, txt::NAME_SUBTITLE);
        frame->setContent(new Pad(this));
        return frame;
    }

    void update() override {
        if (pend_ >= 0 && ult::nowNs() - pend_ns_ > TAP_NS) commit();
    }

    bool handleInput(u64 down, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        // (rows of three keys, 0..11, and Save under them, 12; moving round at the edges)
        if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
            commit();
            if (down & KEY_UP) sel_ = sel_ == 12 ? 10 : sel_ >= 3 ? sel_ - 3 : 12;
            else if (down & KEY_DOWN) sel_ = sel_ == 12 ? 1 : sel_ + 3 < 12 ? sel_ + 3 : 12;
            else if (sel_ != 12 && (down & KEY_LEFT)) sel_ = sel_ % 3 ? sel_ - 1 : sel_ + 2;
            else if (sel_ != 12 && (down & KEY_RIGHT)) sel_ = sel_ % 3 < 2 ? sel_ + 1 : sel_ - 2;
            return true;
        }
        if (down & (HidNpadButton_L | HidNpadButton_R)) {
            commit();
            if ((down & HidNpadButton_L) && cursor_ > 0) cursor_--;
            if ((down & HidNpadButton_R) && cursor_ < name_.size()) cursor_++;
            return true;
        }
        if (down & HidNpadButton_A) press(sel_);
        else if (down & HidNpadButton_Y) erase();
        else if (down & HidNpadButton_Plus) save();
        else return false;  // (B: back, as usual)
        return true;
    }

private:
    static constexpr u64 TAP_NS = 1'000'000'000ULL;
    // keys 0..11 are the keypad (1 2 3 / 4 5 6 / 7 8 9 / aA 0 Del); 12 is Save
    static constexpr const char* KEYS[12] = {".,-!@#$%^&'1", "abc2", "def3", "ghi4", "jkl5", "mno6",
                                             "pqrs7", "tuv8", "wxyz9", "", " 0", ""};
    static constexpr const char* LABELS[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "aA", "0", "Del"};

    char shown(int key, int i) const {
        char c = KEYS[key][i];
        return (shift_ && c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }

    void press(int key) {
        u64 now = ult::nowNs();
        if (key == 12) return save();
        if (key == 9) {  // shift: off, the next letter, all letters
            commit();
            shift_ = (shift_ + 1) % 3;
            return;
        }
        if (key == 11) return erase();
        int n = (int)strlen(KEYS[key]);
        if (pend_ == key && now - pend_ns_ < TAP_NS) {  // the same key again: its next character
            pend_i_ = (pend_i_ + 1) % n;
            name_[cursor_ - 1] = shown(key, pend_i_);
        } else {
            commit();
            if (name_.size() >= 40) return;
            pend_ = key;
            pend_i_ = 0;
            name_.insert(name_.begin() + cursor_, shown(key, 0));
            cursor_++;
        }
        pend_ns_ = now;
        msg_.clear();
    }

    void commit() {
        if (pend_ < 0) return;
        pend_ = -1;
        if (shift_ == 1) shift_ = 0;  // (shift is for one letter)
    }

    void erase() {
        pend_ = -1;
        if (cursor_ > 0) name_.erase(--cursor_, 1);
        msg_.clear();
    }

    void save() {
        commit();
        std::string n = name_;
        while (!n.empty() && n.back() == ' ') n.pop_back();
        while (!n.empty() && n[0] == ' ') n.erase(0, 1);
        if (n.empty()) {
            msg_ = txt::NAME_FIRST;
            return;
        }
        std::vector<std::string> files = savedPlaylists();
        bool taken = std::find(files.begin(), files.end(), n + ".m3u8") != files.end();
        if (taken && confirm_ != n) {
            confirm_ = n;
            msg_ = fmt(txt::NAME_TAKEN, n.c_str());
            return;
        }
        u32 r = client::savePlaylist(n.c_str());
        if (r != MelodySave_Ok) {
            msg_ = client::saveError(r);
            return;
        }
        g_saved_as = n;
        tsl::goBack(2);  // (to the menu, past the list of playlists)
    }

    // The name and the keypad
    class Pad : public tsl::elm::Element {
    public:
        explicit Pad(NameGui* g) : g_(g) { m_isItem = false; }
        void layout(u16, u16, u16, u16) override {}
        void draw(Renderer* r) override {
            const NameGui& g = *g_;
            s32 x = getX(), y = getY() + 6, w = getWidth();
            const u32 NAME = 26, LABEL = 26, LETTERS = 18, SMALL = 18;

            // the name, the letter still being picked in the accent colour, and the cursor
            r->drawRect(x + 4, y + 52, w - 8, 2, r->a(tsl::separatorColor));  // (a line under the name: the overlay's own background is enough)
            std::string before = g.name_.substr(0, g.cursor_), after = g.name_.substr(g.cursor_);
            std::string pending;
            if (g.pend_ >= 0 && !before.empty()) pending = before.back(), before.pop_back();
            s32 bw = r->getTextDimensions(before, false, NAME).first, pw = r->getTextDimensions(pending, false, NAME).first;
            s32 room = w - 28, off = std::max<s32>(0, bw + pw - room);  // (long names: the cursor stays in the box)
            r->enableScissoring(x + 10, std::max<s32>(y, ult::activeHeaderHeight), w - 20, 54);
            s32 tx = x + 14 - off;
            r->drawString(before, false, tx, y + 37, NAME, r->a(WHITE));
            tx += bw;
            if (!pending.empty()) r->drawString(pending, false, tx, y + 37, NAME, r->a(ACCENT_TEXT));
            tx += pw;
            if (g.pend_ < 0 && (ult::nowNs() / 500'000'000ULL) % 2 == 0) r->drawRect(tx + 1, y + 12, 2, 30, r->a(ACCENT_TEXT));
            r->drawString(after, false, tx + 4, y + 37, NAME, r->a(WHITE));
            r->disableScissoring();

            // the keys, 3 x 4
            const s32 gap = 8, kw = (w - 2 * gap) / 3, kh = 64, top = y + 70;
            for (int k = 0; k < 12; k++) {
                s32 kx = x + (k % 3) * (kw + gap), ky = top + (k / 3) * (kh + gap);
                bool on = k == 9 && g.shift_;
                if (g.sel_ == k) squareBorder(r, kx - 2, ky - 2, kw + 4, kh + 4, 4, pulse());
                std::string label = k == 9 ? (g.shift_ == 2 ? txt::KEY_SHIFT_CAPS : g.shift_ == 1 ? txt::KEY_SHIFT_ONE : txt::KEY_SHIFT_OFF)
                                           : LABELS[k];
                bool letters = KEYS[k][0];
                s32 lw = r->getTextDimensions(label, false, LABEL).first;
                r->drawString(label, false, kx + (kw - lw) / 2, ky + (letters ? 30 : kh / 2 + 10), LABEL, r->a(on ? ACCENT_TEXT : WHITE));
                if (!letters) continue;
                if (k == 10) {  // (0: a space first)
                    // (the sign for a space, drawn: a line with its ends turned up)
                    bool now = g.pend_ == 10 && g.pend_i_ == 0;
                    Color c = now ? ACCENT_TEXT : GREY;
                    s32 sx = kx + kw / 2 - 12, sb = ky + 52;
                    r->drawRect(sx, sb, 24, 2, r->a(c));
                    r->drawRect(sx, sb - 6, 2, 6, r->a(c));
                    r->drawRect(sx + 22, sb - 6, 2, 6, r->a(c));
                    continue;
                }
                // the key's letters, the one being picked in the accent colour
                int n = (int)strlen(KEYS[k]) - 1;  // (the digit's the label)
                std::string chars;
                for (int i = 0; i < n; i++) chars += g.shown(k, i);
                s32 cw = r->getTextDimensions(chars, false, LETTERS).first, cx = kx + (kw - cw) / 2;
                for (int i = 0; i < n; i++) {
                    std::string c(1, g.shown(k, i));
                    bool now = g.pend_ == k && g.pend_i_ == i;
                    r->drawString(c, false, cx, ky + 54, LETTERS, r->a(now ? ACCENT_TEXT : GREY));
                    cx += r->getTextDimensions(c, false, LETTERS).first;
                }
            }

            // Save: a wide button with the caret, like the menu's items
            s32 sy = top + 4 * (kh + gap) + 4, sh = 64;
            r->drawRect(x + 4, sy - 4, w - 8, 1, r->a(tsl::separatorColor));  // (above it, as between the menu's items)
            if (g.sel_ == 12) squareBorder(r, x - 2, sy - 2, w + 4, sh + 4, 4, pulse());
            r->drawString(txt::KEY_SAVE, false, x + 20, sy + 41, 24, r->a(WHITE));
            s32 cw = r->getTextDimensions(ult::DROPDOWN_SYMBOL, false, 24).first;
            r->drawString(ult::DROPDOWN_SYMBOL, false, x + w - 20 - cw, sy + 41, 24, r->a(WHITE));
            // what went wrong, if something did (nothing there otherwise)
            s32 my = sy + sh + 28;
            for (const std::string& line : wrap(r, g.msg_, w - 8, SMALL)) {
                r->drawString(line, false, x + 4, my, SMALL, r->a(ACCENT_TEXT));
                my += SMALL + 6;
            }
            scrollingText(r, txt::NAME_HINT, x + 4, sy + sh + 84, w - 8, SMALL, GREY, 0);
        }

    private:
        NameGui* g_;
    };

    std::string name_, msg_, confirm_;
    size_t cursor_ = 0;  // where letters go in name_
    int sel_ = 1;        // the key picked (starting on 2: abc)
    int pend_ = -1;      // the key whose character (before the cursor) can still change (-1: none)
    int pend_i_ = 0;
    u64 pend_ns_ = 0;
    int shift_ = 1;      // 0: off, 1: the next letter (on to start with), 2: caps
};

bool g_eq_changed = false;  // (for the menu to show the new one when it's back)

// The equalizer: Off, or one of the Equalizer APO configs in MELODY_EQ_DIR
// (the one in use ticked); A picks one
class EqGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(txt::EQ_TITLE, txt::EQ_SUBTITLE);
        auto* list = pickList();
        list->addItem(new Hint(""), 40);  // (what a config gave, for a moment)
        std::string now = client::equalizer();
        std::vector<std::string> files;
        if (DIR* d = opendir(MELODY_EQ_DIR)) {
            while (struct dirent* e = readdir(d)) {
                const char* dot = strrchr(e->d_name, '.');
                if (e->d_name[0] != '.' && dot && !strcasecmp(dot, ".txt")) files.emplace_back(e->d_name);
                if (files.size() >= 200) break;
            }
            closedir(d);
        }
        std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
        files.insert(files.begin(), "");  // (Off)
        for (const std::string& file : files) {
            bool on = file == now;
            auto* item = new PickEntry(file.empty() ? txt::EQ_OFF : withoutExtension(file), on ? ult::CHECKMARK_SYMBOL : "", on);
            item->setClickListener([file](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                std::string f = file;  // (copies, before swapTo)
                MelodyEqResult r = client::setEqualizer(f.c_str());
                if (r.filters == -2) flash(txt::SAVE_NO_ANSWER);
                else if (r.filters < 0) flash(txt::EQ_FAILED);
                else if (!f.empty()) flash(r.skipped ? fmt(txt::EQ_PARTLY, (u32)r.filters, r.skipped) : fmt(txt::EQ_LOADED, (u32)r.filters));
                g_eq_changed = true;
                tsl::swapTo<EqGui>();  // (again, ticking the new one)
                return true;
            });
            list->addItem(item);
        }
        if (files.size() == 1) list->addItem(new tsl::elm::ListItem(txt::EQ_NONE));
        frame->setContent(list);
        return frame;
    }
};

// Saving the playlist: as a new one (named on the keypad), or over one saved before
class SaveGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(txt::SAVE_TITLE, fmt(txt::TRACKS, g_now.st.count));
        auto* list = new tsl::elm::List();
        if (!g_now.st.count) {
            list->addItem(new tsl::elm::ListItem(txt::QUEUE_EMPTY));
            frame->setContent(list);
            return frame;
        }
        auto* add = new tsl::elm::ListItem(txt::NEW_PLAYLIST, ult::DROPDOWN_SYMBOL);
        add->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            tsl::changeTo<NameGui>();
            return true;
        });
        list->addItem(add);
        std::vector<std::string> files = savedPlaylists();
        if (!files.empty()) list->addItem(new tsl::elm::CategoryHeader(txt::REPLACE));
        for (const std::string& file : files) {
            auto* item = new tsl::elm::ListItem(withoutExtension(file));
            item->setClickListener([this, item, file](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                if (armed_ != file) {  // (a second press, to be sure)
                    armed_ = file;
                    item->setValue(txt::REPLACE_AGAIN);
                    return true;
                }
                armed_.clear();
                u32 r = client::savePlaylist(file.c_str());
                if (r == MelodySave_Ok)
                    item->setValue(std::string(txt::SAVED) + " " + ult::CHECKMARK_SYMBOL);
                else
                    flash(client::saveError(r)), item->setValue(ult::CROSSMARK_SYMBOL);
                return true;
            });
            list->addItem(item);
        }
        frame->setContent(list);
        return frame;
    }

private:
    std::string armed_;
};

// Y held for a second (once per hold: it has to be let go before it counts again)
class HoldY {
public:
    explicit HoldY(bool wait_for_release = false) : fired_(wait_for_release) {}
    bool step(u64 held) {
        if (!(held & KEY_Y)) {
            since_ = 0, fired_ = false;
            return false;
        }
        u64 now = ult::nowNs();
        if (!since_) since_ = now;
        if (fired_ || now - since_ < 1'000'000'000ULL) return false;
        fired_ = true;
        return true;
    }

private:
    u64 since_ = 0;
    bool fired_;
};

// The handheld screen's backlight: off with Y held on the main menu (the music
// plays on, and the buttons are locked), on again with Y held, or when the
// overlay closes
bool g_display_off = false;
void displayOn(bool on) {
    if (on == !g_display_off) return;
    if (R_FAILED(lblInitialize())) return;
    if (on) lblSwitchBacklightOn(0);
    else lblSwitchBacklightOff(0);
    lblExit();
    g_display_off = !on;
    // (while it's off, a touch beside the overlay or the overlay's combo
    // doesn't close it: the lock would open. HOME still does, below.)
    tsl::disableHiding = !on;
}

// What's shown while the display's off. Every button is taken here (the game
// gets none while the overlay's open), and only Y held wakes it; HOME and
// power still work, as the system has those first.
class DisplayOffGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(txt::TITLE, "");
        auto* list = new tsl::elm::List();
        // (the words only docked: handheld they'd show for a frame before the screen goes dark)
        bool docked = appletGetOperationMode() == AppletOperationMode_Console;
        list->addItem(new tsl::elm::CustomDrawer([docked](Renderer* r, s32 x, s32 y, s32 w, s32) {
            if (!docked) return;
            s32 tw = r->getTextDimensions(txt::DISPLAY_OFF, false, 18).first;
            r->drawString(txt::DISPLAY_OFF, false, x + std::max(0, (w - tw) / 2), y + 28, 18, r->a(GREY));
        }), 40);
        frame->setContent(list);
        displayOn(false);
        return frame;
    }
    void update() override {
        if (tsl::homeButtonPressedInGame.exchange(false)) {  // HOME (libtesla doesn't hide while disableHiding)
            displayOn(true);
            tsl::goBack();
            tsl::Overlay::get()->hide();
            return;
        }
        if (!g_display_off) tsl::goBack();  // (it came on another way: the overlay was closed and opened again)
    }
    bool handleInput(u64, u64 held, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        if (hold_.step(held)) {
            displayOn(true);
            tsl::goBack();
        }
        return true;
    }

private:
    HoldY hold_{true};  // (Y is still down from turning it off)
};

// Text broken into lines to fit the page (not focusable)
class Paragraph : public tsl::elm::Element {
public:
    static constexpr u32 SIZE = 16, LINE = 22;
    explicit Paragraph(const std::string& text) {
        m_isItem = false;
        lines_ = wrap(&tsl::gfx::Renderer::get(), text, tsl::cfg::FramebufferWidth - 110, SIZE);
    }
    u16 height() const { return (u16)(lines_.size() * LINE + 12); }
    void layout(u16, u16, u16, u16) override {}
    void draw(Renderer* r) override {
        s32 y = getY() + LINE;
        for (const std::string& l : lines_) r->drawString(l, false, getX() + 12, y, SIZE, r->a(GREY)), y += LINE;
    }

private:
    std::vector<std::string> lines_;
};

// Bluetooth audio patches (btpatch.h): a bitrate and a latency, each written
// as its own IPS for Atmosphère to load at the next boot (Original deletes
// it). Only for a firmware they're known on.
class PatchesGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame(txt::PATCHES_TITLE, txt::PATCHES_SUBTITLE);
        auto* list = new tsl::elm::List();
        // (the cursor's kept low in the view, not centred: else the first option,
        // under the text, opens scrolled down past the text's start)
        list->setCenterOffset(200);
        SetSysFirmwareVersion fw{};
        bool got = R_SUCCEEDED(setsysInitialize());
        if (got) got = R_SUCCEEDED(setsysGetFirmwareVersion(&fw)), setsysExit();
        fw_ = got ? btpatch::find(fw.major, fw.minor, fw.micro) : nullptr;
        if (!fw_) {
            std::string v = got ? fmt("%u.%u.%u", fw.major, fw.minor, fw.micro) : "?";
            auto* p = new Paragraph(fmt(txt::PATCHES_UNSUPPORTED, v.c_str()));
            list->addItem(p, p->height());
            frame->setContent(list);
            return frame;
        }
        for (const char* t : {txt::PATCHES_INFO_1, txt::PATCHES_INFO_2}) {
            auto* p = new Paragraph(t);
            list->addItem(p, p->height());
        }

        list->addItem(new tsl::elm::CategoryHeader(txt::BITRATE));
        const char* bitrates[] = {txt::BITRATE_ORIGINAL, txt::BITRATE_HIGH, txt::BITRATE_MAX};
        int bnow = installed(btpatch::BITRATE_FOLDER, btpatch::Bitrate_Count, [this](int i) { return btpatch::bitrateIps(*fw_, (btpatch::Bitrate)i); });
        for (int i = 0; i < btpatch::Bitrate_Count; i++) {
            bitrate_[i] = new tsl::elm::ListItem(bitrates[i], i == bnow ? ult::CHECKMARK_SYMBOL : "");
            bitrate_[i]->setClickListener([this, i](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                pick(bitrate_, btpatch::Bitrate_Count, i, btpatch::BITRATE_FOLDER, btpatch::bitrateIps(*fw_, (btpatch::Bitrate)i));
                return true;
            });
            list->addItem(bitrate_[i]);
        }

        list->addItem(new tsl::elm::CategoryHeader(txt::LATENCY));
        const char* latencies[] = {txt::LATENCY_ORIGINAL, txt::LATENCY_IMPROVED, txt::LATENCY_IMPOSSIBLE};
        int lnow = installed(btpatch::LATENCY_FOLDER, btpatch::Latency_Count, [this](int i) { return btpatch::latencyIps(*fw_, (btpatch::Latency)i); });
        for (int i = 0; i < btpatch::Latency_Count; i++) {
            latency_[i] = new tsl::elm::ListItem(latencies[i], i == lnow ? ult::CHECKMARK_SYMBOL : "");
            latency_[i]->setClickListener([this, i](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                pick(latency_, btpatch::Latency_Count, i, btpatch::LATENCY_FOLDER, btpatch::latencyIps(*fw_, (btpatch::Latency)i));
                return true;
            });
            list->addItem(latency_[i]);
        }

        list->addItem(new tsl::elm::CategoryHeader(txt::APPLY));
        reboot_ = new tsl::elm::ListItem(txt::REBOOT);
        reboot_->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (!reboot_armed_) {  // (a second press, to be sure)
                reboot_armed_ = true;
                reboot_->setValue(txt::REBOOT_AGAIN);
                return true;
            }
            if (R_SUCCEEDED(spsmInitialize())) spsmShutdown(true);
            reboot_->setValue(ult::CROSSMARK_SYMBOL);  // (still here: it didn't)
            return true;
        });
        list->addItem(reboot_);

        frame->setContent(list);
        return frame;
    }

private:
    // Which option's IPS is in the folder now (0: none; -1: one this didn't make)
    template <typename Make>
    int installed(const char* folder, int count, Make make) {
        FILE* f = fopen(btpatch::path(*fw_, folder).c_str(), "rb");
        if (!f) return 0;
        std::vector<uint8_t> have(4096);
        have.resize(fread(have.data(), 1, have.size(), f));
        fclose(f);
        for (int i = 1; i < count; i++)
            if (make(i) == have) return i;
        return -1;
    }

    // Writes (or for Original, deletes) the option's IPS; ticks it, or crosses it if that failed
    void pick(tsl::elm::ListItem** items, int count, int i, const char* folder, const std::vector<uint8_t>& ips) {
        std::string dir = std::string("sdmc:/atmosphere/exefs_patches/") + folder, file = btpatch::path(*fw_, folder);
        bool ok;
        if (ips.empty()) {
            ok = remove(file.c_str()) == 0 || !isFile(file);
            rmdir(dir.c_str());  // (only if it's empty now)
        } else {
            mkdir("sdmc:/atmosphere/exefs_patches", 0777);
            mkdir(dir.c_str(), 0777);
            FILE* f = fopen(file.c_str(), "wb");
            ok = f && fwrite(ips.data(), 1, ips.size(), f) == ips.size();
            if (f) ok = fclose(f) == 0 && ok;
        }
        for (int k = 0; k < count; k++) items[k]->setValue(k == i ? (ok ? ult::CHECKMARK_SYMBOL : ult::CROSSMARK_SYMBOL) : "");
    }

    static bool isFile(const std::string& p) {
        struct stat st;
        return stat(p.c_str(), &st) == 0;
    }

    const btpatch::Firmware* fw_ = nullptr;
    tsl::elm::ListItem* bitrate_[btpatch::Bitrate_Count] = {};
    tsl::elm::ListItem* latency_[btpatch::Latency_Count] = {};
    tsl::elm::ListItem* reboot_ = nullptr;
    bool reboot_armed_ = false;
};

class MainGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        poll(true);
        auto* frame = new tsl::elm::OverlayFrame(txt::TITLE, txt::MAIN_SUBTITLE);
        auto* list = new tsl::elm::List();
        list->addItem(new NowPlaying(), 150);
        list->addItem(new SeekBar(), 64);
        list->addItem(new Controls(), 84);

        list->addItem(new tsl::elm::CategoryHeader(txt::SOUND));
        volume_ = new tsl::elm::StepTrackBar("\uE13C", 11);  // (in steps of 10%)
        volume_->setProgress((g_now.st.volume + 5) / 10);
        volume_->setValueChangedListener([](u16 step) { client::setVolume(step * 10u); });
        list->addItem(volume_);

        // The game's own volume, apart from the music's (not on firmware before
        // 11, nor with an older sysmodule: the bar isn't there then)
        u32 gv = client::gameVolume();
        if (!client::connected() || gv != MELODY_NO_VOLUME) {
            game_ = new tsl::elm::StepTrackBar("\uE0AA", 11);  // (in steps of 10%)
            showGameVolume();
            game_->setValueChangedListener([](u16 step) { client::setGameVolume(step * 10u); });
            list->addItem(game_);
        }

        shuffle_ = new tsl::elm::ToggleListItem(txt::SHUFFLE, g_now.st.shuffle);
        shuffle_->setStateChangedListener([](bool on) { client::setShuffle(on); });
        list->addItem(shuffle_);
        repeat_ = new tsl::elm::ListItem(txt::REPEAT);
        showRepeat(g_now.st.repeat);
        repeat_->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            u32 m = (g_now.st.repeat + 1) % 3;
            client::setRepeat(m);
            g_now.st.repeat = (u8)m;
            showRepeat(m);
            return true;
        });
        list->addItem(repeat_);
        eq_ = new tsl::elm::ListItem(txt::EQUALIZER);
        showEq();
        eq_->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            tsl::changeTo<EqGui>();
            return true;
        });
        list->addItem(eq_);

        list->addItem(new tsl::elm::CategoryHeader(txt::PLAYLIST));
        auto* saved = new tsl::elm::ListItem(txt::SAVED_PLAYLISTS, ult::DROPDOWN_SYMBOL);
        saved->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            tsl::changeTo<SavedGui>();
            return true;
        });
        list->addItem(saved);
        save_ = new tsl::elm::ListItem(txt::SAVE_PLAYLIST, ult::DROPDOWN_SYMBOL);
        save_->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            tsl::changeTo<SaveGui>();
            return true;
        });
        list->addItem(save_);
        auto* show = new tsl::elm::ListItem(txt::SHOW_PLAYLIST, ult::DROPDOWN_SYMBOL);
        show->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            tsl::changeTo<QueueGui>();
            return true;
        });
        list->addItem(show);
        clear_ = new tsl::elm::ListItem(txt::CLEAR_PLAYLIST);
        clear_->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (!clear_armed_) {  // (a second press, to be sure)
                clear_armed_ = true;
                clear_->setValue(txt::CLEAR_AGAIN);
                return true;
            }
            client::clear();
            clear_armed_ = false;
            clear_->setValue(txt::CLEARED);
            poll(true);
            return true;
        });
        list->addItem(clear_);
        auto* add = new tsl::elm::ListItem(txt::ADD_TO_PLAYLIST, ult::DROPDOWN_SYMBOL);
        add->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            tsl::changeTo<BrowseGui>(startFolder());
            return true;
        });
        list->addItem(add);

        frame->setContent(list);
        return frame;
    }

    bool handleInput(u64 down, u64 held, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        if (hold_y_.step(held)) {
            tsl::changeTo<DisplayOffGui>();
            return true;
        }
        if (down & KEY_X) {
            tsl::changeTo<PatchesGui>();
            return true;
        }
        return false;
    }

    void update() override {
        bool was = g_now.running;
        poll();
        if (g_eq_changed) {  // (back from the equalizer)
            g_eq_changed = false;
            showEq();
        }
        if (!g_saved_as.empty()) {  // (back from naming a new playlist)
            save_->setValue(std::string(txt::SAVED) + " " + ult::CHECKMARK_SYMBOL);
            g_saved_as.clear();
        }
        if (g_now.running && !was) {  // (the sysmodule came up while the overlay was open)
            volume_->setProgress((g_now.st.volume + 5) / 10);
            showGameVolume();
            shuffle_->setState(g_now.st.shuffle);
            showRepeat(g_now.st.repeat);
        }
    }

private:
    // (Off in the faint colour, like a toggle that's off)
    void showRepeat(u32 m) { repeat_->setValue(repeatName(m), m == MelodyRepeat_Off); }

    void showGameVolume() {
        if (!game_) return;
        u32 v = client::gameVolume();
        // (MELODY_NO_VOLUME here: no sysmodule yet, shown at full until it's up)
        game_->setProgress(v == MELODY_NO_VOLUME ? 10 : (v + 5) / 10);
    }

    tsl::elm::StepTrackBar* volume_ = nullptr;
    tsl::elm::StepTrackBar* game_ = nullptr;
    tsl::elm::ToggleListItem* shuffle_ = nullptr;
    tsl::elm::ListItem* repeat_ = nullptr;
    tsl::elm::ListItem* clear_ = nullptr;
    tsl::elm::ListItem* save_ = nullptr;
    tsl::elm::ListItem* eq_ = nullptr;

    // (the config's name, or Off in the faint colour)
    void showEq() {
        std::string n = client::equalizer();
        eq_->setValue(n.empty() ? std::string(txt::EQ_OFF) : withoutExtension(n), n.empty());
    }
    bool clear_armed_ = false;
    HoldY hold_y_;
};

class MelodyOverlay : public tsl::Overlay {
public:
    void initServices() override {}
    void exitServices() override {
        displayOn(true);
        client::close();
    }
    void onShow() override { g_now.track_id = ~0u; }  // (the cover is read again: it may have changed)
    void onHide() override { displayOn(true); }  // (never left dark with nothing to wake it)
    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<MainGui>(); }
};

}  // namespace

int main(int argc, char** argv) { return tsl::loop<MelodyOverlay>(argc, argv); }
