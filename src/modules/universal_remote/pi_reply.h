#pragma once
#include <ArduinoJson.h>
#include <cstring>

namespace PiReply {
inline bool validChannels(JsonVariantConst root) {
    if (!root.is<JsonArrayConst>() || root.size() == 0 || root.size() > 192) return false;
    JsonArrayConst list = root.as<JsonArrayConst>();
    size_t index = 0;
    for (JsonVariantConst item : list) {
        if (!item["id"].is<const char *>() || !item["name"].is<const char *>()) return false;
        const char *id = item["id"], *name = item["name"];
        if (item["id"].as<JsonString>().size() != strlen(id) ||
            item["name"].as<JsonString>().size() != strlen(name)) return false;
        if (!*id || strlen(id) > 63 || !*name || strlen(name) > 95) return false;
        for (const char *p = id; *p; ++p) {
            char c = *p;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') || c == '-' || c == '_')) return false;
        }
        for (const char *p = name; *p; ++p) if (static_cast<unsigned char>(*p) < 32) return false;
        for (size_t j = 0; j < index; ++j) if (!strcmp(list[j]["id"], id)) return false;
        ++index;
    }
    return true;
}
}
