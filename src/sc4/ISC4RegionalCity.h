// cISC4RegionalCity — a city as the region lists it (iid 0x69d2be1d, secondary message-target view
// 0x452294aa and serializable view 0xe4fda3d4).
//
// The interface originally has 76 slots (0x18..0x270); this header keeps the five methods the rest of the
// port calls through the interface (the application object, the region and the save-game helpers). The full
// slot list lives on cSC4RegionalCity (src/sc4/RegionalCity.h), which documents every original offset; the
// remaining methods are not declared here so that the small test fakes of this interface stay small.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "sc4/ISC4City.h"

constexpr std::uint32_t kGZIID_cISC4RegionalCity = 0x69d2be1d;

class cISC4RegionalCity : public cIGZUnknown {
public:
    // +0x240 GetPointsOfInterest: a 12-byte point in the city (x, y, z).
    struct PointOfInterest {
        float x = 0, y = 0, z = 0;
    };
    // The original enum; point-of-interest type 0 is the airport list, 1 the seaport list.
    enum PointOfInterestType : std::int32_t { kPointOfInterestAirport = 0, kPointOfInterestSeaport = 1 };

    virtual bool Init() = 0;                                                    // +0x18
    virtual bool Shutdown() = 0;                                                // +0x20
    virtual void SetupCity(cISC4City* city) = 0;                                // +0xc8
    virtual std::uint32_t GetCitySerialNumber() const = 0;                      // +0xd8
    virtual void GetCitySaveFilePath(cIGZString& out) const = 0;                // +0xf8
};
