// cISC4City — the city interface (iid 0x66ba4db5) as far as the application object uses it. cSC4City implements it; the
// slots the app calls are the ones listed here (vtable offsets of the original in the comments).
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"

class cISC4City : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                    // +0x18
    virtual bool Shutdown() = 0;                                                // +0x20
    virtual void SetCitySerialNumber(std::uint32_t serialNumber) = 0;           // +0x30
    virtual void GetCitySaveFilePath(cIGZString& out) const = 0;                // +0x50
    virtual void SetCitySaveFilePath(const cIGZString& path) = 0;               // +0x58
    virtual void SetCityNameChanged(bool changed) = 0;                          // +0x78
    virtual bool IsSaveDisabled() const = 0;                                    // +0x348
};
