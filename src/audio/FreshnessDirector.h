// cFreshnessCOMDirector — the COM director of the Freshness music library. Its only job is to
// register the five Fresh classes with the COM framework under director id 0xfd53ff17
// (constructor @ 0x10043aafe; GetDirector() @ 0x10043aa60 is the Meyers singleton; OnStart
// @ 0x10043acec does nothing and returns true). The cRZCOMDllDirector base is not recovered yet;
// this table is what the constructor feeds to its AddCls().
#pragma once
#include <array>
#include <cstdint>

#include "audio/FreshCell.h"
#include "audio/FreshCellPlayer.h"
#include "audio/FreshDimensionPlayer.h"
#include "audio/FreshPlayer.h"
#include "audio/FreshScore.h"
#include "audio/HitClassIds.h"

namespace Fresh {

struct ClassFactory {
    std::uint32_t classId;
    cIGZUnknown* (*create)();
};

inline constexpr std::uint32_t kDirectorId = HitIds::kFreshnessDirectorId;  // GetDirectorID() @ 0x10043acfc

// Order of the AddCls() calls in the constructor.
inline const std::array<ClassFactory, 5> kClassFactories = {{
    {HitIds::kClsidFreshPlayer, []() -> cIGZUnknown* { return static_cast<cIFreshPlayer*>(cFreshPlayer::Create()); }},
    {HitIds::kClsidFreshScore, []() -> cIGZUnknown* { return static_cast<cIFreshScore*>(cFreshScore::Create()); }},
    {HitIds::kClsidFreshDimensionPlayer, []() -> cIGZUnknown* { return cFreshDimensionPlayer::Create(); }},
    {HitIds::kClsidFreshCell, []() -> cIGZUnknown* { return static_cast<cIFreshCell*>(cFreshCell::Create()); }},
    {HitIds::kClsidFreshCellPlayer, []() -> cIGZUnknown* { return static_cast<cIFreshCellPlayer*>(cFreshCellPlayer::Create()); }},
}};

}  // namespace Fresh
