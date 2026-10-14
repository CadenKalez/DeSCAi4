// Class ids (CLSIDs), director ids and resource type ids of the Hit/Fresh audio subsystem, as
// registered by cHitManCOMDirector @ 0x10043adc2 and cFreshnessCOMDirector @ 0x10043aafe.
#pragma once
#include <cstdint>

namespace HitIds {

// --- cHitManCOMDirector (director id 0x9b2d3d2f) -------------------------------------------
inline constexpr std::uint32_t kHitManDirectorId                       = 0x9b2d3d2f;
inline constexpr std::uint32_t kClsidHitMan                            = 0x7b1bd993;
inline constexpr std::uint32_t kClsidHitDebugInfo                      = 0x3bc9e6e0;
inline constexpr std::uint32_t kClsidHitResourceFactory                = 0x5b945038;
inline constexpr std::uint32_t kClsidHitSegmentTrackCreator            = 0xdc45dcb1;
inline constexpr std::uint32_t kClsidHitSegmentDataSourceChannelCreator = 0x2b2eb869;
inline constexpr std::uint32_t kClsidHitTrackPlayer                    = 0xa3cd96cf;
inline constexpr std::uint32_t kClsidHitChannel                        = 0x3b1ea5be;
inline constexpr std::uint32_t kClsidHitIniFile                        = 0x3d802d67;
inline constexpr std::uint32_t kClsidPropertySetUint32XmlReader        = 0xaba06ef4;
inline constexpr std::uint32_t kClsidHitList                           = 0x7b1acfcd;

// --- cFreshnessCOMDirector (director id 0xfd53ff17) ------------------------------------------
inline constexpr std::uint32_t kFreshnessDirectorId                    = 0xfd53ff17;
inline constexpr std::uint32_t kClsidFreshPlayer                       = 0x3c1cf59e;
inline constexpr std::uint32_t kClsidFreshScore                        = 0x5c1cf5a3;
inline constexpr std::uint32_t kClsidFreshDimensionPlayer              = 0xbdda72dd;
inline constexpr std::uint32_t kClsidFreshCell                         = 0x7dd9c9f2;
inline constexpr std::uint32_t kClsidFreshCellPlayer                   = 0xbdd9d7a5;

// --- persisted resource types the cHitResourceFactory handles ---------------------------------
// (registered with the resource manager in cHitManCOMDirector::PreAppInit @ 0x10043b22a)
inline constexpr std::uint32_t kResTypeTrackPlayer                     = 0xa3cd96cf;  // cHitResource<cHitTrackPlayer>
inline constexpr std::uint32_t kResTypeHitList                         = 0x7b1acfcd;  // cHitResource<cHitList>
inline constexpr std::uint32_t kResTypePropertySetA                    = 0xebfee345;  // cHitPropertySetResource
inline constexpr std::uint32_t kResTypePropertySetB                    = 0xebfee349;  // cHitPropertySetResource
inline constexpr std::uint32_t kResTypeTrackPropertySetA               = 0x0b8d821a;  // cHitPropertySetTrackResource
inline constexpr std::uint32_t kResTypeTrackPropertySetB               = 0x0b9eb87e;  // cHitPropertySetTrackResource
inline constexpr std::uint32_t kResTypeRegisteredButUnhandled          = 0xdb9a4f83;  // registered; CreateInstance returns false

// --- interface ids used by the Hit classes' QueryInterface ----------------------------------------
inline constexpr std::uint32_t kIidHitResource                         = 0x456b8f1d;  // cIHitResource
inline constexpr std::uint32_t kIidHitSem                              = 0xddcf3bd7;
inline constexpr std::uint32_t kIidHitList                             = 0xdb1acfb2;
inline constexpr std::uint32_t kIidHitTrackDataBlock                   = 0x9b82eae7;
inline constexpr std::uint32_t kIidHitResourceFactory                  = 0xa56b8f17;
inline constexpr std::uint32_t kIidPersistResource                     = 0x3b93c5e7;
inline constexpr std::uint32_t kIidHitTrackPlayer                      = 0xbb1be367;  // cIHitTrackPlayer (object start)
inline constexpr std::uint32_t kIidSndMultipleSources                  = 0xdd583e2d;  // cHitTrackPlayer +0x30
inline constexpr std::uint32_t kIidSndCompletionNotification            = 0xe9d00a9a;  // cHitTrackPlayer +0x38
inline constexpr std::uint32_t kIidRecordAccess                        = 0xc56b8f08;  // sized-record view used by ReadResource

}  // namespace HitIds
