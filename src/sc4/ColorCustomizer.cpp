#include "sc4/ColorCustomizer.h"

namespace sc4 {

bool cSC24BitRGBColorCustomizer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISCPropertyCustomizer || iid == 1 || iid == kGZIID_cISCPropertyCustomizerView) {
        *out = static_cast<cISCPropertyCustomizer*>(this);
        AddRef();
        return true;
    }
    *out = nullptr;
    return false;
}

bool cSC24BitRGBColorCustomizer::GetPropertyValueString(std::uint32_t, const cIGZVariant& value, cIGZString& out) {
    const std::uint8_t* rgb = value.RefUint8();
    out.Sprintf("Color: R:%d G:%d B:%d", rgb[0], rgb[1], rgb[2]);
    return true;
}

bool cSC24BitRGBColorCustomizer::ValueIsValid(std::uint32_t, const cIGZVariant& value) {
    return value.GetType() == (cIGZVariant::kTypeArrayFlag | cIGZVariant::kTypeUint8) && value.GetCount() == 3;
}

}  // namespace sc4
