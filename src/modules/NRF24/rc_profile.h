#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Versioned NVS records: one blob write commits a complete profile.
namespace RcProfile {
constexpr uint32_t ScanMagic = 0x53435232, PacketMagic = 0x50435232;
struct Scan {
    uint32_t magic = ScanMagic;
    uint16_t version = 1, samples = 0;
    uint16_t hits[126] = {};
    uint32_t checksum = 0;
};
struct Packet {
    uint32_t magic = PacketMagic;
    uint8_t version = 1, channel = 40, rate = 0; // RF24: 0=1M, 1=2M, 2=250K
    uint8_t addressSize = 5, address[5] = {'R', 'C', 'L', 'R', 'N'};
    uint8_t width = 32, crc = 2, count = 0; // width 0=dynamic; CRC 0=off/1=8bit/2=16bit
    uint8_t lengths[8] = {}, data[8][32] = {};
    uint32_t checksum = 0;
};
inline uint32_t hash(const void *data, size_t size) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint32_t h = 2166136261u;
    while (size--) h = (h ^ *p++) * 16777619u;
    return h;
}
template <typename T> inline void seal(T &p) { p.checksum = hash(&p, offsetof(T, checksum)); }
template <typename T> inline bool intact(const T &p) {
    return p.checksum == hash(&p, offsetof(T, checksum));
}
inline bool valid(const Scan &s) {
    if (s.magic != ScanMagic || s.version != 1 || !s.samples || !intact(s)) return false;
    for (auto h : s.hits) if (h > s.samples) return false;
    return true;
}
inline bool settingsValid(const Packet &p) {
    return p.magic == PacketMagic && p.version == 1 && p.channel <= 125 && p.rate <= 2 &&
           p.addressSize >= 3 && p.addressSize <= 5 && p.width <= 32 && p.crc <= 2;
}
inline bool valid(const Packet &p) {
    if (!settingsValid(p) || !p.count || p.count > 8 || !intact(p)) return false;
    for (uint8_t i = 0; i < p.count; ++i)
        if (!p.lengths[i] || p.lengths[i] > 32 || (p.width && p.lengths[i] != p.width)) return false;
    return true;
}
inline void rank(const Scan &s, uint8_t (&channels)[126]) {
    for (int i = 0; i < 126; ++i) channels[i] = i;
    for (int i = 1; i < 126; ++i) {
        uint8_t ch = channels[i];
        int j = i;
        while (j > 0 && s.hits[ch] > s.hits[channels[j - 1]]) {
            channels[j] = channels[j - 1]; --j;
        }
        channels[j] = ch;
    }
}
}
