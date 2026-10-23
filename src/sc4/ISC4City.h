// cISC4City — the city interface (iid 0x66ba4db5) as far as the application object uses it. cSC4City implements it; the
// slots the app calls are the ones listed here (vtable offsets of the original in the comments).
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"

class cIGZPersistDBSegment;

class cISC4City : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                    // +0x18
    virtual bool Shutdown() = 0;                                                // +0x20
    // +0x28 / +0x60: the serial number and the name the save path of the city is built from. Both have a default so
    // the city doubles that only model the life cycle keep compiling; cSC4City overrides them.
    virtual std::uint32_t GetCitySerialNumber() const { return 0; }
    virtual void GetCityName(cIGZString& out) const { out.AssignFromCString(""); }
    virtual void SetCitySerialNumber(std::uint32_t serialNumber) = 0;           // +0x30
    virtual void GetCitySaveFilePath(cIGZString& out) const = 0;                // +0x50
    virtual void SetCitySaveFilePath(const cIGZString& path) = 0;               // +0x58
    virtual void SetCityNameChanged(bool changed) = 0;                          // +0x78
    // +0xb8: DoCityObliteration marks the city (the old one before it closes, the empty one it saves) as not established.
    virtual void SetEstablished(bool /*established*/) {}
    virtual bool IsSaveDisabled() const = 0;                                    // +0x348
    // +0x368: write the empty ("obliterated") city over the segment DoCityObliteration opened.
    virtual bool SaveObliterated(cIGZPersistDBSegment* /*segment*/) const { return false; }
};
