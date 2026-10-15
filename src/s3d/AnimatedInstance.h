// Frame animation of a model instance: cS3DModelInstanceAnimated's frame/frameset/play-mode/auto-rate logic
// (0x1001a3f2c..0x1001a4524) and cFramesetInfo::IncrementFrame (0x1001a2aec). The geometry side (loading a
// model frame into the instance) is reached through IAnimatedModelHost.
//
// Original layout: +0xa8 flags (bit0 highlighted, bit1 translucent, bit2 self-illuminated), +0xa9 highlight value,
// +0xb8 opacity, +0xc8 loaded frame, +0xca flags (bit2: self-illuminated by model), +0xd0 frameset info,
// +0xd8 zoom -> model instance table, +0xe8 current model instance id, +0xec zoom, +0xed rotation,
// +0xf0 auto frame rate (-100 = unset), +0xf4 frame accumulator, +0xf8 play mode (0 once, 2 ping-pong, 3 loop),
// +0xf9 direction (1, 0, -1), +0xfa catch-up flag.
#pragma once
#include <cstdint>
#include <vector>

namespace s3d {

constexpr float kAutoFrameRateUnset = -100.0f;      // rate not resolved yet
constexpr float kAutoFrameRateDefault = -200.0f;    // "use the model's own rate"

enum PlayMode : int { kPlayOnce = 0, kPlayPingPong = 2, kPlayLoop = 3 };

// cFramesetInfo: the frames of a model split into framesets; `starts` has one more entry than there are sets.
struct FramesetInfo {
    std::uint16_t current = 0;           // +0 frameset index
    std::uint16_t count = 0;             // +2 number of framesets
    std::uint16_t frameCount = 0;        // +4 frames in the current set
    std::vector<std::uint16_t> starts;   // +8 first frame of every set (+ end)
    std::uint16_t absoluteFrame = 0;     // +0x10 current frame, absolute
    std::uint8_t playMode = 0;           // +0x12
    std::int8_t direction = 0;           // +0x13

    bool IncrementFrame();               // always true
    std::uint16_t Start() const { return starts[current]; }
};

class IAnimatedModelHost {
public:
    virtual ~IAnimatedModelHost() = default;
    virtual int ModelFrameCount() = 0;                  // anim info +0
    virtual int ModelDefaultRate() = 0;                 // anim info +2 (frames per second * 1000)
    virtual bool LoadModelFrame(int frame) = 0;         // resource vslot 0x40
    virtual bool LoadModelInstance(std::uint32_t instanceId, int loadedFrame) = 0;   // resource lookup + SetModelResource
};

class cAnimatedInstanceState {
public:
    explicit cAnimatedInstanceState(IAnimatedModelHost* host) : host_(host) {}

    // return codes follow the original: 0 nothing changed, 2 the instance changed
    int SetFrameset(int index);
    int SetFrame(int frame);
    int GetFrame() const;
    int GetFrameCount() const;
    int IncrementFrame();
    int SetAutoFrameRate(float rate);
    int DoAutoFrameUpdate(float seconds);
    void SetPlayMode(int mode);
    int SetOpacity(bool translucent, float opacity);
    int SetHighlight(int value);
    int SetSelfIlluminated(bool on);
    int SetZoomAndRotation(int zoom, int rotation);

    FramesetInfo* framesets = nullptr;
    std::vector<std::uint32_t> zoomInstances;   // +0xd8: model instance id per zoom level
    std::uint32_t currentInstance = 0;          // +0xe8
    std::uint8_t zoom = 0xff, rotation = 0xff;  // +0xec / +0xed (both start at 0xff)
    std::uint16_t loadedFrame = 0;              // +0xc8
    std::uint8_t modelFlags = 1;                // +0xca (bit 2: model is self-illuminated)
    std::uint8_t flags = 0x00;                  // +0xa8
    std::uint8_t highlight = 0;                 // +0xa9
    float opacity = 1.0f;                       // +0xb8
    float autoFrameRate = kAutoFrameRateUnset;  // +0xf0
    float frameAccumulator = 0.0f;              // +0xf4
    std::uint8_t playMode = kPlayLoop;          // +0xf8
    std::int8_t direction = 1;                  // +0xf9
    bool catchUp = false;                       // +0xfa

private:
    IAnimatedModelHost* host_;
};

}  // namespace s3d
