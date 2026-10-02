#include <cstdint>
#include "../../src/modules/ir/WORLD_IR_CODES.h"
#include "../../src/modules/ir/tv_power_decode.h"
#include <cassert>
#include <set>

int main() {
    // MSB-first packed indexes 0, 1, 2, 3 across a byte boundary.
    const uint16_t timings[] = {1, 2, 3, 4, 5, 6, 7000, 8};
    const uint8_t packed[] = {0x05, 0x30};
    IrCode test{38, 4, 3, timings, packed};
    auto raw = TvPower::decode(test);
    assert((raw == std::vector<uint16_t>{10, 20, 30, 40, 50, 60, 65535, 0, 4465, 80}));
    assert(TvPower::fingerprint(38, raw) != TvPower::fingerprint(40, raw));
    std::set<uint64_t> seen;
    size_t count = 0;
    for (const IrCode *code : EUpowerCodes) {
        raw = TvPower::decode(*code);
        assert(!raw.empty() && raw.size() % 2 == 0);
        seen.insert(TvPower::fingerprint(code->timer_val, raw));
        ++count;
    }
    for (const IrCode *code : NApowerCodes) {
        raw = TvPower::decode(*code);
        assert(!raw.empty() && raw.size() % 2 == 0);
        seen.insert(TvPower::fingerprint(code->timer_val, raw));
        ++count;
    }
    assert(seen.size() < count); // Shared EU/NA codes must only transmit once.
}
