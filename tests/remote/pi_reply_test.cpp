#include "../../src/modules/universal_remote/pi_reply.h"
#include <cassert>
#include <string>

static bool valid(const char *text) {
    JsonDocument doc;
    return !deserializeJson(doc, text) && PiReply::validChannels(doc.as<JsonVariantConst>());
}
int main() {
    assert(valid(R"([{"id":"132","name":"ORF1","streams":["orf1-at"]},{"id":"2","name":"SAT.1"}])"));
    for (const char *bad : {"", "{}", "[]", "[null]", "[1]", "[{]",
            R"([{"id":132,"name":"ORF1"}])", R"([{"id":"1"}])",
            R"([{"id":"../home","name":"bad"}])", R"([{"id":"1?x=y","name":"bad"}])",
            R"([{"id":"1","name":"bad\nname"}])", R"([{"id":"","name":"bad"}])",
            R"([{"id":"1","name":""}])", R"([{"id":"1\u0000/evil","name":"A"}])",
            R"([{"id":"1","name":"A"},{"id":"1","name":"B"}])"})
        assert(!valid(bad));
    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    for (int i = 0; i < 192; ++i) {
        JsonObject item = list.add<JsonObject>();
        item["id"] = std::to_string(i); item["name"] = "TV";
    }
    assert(PiReply::validChannels(doc.as<JsonVariantConst>()));
    JsonObject extra = list.add<JsonObject>(); extra["id"] = "193"; extra["name"] = "TV";
    assert(!PiReply::validChannels(doc.as<JsonVariantConst>()));
}
