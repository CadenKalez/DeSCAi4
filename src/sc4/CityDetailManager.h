// cSC4CityDetailManager - applies the "city detail" preference to the city (class id 0xead4ba36, interface 0x6ad4b9f1 /
// 0xe4fda3d4). Its saved state is the detail level the city was last shown at; Init compares it with the preference and
// reduces or increases the detail of the city's occupants when they differ. Recovered from SimCity 4 Deluxe Edition 1.2.1
// (0x10019720a..0x1001973d0, ReduceCityDetail 0x100197412).
//
// PARTIAL: ReduceCityDetail / IncreaseCityDetail walk every occupant's model and swap its level of detail through the
// city services; the port passes them to the host. The class is a single cIGZSerializable here (the original also has a
// second interface at +8 that QueryInterface answers for 0xe4fda3d4).
#pragma once
#include <cstdint>

#include "gz/cIGZSerializable.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4CityDetailManager = 0xead4ba36;
constexpr std::uint32_t kGZIID_cISC4CityDetailManager = 0xe4fda3d4;
constexpr std::uint32_t kGZIID_cSC4CityDetailManagerPrimary = 0x6ad4b9f1;

class ICityDetailHost {
public:
    virtual ~ICityDetailHost() = default;
    virtual std::uint8_t DetailPreference() = 0;      // SL::spPreferences + 0xf16
    virtual void ReduceCityDetail() = 0;              // @ 0x100197412
    virtual void IncreaseCityDetail() = 0;            // @ 0x1001977be
};
void SetCityDetailHost(ICityDetailHost* host);

class cSC4CityDetailManager final : public cIGZSerializable {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;    // 0x10019723a
    std::uint32_t AddRef() override { return ++refs_; }             // 0x10019727c
    std::uint32_t Release() override;                               // 0x100197298
    bool Write(cIGZOStream& stream) const override;                 // 0x1001972d0
    bool Read(cIGZIStream& stream) override;                        // 0x100197328
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4CityDetailManager; }   // 0x1001973b8
    bool Init();                                                    // 0x1001973d0
    std::uint32_t DetailLevel() const { return detailLevel_; }

private:
    std::uint32_t detailLevel_ = 0xffffffff;   // +0x10 (-1: the city has never been shown)
    std::uint32_t refs_ = 0;                   // +0x14
};

}  // namespace sc4
