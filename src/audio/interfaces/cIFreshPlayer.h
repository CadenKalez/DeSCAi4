// cIFreshPlayer — the Freshness music player's public interface. Slots 3..36 (+0x18..+0x118);
// cFreshPlayer adds the destructors at 37-38 and its own virtuals after them.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIExternalSoundSystem.h"
#include "audio/interfaces/cIFreshCellPlayer.h"
#include "audio/interfaces/cIFreshScore.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZString.h"

class cIFreshPlayer : public cIGZUnknown {
public:
    virtual bool InitializeFromLoadedScore() = 0;                                              // [3]  Init()
    virtual bool InitializeFromResourceKey(const cGZPersistResourceKey& key) = 0;              // [4]  Init(key)
    virtual bool LoadScore(const cIGZString& fileName, std::uint32_t flags) = 0;               // [5]
    virtual cIFreshScore* GetScore(std::uint32_t index) = 0;                                   // [6]  index ignored
    virtual bool Shutdown() = 0;                                                               // [7]
    virtual bool Play() = 0;                                                                   // [8]
    virtual bool IsPlaying() = 0;                                                              // [9]
    virtual bool Stop() = 0;                                                                   // [10]
    virtual bool SetVolume(std::int32_t volume) = 0;                                           // [11]
    virtual bool FadeVolume(std::int32_t durationMs, std::int32_t targetVolume, bool stopAtEnd) = 0;  // [12]
    virtual bool SetYPos(std::int32_t position) = 0;                                           // [13] 0..1000
    virtual bool SetXPos(std::int32_t position) = 0;                                           // [14] 0..1000
    virtual bool SetZoom(std::int32_t zoom) = 0;                                               // [15]
    virtual bool SetPlayPercent(std::int32_t percent) = 0;                                     // [16]
    virtual bool SetTempo(std::int32_t beatsPerMinute) = 0;                                    // [17]
    virtual std::int32_t GetYPos() = 0;                                                        // [18]
    virtual std::int32_t GetXPos() = 0;                                                        // [19]
    virtual std::int32_t GetZoom() = 0;                                                        // [20]
    virtual bool SetManual(bool manual) = 0;                                                   // [21]
    virtual std::int32_t GetFirstSelectableRow() = 0;                                          // [22]
    virtual std::int32_t GetLastSelectableRow() = 0;                                           // [23]
    virtual std::int32_t GetFirstSelectableColumn() = 0;                                       // [24]
    virtual std::int32_t GetLastSelectableColumn() = 0;                                        // [25]
    virtual cIFreshCellPlayer* GetCellPlayer(std::uint32_t row, std::uint32_t column, std::uint32_t ignored) = 0;  // [26]
    virtual bool Reseed(std::uint32_t seed) = 0;                                               // [27]
    virtual bool SetHeadRelative(bool headRelative) = 0;                                       // [28]
    virtual bool SetMinMaxDistance(std::uint32_t minimum, std::uint32_t maximum) = 0;          // [29]
    virtual bool SetNumDivisionsX(std::uint32_t count) = 0;                                    // [30]
    virtual bool SetNumDivisionsZ(std::uint32_t count) = 0;                                    // [31]
    virtual bool SetDivisionEnabled(std::uint32_t x, std::uint32_t z, bool enabled) = 0;       // [32]
    virtual bool SetDivision3DPosition(std::uint32_t x, std::uint32_t z, std::int32_t px, std::int32_t py, std::int32_t pz) = 0;  // [33]
    virtual void SetKey(const cGZPersistResourceKey& key) = 0;                                 // [34]
    virtual const cGZPersistResourceKey* GetKey() = 0;                                         // [35]
    virtual void SetExternalSoundSystemInterface(cIExternalSoundSystem* system) = 0;           // [36]
};

constexpr std::uint32_t kGZIID_cIFreshPlayer = 0xdc1cf595;  // the only id cFreshPlayer::QueryInterface accepts
