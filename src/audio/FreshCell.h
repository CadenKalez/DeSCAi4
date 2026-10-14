// cFreshCell — plain data record behind cIFreshCell. Recovered from Sim City 4 Deluxe Editionsub
// 1.2.1 (vtable 0x100ab7360). Original layout (0x98): +0x08 refcount, +0x0c snd id, +0x10 filename
// (cRZString), +0x38 use-fast-timer, +0x3c volume, +0x40 pan x, +0x44 pan z, +0x48/+0x49 volume/pan
// random flags, +0x4c fade in, +0x50 fade out, +0x54 start delay, +0x58 probability, +0x5c delay-is-
// random, +0x60 loop length, +0x64 quantize, +0x68 sample looped, +0x6c/+0x70 random pitch shift
// low/high, +0x74 group id, +0x78 stereo sample, +0x80 std::list<cRZPoint> of connections.
//
// Defaults (cFreshCell::cFreshCell @ 0x1004c9fe0): volume 1024, pan x/z 512, loop length 8,
// sample looped true, everything else zero (the original leaves probability uninitialised; the
// port zeroes it).
#pragma once
#include <cstdint>
#include <vector>

#include "audio/interfaces/cIFreshCell.h"
#include "rz/cRZPoint.h"
#include "rz/cRZString.h"

class cFreshCell final : public cIFreshCell {
public:
    // cFreshCell::Create() @ 0x10043ac62: heap object with refcount 0.
    static cFreshCell* Create() { return new cFreshCell(); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;  // deletes when the count is <= 1

    std::uint32_t GetSndId() const override { return sndId_; }
    cIGZString* GetFilename() override { return &filename_; }
    bool GetUseFastTimer() const override { return useFastTimer_; }
    std::int32_t GetVolume() const override { return volume_; }
    std::int32_t GetPanX() const override { return panX_; }
    std::int32_t GetPanZ() const override { return panZ_; }
    bool GetVolIsRandom() const override { return volIsRandom_; }
    bool GetPanIsRandom() const override { return panIsRandom_; }
    std::int32_t GetFadeInTime() const override { return fadeInTime_; }
    std::int32_t GetFadeOutTime() const override { return fadeOutTime_; }
    std::int32_t GetStartDelay() const override { return startDelay_; }
    std::int32_t GetProbability() const override { return probability_; }
    bool GetDelayIsRandom() const override { return delayIsRandom_; }
    std::int32_t GetLoopLength() const override { return loopLength_; }
    std::int32_t GetQuantize() const override { return quantize_; }
    bool GetSampleLooped() const override { return sampleLooped_; }
    std::int32_t GetRandPitchShiftLow() const override { return randPitchShiftLow_; }
    std::int32_t GetRandPitchShiftHigh() const override { return randPitchShiftHigh_; }
    std::uint32_t GetGroupId() const override { return groupId_; }
    bool GetStereoSample() const override { return stereoSample_; }

    bool SetSndId(std::uint32_t v) override { sndId_ = v; return true; }
    bool SetFilename(const cIGZString& v) override { filename_.AssignFromIGZString(v); return true; }
    bool SetUseFastTimer(bool v) override { useFastTimer_ = v; return true; }
    bool SetVolume(std::int32_t v) override { volume_ = v; return true; }
    bool SetPanX(std::int32_t v) override { panX_ = v; return true; }
    bool SetPanZ(std::int32_t v) override { panZ_ = v; return true; }
    bool SetVolIsRandom(bool v) override { volIsRandom_ = v; return true; }
    bool SetPanIsRandom(bool v) override { panIsRandom_ = v; return true; }
    bool SetFadeInTime(std::int32_t v) override { fadeInTime_ = v; return true; }
    bool SetFadeOutTime(std::int32_t v) override { fadeOutTime_ = v; return true; }
    bool SetStartDelay(std::int32_t v) override { startDelay_ = v; return true; }
    bool SetProbability(std::int32_t v) override { probability_ = v; return true; }
    bool SetDelayIsRandom(bool v) override { delayIsRandom_ = v; return true; }
    bool SetLoopLength(std::int32_t v) override { loopLength_ = v; return true; }
    bool SetQuantize(std::int32_t v) override { quantize_ = v; return true; }
    bool SetSampleLooped(bool v) override { sampleLooped_ = v; return true; }
    bool SetRandPitchShiftLow(std::int32_t v) override { randPitchShiftLow_ = v; return true; }
    bool SetRandPitchShiftHigh(std::int32_t v) override { randPitchShiftHigh_ = v; return true; }
    bool SetGroupId(std::uint32_t v) override { groupId_ = v; return true; }
    bool SetStereoSample(bool v) override { stereoSample_ = v; return true; }

    // Connections to other cells ("x,y" pairs in the score text).
    std::vector<cRZPoint>& connections() { return connections_; }
    const std::vector<cRZPoint>& connections() const { return connections_; }

private:
    cFreshCell() = default;
    virtual ~cFreshCell() = default;

    std::int32_t refCount_ = 0;
    std::uint32_t sndId_ = 0;
    cRZString filename_;
    bool useFastTimer_ = false;
    std::int32_t volume_ = 1024;
    std::int32_t panX_ = 512;
    std::int32_t panZ_ = 512;
    bool volIsRandom_ = false;
    bool panIsRandom_ = false;
    std::int32_t fadeInTime_ = 0;
    std::int32_t fadeOutTime_ = 0;
    std::int32_t startDelay_ = 0;
    std::int32_t probability_ = 0;
    bool delayIsRandom_ = false;
    std::int32_t loopLength_ = 8;
    std::int32_t quantize_ = 0;
    bool sampleLooped_ = true;
    std::int32_t randPitchShiftLow_ = 0;
    std::int32_t randPitchShiftHigh_ = 0;
    std::uint32_t groupId_ = 0;
    bool stereoSample_ = false;
    std::vector<cRZPoint> connections_;
};
