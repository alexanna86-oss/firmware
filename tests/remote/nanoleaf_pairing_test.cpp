#include "../../src/modules/universal_remote/nanoleaf_pairing.h"
#include <cassert>
#include <string>
int main() {
    std::string current = "paired-key", backup;
    int calls = 0;
    auto accepts = [&](const std::string &key) { ++calls; return key == "paired-key"; };
    assert(nanoleafSelectToken(current, backup, true, accepts) == current);
    assert(calls == 0 && backup == current);
    // DHCP changes: reauthenticate with the original credential.
    assert(nanoleafSelectToken(current, backup, false, accepts) == current);
    assert(calls == 1);
    // Wrong candidate or timeout must not destroy the sole recoverable key.
    auto rejects = [](const std::string &) { return false; };
    assert(nanoleafSelectToken(current, backup, false, rejects).empty());
    assert(backup == current);
    // After saving/reloading an unpaired selection the backup still recovers.
    std::string empty;
    assert(nanoleafSelectToken(empty, backup, false, accepts) == current);
    assert(nanoleafSelectToken(empty, backup, true, accepts) == current);
    std::string noBackup;
    assert(nanoleafSelectToken(empty, noBackup, false, accepts).empty());
    assert(noBackup.empty());
}
