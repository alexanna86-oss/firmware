#pragma once

// Keep the last paired credential even if discovery selects another device or
// authentication times out. A different endpoint must prove it accepts the key.
template <typename Text, typename Authenticate>
Text nanoleafSelectToken(const Text &current, Text &backup, bool sameEndpoint, Authenticate authenticate) {
    if (current.length()) backup = current;
    if (sameEndpoint && current.length()) return current;
    if (backup.length() && authenticate(backup)) return backup;
    return Text();
}
