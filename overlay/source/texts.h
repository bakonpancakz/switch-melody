// Everything the overlay says, in one place to reword. Each is used as the
// comment above it says. "%u" is a number and "%s" a name, filled in when shown;
// keep those (and the order they come in). BTN_A and the others draw the
// Switch's button icons: put them in a string as below.
#pragma once

#define BTN_A "\uE0E0"
#define BTN_B "\uE0E1"
#define BTN_X "\uE0E2"
#define BTN_Y "\uE0E3"
#define BTN_MINUS "\uE0B6"
#define BTN_PLUS "\uE0B5"
#define BTN_L "\uE0E4"
#define BTN_R "\uE0E5"

namespace txt {

// --- the main menu ---
inline constexpr const char* TITLE = "Melody";
inline constexpr const char* MAIN_SUBTITLE = BTN_L " " BTN_R " Shortcuts   " BTN_Y " Display Off   " BTN_X " Patches";  // under the title
inline constexpr const char* SOUND = "Controls";            // the heading over volume, shuffle and repeat
inline constexpr const char* SHUFFLE = "Shuffle";
inline constexpr const char* REPEAT = "Repeat";
inline constexpr const char* REPEAT_OFF = "Off";
inline constexpr const char* REPEAT_ALL = "All";
inline constexpr const char* REPEAT_ONE = "Track";
inline constexpr const char* PLAYLIST = "Playlists";      // the heading over the playlist items
inline constexpr const char* ADD_TO_PLAYLIST = "Add to Queue";
inline constexpr const char* SHOW_PLAYLIST = "List Queue";
inline constexpr const char* SAVED_PLAYLISTS = "List Playlists";
inline constexpr const char* SAVE_PLAYLIST = "Save as Playlist";
inline constexpr const char* CLEAR_PLAYLIST = "Clear Queue";
inline constexpr const char* CLEAR_AGAIN = "Are you sure?";  // beside Clear Playlist, after the first press
inline constexpr const char* CLEARED = "Cleared!";
inline constexpr const char* SAVED = "Saved!";              // beside Save Playlist, back from naming one

// --- the equalizer (Equalizer APO configs, .txt, in /config/sys-melody/eq) ---
inline constexpr const char* EQUALIZER = "Equalizer";     // under Controls, its value the config in use
inline constexpr const char* EQ_OFF = "Off";
inline constexpr const char* EQ_TITLE = "Equalizer";
inline constexpr const char* EQ_SUBTITLE = "Hint! APO Data is stored at '/config/sys-melody/eq' !";
inline constexpr const char* EQ_NONE = "(no configs in here)";
inline constexpr const char* EQ_LOADED = "%u filters";     // what a config gave, for a moment at the top
inline constexpr const char* EQ_PARTLY = "%u filters, %u lines ignored";  // (GraphicEQ, Channel, ... aren't)
inline constexpr const char* EQ_FAILED = "Failed to parse config";

// --- what's playing ---
inline constexpr const char* NOT_RUNNING = "Melody is unavailable!";
inline constexpr const char* NOT_RUNNING_1 = "Install the sysmodule,";      // (two lines under it)
inline constexpr const char* NOT_RUNNING_2 = "and then restart your Switch.";
inline constexpr const char* NOTHING_QUEUED = "Idling...";
inline constexpr const char* NOTHING_QUEUED_HINT = "Add to the queue below!";
inline constexpr const char* UNKNOWN_ARTIST = "Unknown";
inline constexpr const char* UNKNOWN_ALBUM = "Unknown";
inline constexpr const char* TRACK_COUNT = "%u / %u";                 // which track of how many
inline constexpr const char* RESTART_TO_UPDATE = "Sysmodule outdated. Restart ASAP!";  // (the sysmodule is older than the overlay)
inline constexpr const char* SEEK_HINT = BTN_A " Seek";               // under the seek bar, when it's picked
inline constexpr const char* SEEK_PICKED_HINT = BTN_A " Resume from here";  // ... and once A picked the playhead up

// --- Add to Playlist (the folders) ---
inline constexpr const char* BROWSE_TITLE = "Add to Queue";
inline constexpr const char* BROWSE_HINT = BTN_Y " Queue Folder     " BTN_X " Set as Start Folder";
inline constexpr const char* UP = "Up";                    // beside ".."
inline constexpr const char* IN_PLAYLIST = "Already Queued!";  // beside a file that's already queued
inline constexpr const char* NO_MUSIC_HERE = "(no music in here)";
// A on a file that can't play (it's dimmed in the list), and why
inline constexpr const char* CANT_QUEUE_EMPTY = "Unable to add to queue! (Empty File)";      // 0 KB
inline constexpr const char* CANT_QUEUE_CORRUPT = "Unable to add to queue! (Corrupt File)";  // not FLAC or Ogg inside
inline constexpr const char* CANT_QUEUE_NOT_OPUS = "Unable to add to queue! (Invalid Codec)";  // Ogg, but Vorbis or such
inline constexpr const char* ADDED_ONE_TRACK = "Queued 1 track";
inline constexpr const char* ADDED_TRACKS = "Queued %u tracks";
inline constexpr const char* NOTHING_NEW = "Nothing added to queue";
inline constexpr const char* START_FOLDER_SET = "Start Folder set!";
inline constexpr const char* START_FOLDER_FAILED = "Unable to set Start Folder...";

// --- Show Playlist ---
inline constexpr const char* QUEUE_TITLE = "Current Queue";
inline constexpr const char* TRACKS = "%u tracks";         // under the title (here and in Save Playlist)
inline constexpr const char* QUEUE_HINT = BTN_X " Up     " BTN_Y " Down     " BTN_MINUS " Remove";
inline constexpr const char* EMPTY = "(no music in here)";

// --- Saved Playlists ---
inline constexpr const char* SAVED_TITLE = "Saved Playlists";
inline constexpr const char* SAVED_SUBTITLE = "Hint! Data is stored at '/config/sys-melody/playlists' !";
inline constexpr const char* SAVED_HINT = BTN_A " Play     " BTN_Y " Add to queue     " BTN_MINUS " Delete";
inline constexpr const char* ADDED_N = "Added %u";         // beside a playlist added with Y
inline constexpr const char* NOTHING_NEW_SHORT = "Nothing added to queue";
inline constexpr const char* DELETE_AGAIN = "Are you sure?";
inline constexpr const char* NO_SAVED = "(no saved playlists)";

// --- Save Playlist ---
inline constexpr const char* SAVE_TITLE = "Save Playlist";
inline constexpr const char* QUEUE_EMPTY = "(the queue is empty)";
inline constexpr const char* NEW_PLAYLIST = "New Playlist";
inline constexpr const char* REPLACE = "Overwrite Playlist";          // the heading over the saved playlists
inline constexpr const char* REPLACE_AGAIN = "Are you sure?";

// --- New Playlist (the keypad) ---
inline constexpr const char* NAME_TITLE = "New Playlist";
inline constexpr const char* NAME_SUBTITLE = "";
inline constexpr const char* KEY_SAVE = "Save";
inline constexpr const char* KEY_SHIFT_OFF = "abc";   // the shift key, saying what the next letter will be (the 0 key draws a space's sign)
inline constexpr const char* KEY_SHIFT_ONE = "Abc";   // ... a capital, then small ones
inline constexpr const char* KEY_SHIFT_CAPS = "ABC";  // ... all capitals
inline constexpr const char* NAME_HINT = BTN_A " Enter   " BTN_Y " Bksp   L/R Move   " BTN_PLUS " Save";
inline constexpr const char* NAME_FIRST = "Write something first!";
inline constexpr const char* NAME_TAKEN = "\"%s\" already exists! Overwrite it?";

// --- Display Off (Y held on the main menu): the screen's off and the buttons locked until Y is held again ---
inline constexpr const char* DISPLAY_OFF = "Hold " BTN_Y " to turn the display back on.";

// --- Patches (X on the main menu): Bluetooth audio patches for the bluetooth sysmodule ---
inline constexpr const char* PATCHES_TITLE = "Patches";
inline constexpr const char* PATCHES_SUBTITLE = "Please read instructions before use.";
inline constexpr const char* PATCHES_INFO_1 =
    "This menu allows you to generate patches for your Nintendo Switch regarding Bluetooth Audio. They are experimental "
    "but at worst will cause silence or garbled audio to play from your headphones.";
inline constexpr const char* PATCHES_INFO_2 =
    "Please consider that lower latencies and higher bitrates will require you to be physically closer to the console. "
    "Additionally, not every headphone will support the options you request.";
inline constexpr const char* PATCHES_UNSUPPORTED = "Your console firmware (%s) is not supported.";
inline constexpr const char* BITRATE = "Bitrate (Quality)";  // the heading over the bitrate options
inline constexpr const char* BITRATE_ORIGINAL = "Original";
inline constexpr const char* BITRATE_HIGH = "High Quality (345 kbps)";
inline constexpr const char* BITRATE_MAX = "Max Quality (570 kbps)";
inline constexpr const char* LATENCY = "Latency (Delay)";    // the heading over the latency options
inline constexpr const char* LATENCY_ORIGINAL = "Original";
inline constexpr const char* LATENCY_IMPROVED = "Standard (~60 ms)";
inline constexpr const char* LATENCY_IMPOSSIBLE = "Minimum (~20 ms)";
inline constexpr const char* APPLY = "Finale";  // the heading over Reboot to Apply
inline constexpr const char* REBOOT = "Reboot to Apply";
inline constexpr const char* REBOOT_AGAIN = "Are you sure?";  // beside Reboot to Apply, after the first press

// --- when saving fails ---
inline constexpr const char* SAVE_BAD_NAME = "Invalid Filename";
inline constexpr const char* SAVE_NO_CARD = "Failed to open SD Card";
inline constexpr const char* SAVE_WRITE_FAILED = "Write Failed, check logs for error.";
inline constexpr const char* SAVE_NO_ANSWER = "Sysmodule unavailable, restart your Switch and try again.";

}  // namespace txt

// A string with its %u / %s filled in
#include <cstdarg>
#include <cstdio>
#include <string>
inline std::string fmt(const char* f, ...) {
    char b[256];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof(b), f, a);
    va_end(a);
    return b;
}
