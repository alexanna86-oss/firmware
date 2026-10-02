#include "../../src/modules/NRF24/rc_profile.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace RcProfile;
int main() {
    Scan s;
    assert(!valid(s));
    s.samples = 144; seal(s); assert(valid(s)); // A silent scan is a valid result.
    s.hits[125] = 144; s.hits[0] = 100; s.hits[63] = 100; seal(s);
    assert(valid(s));
    uint8_t channels[126]; rank(s, channels);
    assert(channels[0] == 125 && channels[1] == 0 && channels[2] == 63);
    bool seen[126] = {};
    for (auto ch : channels) { assert(ch < 126 && !seen[ch]); seen[ch] = true; }
    Scan roundtrip; memcpy(&roundtrip, &s, sizeof(s)); assert(valid(roundtrip));
    ++roundtrip.hits[1]; assert(!valid(roundtrip));
    roundtrip = s; roundtrip.hits[1] = 145; seal(roundtrip); assert(!valid(roundtrip));
    Packet p; assert(!valid(p));
    p.count = 8;
    for (auto &len : p.lengths) len = 32;
    seal(p); assert(valid(p));
    Packet q;
    for (int n : {0, 9, 255}) { q = p; q.count = n; seal(q); assert(!valid(q)); }
    q = p; q.channel = 126; seal(q); assert(!valid(q));
    q = p; q.rate = 3; seal(q); assert(!valid(q));
    q = p; q.addressSize = 2; seal(q); assert(!valid(q));
    q = p; q.width = 33; seal(q); assert(!valid(q));
    q = p; q.crc = 3; seal(q); assert(!valid(q));
    q = p; q.lengths[7] = 31; seal(q); assert(!valid(q));
    q = p; q.width = 0; q.lengths[7] = 1; seal(q); assert(valid(q));
    q.lengths[0] = 33; seal(q); assert(!valid(q));
    q = p; q.data[7][31] ^= 1; assert(!valid(q));
    // All single-byte corruption in either record must be rejected.
    for (size_t i = 0; i < sizeof(p); ++i) { q = p; reinterpret_cast<uint8_t *>(&q)[i] ^= 1; assert(!valid(q)); }
    puts("RC profile regression tests passed");
}
