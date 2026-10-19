// cISC4RegionalCity — a city as the region lists it (the subset the application object uses).
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "sc4/ISC4City.h"

class cISC4RegionalCity : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                    // +0x18
    virtual bool Shutdown() = 0;                                                // +0x20
    virtual void SetupCity(cISC4City* city) = 0;                                // +0xc8
    virtual std::uint32_t GetCitySerialNumber() const = 0;                      // +0xd8
    virtual void GetCitySaveFilePath(cIGZString& out) const = 0;                // +0xf8
};
