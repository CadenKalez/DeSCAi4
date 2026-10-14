// Hit audio engine register ids.
//
// Sound "registers" are addressed by 32-bit property ids (0xFFxxxxxx). Older content and code use
// small legacy register numbers (0x00..0x4F); nsHitReg::GetPropertyIdFromLegacyRegisterId maps
// them (table recovered verbatim from galPropertyIdFromLegacyRegisterId @ 0x100b0be80).
//
// Names below are only given where the engine's own code proves the role (cited per entry).
// Everything else is reachable through the table by its legacy number.
#pragma once
#include <cstdint>

namespace nsHitReg {

// Returns the property id for a legacy register number, or 0 when `legacyRegisterId` >= 0x50.
// Entries the original table leaves empty also return 0.
std::uint32_t GetPropertyIdFromLegacyRegisterId(std::uint32_t legacyRegisterId) noexcept;

inline constexpr std::uint32_t kLegacyRegisterCount = 0x50;

// Property ids (legacy number in the comment). Evidence: cHitChannel setters/UpdateVolPan,
// cHitTrackPlayer property accessors, cHitSem2d/3dDefault.
inline constexpr std::uint32_t kPropSoundPriority     = 0xff050a94;  // 0x13  cHitTrackPlayer::SetSoundPriority
inline constexpr std::uint32_t kPropVolumeScale       = 0xff53dbf3;  // 0x14  multiplied with volume, /1024
inline constexpr std::uint32_t kPropVolume            = 0xff90aac8;  // 0x15  cHitChannel::SetVolume
inline constexpr std::uint32_t kPropPan               = 0xff3ec880;  // 0x16  cHitChannel::SetPan
inline constexpr std::uint32_t kPropFrequency         = 0xff099db2;  // 0x17  cHitChannel::SetFrequency
inline constexpr std::uint32_t kPropEffectsType       = 0xffa9b525;  // 0x19  cHitTrackPlayer::SetEffectsType(int)
inline constexpr std::uint32_t kPropEffectsLevel      = 0xfff3edf2;  // 0x1a  cHitTrackPlayer::SetEffectsLevel(int)
inline constexpr std::uint32_t kPropDuckPriority      = 0xffae9ddb;  // 0x1b  compared with the manager's duck-map max
inline constexpr std::uint32_t kPropUse3DPositioning  = 0xffcc156f;  // 0x1c  cHitChannel::UpdateVolPan 3D branch
inline constexpr std::uint32_t kPropHeadRelative      = 0xffcaa344;  // 0x1d  cHitChannel::SetHeadRelative
inline constexpr std::uint32_t kPropMinimumDistance   = 0xffaab623;  // 0x1e  cHitChannel::SetMinimumDistance
inline constexpr std::uint32_t kPropMaximumDistance   = 0xffa6a614;  // 0x1f  cHitChannel::SetMaximumDistance
inline constexpr std::uint32_t kPropPositionX         = 0xffcc4d2d;  // 0x20  cHitChannel::SetPosition
inline constexpr std::uint32_t kPropPositionY         = 0xff4a01d6;  // 0x21
inline constexpr std::uint32_t kPropPositionZ         = 0xff469820;  // 0x22
inline constexpr std::uint32_t kPropLoopFlag          = 0xfff7b8df;  // 0x33  2nd arg of data-source Play in NoteOn
inline constexpr std::uint32_t kPropDefaultMinimumDistance = 0xffcc3b29;  // 0x38  cHitSem3dDefault: min = this or 4, max = min*500
inline constexpr std::uint32_t kPropVelocityX         = 0xff444c9a;  // 0x40  cHitChannel::SetVelocity
inline constexpr std::uint32_t kPropVelocityY         = 0xffc20061;  // 0x41
inline constexpr std::uint32_t kPropVelocityZ         = 0xffce9997;  // 0x42

}  // namespace nsHitReg
