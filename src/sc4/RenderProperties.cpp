#include "sc4/RenderProperties.h"

#include <cctype>
#include <cstring>

namespace sc4 {

namespace {
float BitsToFloat(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
}  // namespace

const RenderPropertyRegistry& DefaultRenderProperties() {
    static const RenderPropertyRegistry registry = [] {
        RenderPropertyRegistry r;
#define RP_BOOL(key_, id_, v_, f39_, f3a_) \
        { cSC4RenderProperty<bool> p; p.id = id_; p.name = key_; p.value = (v_) != 0; p.flag39 = (f39_) != 0; p.flag3a = (f3a_) != 0; r.bools.push_back(p); }
#define RP_INT(key_, id_, v_, lo_, hi_) \
        { cSC4RenderProperty<std::int32_t> p; p.id = id_; p.name = key_; p.value = static_cast<std::int32_t>(v_); \
          p.minimum = static_cast<std::int32_t>(lo_); p.maximum = static_cast<std::int32_t>(hi_); r.ints.push_back(p); }
#define RP_FLOAT(key_, id_, v_, lo_, hi_) \
        { cSC4RenderProperty<float> p; p.id = id_; p.name = key_; p.value = BitsToFloat(v_); \
          p.minimum = BitsToFloat(lo_); p.maximum = BitsToFloat(hi_); r.floats.push_back(p); }
#include "sc4/RenderPropertyDefaults.inc"
#undef RP_BOOL
#undef RP_INT
#undef RP_FLOAT
        return r;
    }();
    return registry;
}

std::string cSC4RenderProperties::MakeShortcut(const std::string& name) {
    std::string shortcut;
    for (char c : name)
        if (std::isupper(static_cast<unsigned char>(c))) shortcut.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return shortcut;
}

template <class V>
void cSC4RenderProperties::Normalise(std::vector<V>& list) {
    for (V& p : list) {
        p.name.erase(0, 1);
        p.shortcut = MakeShortcut(p.name);
        for (char& c : p.name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

cSC4RenderProperties::cSC4RenderProperties() {
    const RenderPropertyRegistry& defaults = DefaultRenderProperties();
    bools_ = defaults.bools;
    ints_ = defaults.ints;
    floats_ = defaults.floats;
    Normalise(bools_);
    Normalise(ints_);
    Normalise(floats_);
}

}  // namespace sc4
