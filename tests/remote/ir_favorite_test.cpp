#include "../../src/modules/ir/ir_favorite_record.h"
#include <cassert>

int main() {
    IrFavorite::Record r;
    strcpy(r.name, "TV Power"); r.count = 2; r.timings[0] = 9000; r.timings[1] = 4500;
    r.checksum = IrFavorite::hash(r);
    assert(IrFavorite::valid(r));
    assert(!r.offOnly); // Learned/captured power is never implicitly a discrete OFF.
    auto bad = r; bad.count = 513; bad.checksum = IrFavorite::hash(bad); assert(!IrFavorite::valid(bad));
    bad = r; bad.offOnly = 2; bad.checksum = IrFavorite::hash(bad); assert(!IrFavorite::valid(bad));
    bad = r; bad.frequency = 0; bad.checksum = IrFavorite::hash(bad); assert(!IrFavorite::valid(bad));
    bad = r; bad.timings[0] ^= 1; assert(!IrFavorite::valid(bad));
    bad = r; memset(bad.name, 'X', sizeof(bad.name)); bad.checksum = IrFavorite::hash(bad); assert(!IrFavorite::valid(bad));
    bad = r; bad.count = 512;
    for (auto &t : bad.timings) t = 65535;
    bad.checksum = IrFavorite::hash(bad); assert(!IrFavorite::valid(bad));
    r.offOnly = 1; r.checksum = IrFavorite::hash(r); assert(IrFavorite::valid(r));
}
