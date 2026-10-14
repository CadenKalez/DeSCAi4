// cIFreshCell — one cell of a "Freshness" music score: which sample plays, how loud / where in the
// stereo field, fade times, probability and looping. Slots 3..42 (byte offsets +0x18..+0x150),
// names verbatim from the vtable.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"

class cIFreshCell : public cIGZUnknown {
public:
    virtual std::uint32_t GetSndId() const = 0;              // [3]
    virtual cIGZString* GetFilename() = 0;                   // [4]
    virtual bool GetUseFastTimer() const = 0;                // [5]
    virtual std::int32_t GetVolume() const = 0;              // [6]
    virtual std::int32_t GetPanX() const = 0;                // [7]
    virtual std::int32_t GetPanZ() const = 0;                // [8]
    virtual bool GetVolIsRandom() const = 0;                 // [9]
    virtual bool GetPanIsRandom() const = 0;                 // [10]
    virtual std::int32_t GetFadeInTime() const = 0;          // [11]
    virtual std::int32_t GetFadeOutTime() const = 0;         // [12]
    virtual std::int32_t GetStartDelay() const = 0;          // [13]
    virtual std::int32_t GetProbability() const = 0;         // [14]
    virtual bool GetDelayIsRandom() const = 0;               // [15]
    virtual std::int32_t GetLoopLength() const = 0;          // [16]
    virtual std::int32_t GetQuantize() const = 0;            // [17]
    virtual bool GetSampleLooped() const = 0;                // [18]
    virtual std::int32_t GetRandPitchShiftLow() const = 0;   // [19]
    virtual std::int32_t GetRandPitchShiftHigh() const = 0;  // [20]
    virtual std::uint32_t GetGroupId() const = 0;            // [21]
    virtual bool GetStereoSample() const = 0;                // [22]
    virtual bool SetSndId(std::uint32_t value) = 0;          // [23]
    virtual bool SetFilename(const cIGZString& value) = 0;   // [24]
    virtual bool SetUseFastTimer(bool value) = 0;            // [25]
    virtual bool SetVolume(std::int32_t value) = 0;          // [26]
    virtual bool SetPanX(std::int32_t value) = 0;            // [27]
    virtual bool SetPanZ(std::int32_t value) = 0;            // [28]
    virtual bool SetVolIsRandom(bool value) = 0;             // [29]
    virtual bool SetPanIsRandom(bool value) = 0;             // [30]
    virtual bool SetFadeInTime(std::int32_t value) = 0;      // [31]
    virtual bool SetFadeOutTime(std::int32_t value) = 0;     // [32]
    virtual bool SetStartDelay(std::int32_t value) = 0;      // [33]
    virtual bool SetProbability(std::int32_t value) = 0;     // [34]
    virtual bool SetDelayIsRandom(bool value) = 0;           // [35]
    virtual bool SetLoopLength(std::int32_t value) = 0;      // [36]
    virtual bool SetQuantize(std::int32_t value) = 0;        // [37]
    virtual bool SetSampleLooped(bool value) = 0;            // [38]
    virtual bool SetRandPitchShiftLow(std::int32_t value) = 0;   // [39]
    virtual bool SetRandPitchShiftHigh(std::int32_t value) = 0;  // [40]
    virtual bool SetGroupId(std::uint32_t value) = 0;        // [41]
    virtual bool SetStereoSample(bool value) = 0;            // [42]
};

constexpr std::uint32_t kGZIID_cIFreshCell = 0x1dd9c9de;
