// The Patches page's Bluetooth audio patches: IPS files for the bluetooth
// sysmodule (010000000000000B), made here for the firmware they're known on.
// (Worked out in patches/patch-bt; each site is checked against that
// firmware's dump.) Atmosphère loads them at boot from
// sdmc:/atmosphere/exefs_patches/<folder>/<build id>.ips.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace btpatch {

// Where a firmware's bluetooth keeps what the patches change (IPS offsets:
// the NSO header's 0x100 included)
struct Firmware {
    uint32_t major, minor, micro;
    const char* build_id;
    uint32_t queue_full[3];  // `cmp w8,#15`: don't start encoding when the packet queue is full (Q-1)
    uint32_t queue_size[3];  // `cmp w8,#16`: keep encoding while the queue is below Q
    uint32_t frames;         // `cmp w10,#14`: SBC frames per packet (F-1)
    uint32_t ladder;         // the 6 SBC bitrate steps (kbps, u16 each; stock 250 250 220 180 180 100)
    uint32_t ladder_init;    // `mov w11,#250`: the step it starts on
};

inline constexpr Firmware FIRMWARES[] = {
    {23, 5, 0, "064521C83399E09105D9A190688BF8B8C2C7F17C",
     {0x59B44, 0x59E38, 0x59EC0}, {0x59E04, 0x59EA4, 0x59F8C}, 0x59DBC, 0x11BCA0, 0x58F84},
};

inline const Firmware* find(uint32_t major, uint32_t minor, uint32_t micro) {
    for (const Firmware& f : FIRMWARES)
        if (f.major == major && f.minor == minor && f.micro == micro) return &f;
    return nullptr;
}

// The options, in the order the page lists them (0 is Original: no patch)
enum Bitrate { Bitrate_Original, Bitrate_High, Bitrate_Max, Bitrate_Count };
enum Latency { Latency_Original, Latency_Improved, Latency_Impossible, Latency_Count };

inline constexpr const char* BITRATE_FOLDER = "melody_bluetooth_sbc_quality";
inline constexpr const char* LATENCY_FOLDER = "melody_bluetooth_low_latency";

namespace detail {
inline void u32le(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i)));
}
inline void record(std::vector<uint8_t>& o, uint32_t off, const std::vector<uint8_t>& data) {
    o.push_back((uint8_t)(off >> 16)), o.push_back((uint8_t)(off >> 8)), o.push_back((uint8_t)off);
    o.push_back((uint8_t)(data.size() >> 8)), o.push_back((uint8_t)data.size());
    o.insert(o.end(), data.begin(), data.end());
}
inline std::vector<uint8_t> cmpW(uint32_t reg, uint32_t imm) {  // cmp wN, #imm
    std::vector<uint8_t> o;
    u32le(o, 0x7100001Fu | (imm << 10) | (reg << 5));
    return o;
}
inline std::vector<uint8_t> ips(const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& records) {
    std::vector<uint8_t> o = {'P', 'A', 'T', 'C', 'H'};
    for (const auto& r : records) record(o, r.first, r.second);
    o.insert(o.end(), {'E', 'O', 'F'});
    return o;
}
}  // namespace detail

// The IPS for an option (empty for Original)
inline std::vector<uint8_t> bitrateIps(const Firmware& fw, Bitrate b) {
    static const uint16_t LADDERS[Bitrate_Count][6] = {
        {},
        {345, 345, 300, 250, 220, 150},  // High: bitpool ~52 at the top (most earbuds allow up to 53)
        {570, 570, 490, 400, 345, 250},  // Max: bitpool ~89, the Switch's own limit
    };
    if (b <= Bitrate_Original || b >= Bitrate_Count) return {};
    std::vector<uint8_t> ladder, init;
    for (uint16_t k : LADDERS[b]) ladder.push_back((uint8_t)k), ladder.push_back((uint8_t)(k >> 8));
    detail::u32le(init, 0x52800000u | ((uint32_t)LADDERS[b][0] << 5) | 11);  // mov w11, #top step
    return detail::ips({{fw.ladder, ladder}, {fw.ladder_init, init}});
}

inline std::vector<uint8_t> latencyIps(const Firmware& fw, Latency l) {
    // Q packets of F SBC frames (2.67 ms each) at most queued; stock is 16 of 15 (~640 ms)
    uint32_t q, f;
    if (l == Latency_Improved) q = 3, f = 8;         // ~64 ms
    else if (l == Latency_Impossible) q = 1, f = 8;  // ~21 ms
    else return {};
    // (in the order patch-bt's files have them, so one installed by hand is recognised)
    return detail::ips({{fw.queue_full[0], detail::cmpW(8, q - 1)},
                        {fw.queue_size[0], detail::cmpW(8, q)},
                        {fw.frames, detail::cmpW(10, f - 1)},
                        {fw.queue_full[1], detail::cmpW(8, q - 1)},
                        {fw.queue_size[1], detail::cmpW(8, q)},
                        {fw.queue_full[2], detail::cmpW(8, q - 1)},
                        {fw.queue_size[2], detail::cmpW(8, q)}});
}

inline std::string path(const Firmware& fw, const char* folder) {
    return std::string("sdmc:/atmosphere/exefs_patches/") + folder + "/" + fw.build_id + ".ips";
}

}  // namespace btpatch
