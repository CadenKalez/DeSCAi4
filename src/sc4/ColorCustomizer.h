// cSC24BitRGBColorCustomizer (8 functions, 0x10052ea2e..0x10052ecf4, iids 0x873b785a / 0x273b784c): describes and
// validates 3-byte RGB exemplar property values for the property editor. The original is a cRZRefCount object
// with two interface views at +0x10 / +0x18; this port answers both ids with the one object.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZVariant.h"
#include "rz/cRZRefCount.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISCPropertyCustomizer = 0x873b785a, kGZIID_cISCPropertyCustomizerView = 0x273b784c;

class cISCPropertyCustomizer : public cIGZUnknown {
public:
    virtual bool GetPropertyValueString(std::uint32_t propertyId, const cIGZVariant& value, cIGZString& out) = 0;
    virtual bool ValueIsValid(std::uint32_t propertyId, const cIGZVariant& value) = 0;
};

class cSC24BitRGBColorCustomizer final : public cRZRefCount, public cISCPropertyCustomizer {
public:
    static cISCPropertyCustomizer* Create() { return new cSC24BitRGBColorCustomizer(); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { cRZRefCount::AddRef(); return RefCount(); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(cRZRefCount::Release()); }
    bool GetPropertyValueString(std::uint32_t propertyId, const cIGZVariant& value, cIGZString& out) override;
    bool ValueIsValid(std::uint32_t propertyId, const cIGZVariant& value) override;
};

}  // namespace sc4
