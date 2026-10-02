#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace IrFavorite {
constexpr size_t MaxTimings = 512;
struct Record {
    uint32_t magic = 0x49524631;
    uint16_t version = 1, frequency = 38000, count = 0;
    uint8_t offOnly = 0, reserved = 0;
    char name[32] = {};
    uint16_t timings[MaxTimings] = {};
    uint32_t checksum = 0;
};
inline uint32_t hash(const Record &r) {
    const auto *p = reinterpret_cast<const uint8_t *>(&r);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(Record, checksum); ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
inline bool valid(const Record &r) {
    if (r.magic != 0x49524631 || r.version != 1 || r.frequency < 30000 || r.frequency > 60000 ||
        !r.count || r.count > MaxTimings || r.offOnly > 1 || r.reserved ||
        !r.name[0] || !memchr(r.name, 0, sizeof(r.name)) || r.checksum != hash(r)) return false;
    uint32_t duration = 0;
    for (size_t i = 0; i < r.count; ++i) duration += r.timings[i];
    return duration > 0 && duration <= 3000000;
}
}
