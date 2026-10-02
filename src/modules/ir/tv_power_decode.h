#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace TvPower {
// A zero-length opposite pulse preserves mark/space polarity for long timings.
inline void appendTiming(std::vector<uint16_t> &raw, uint32_t duration) {
    while (duration > 65535) {
        raw.push_back(65535);
        raw.push_back(0);
        duration -= 65535;
    }
    raw.push_back(static_cast<uint16_t>(duration));
}
template <typename Code> std::vector<uint16_t> decode(const Code &code) {
    std::vector<uint16_t> raw;
    if (code.bitcompression < 1 || code.bitcompression > 8) return raw;
    size_t bit = 0;
    for (size_t pair = 0; pair < code.numpairs; ++pair) {
        unsigned index = 0;
        for (unsigned n = 0; n < code.bitcompression; ++n, ++bit)
            index = (index << 1) | ((code.codes[bit / 8] >> (7 - bit % 8)) & 1);
        appendTiming(raw, uint32_t(code.times[index * 2]) * 10);
        appendTiming(raw, uint32_t(code.times[index * 2 + 1]) * 10);
    }
    return raw;
}
inline uint64_t fingerprint(uint8_t frequency, const std::vector<uint16_t> &raw) {
    uint64_t hash = 14695981039346656037ULL;
    hash = (hash ^ frequency) * 1099511628211ULL;
    for (uint16_t value : raw) {
        hash = (hash ^ (value & 255)) * 1099511628211ULL;
        hash = (hash ^ (value >> 8)) * 1099511628211ULL;
    }
    return hash;
}
}
