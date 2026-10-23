// cSC4WinRegionClouds (13 functions at 0x1003b4380..0x1003b4a34): the drifting cloud shadows drawn over the region view. Clouds are
// 128x128 textured quads (four cloud textures 0x4a624656..0x4a624659 from the texture binding factory) that move along a
// normalised drift vector at 5..10 units per second and are recycled when they pass the right edge of the cloud area; one new
// cloud appears per paint with probability 0.003. All drawing goes through the device seam below.
#pragma once
#include <list>

#include "gz/Win.h"
#include "rz/cRZPoint.h"
#include "rz/cRZRandom.h"
#include "rz/cRZTimer.h"

namespace sc4 {

constexpr std::uint32_t kRegionCloudsWindowId = 0x6a0af41d;
constexpr std::uint32_t kRegionCloudsFirstTexture = 0x4a624656;
constexpr int kRegionCloudsTextureCount = 4;

// cS3DTextureBinding: the cloud texture; the quad draw needs its handle (first word of the object the binding points to).
class IRegionCloudTexture : public cIGZUnknown {
public:
    virtual std::uint32_t TextureHandle() = 0;
};

struct CloudVertex {
    float x, y, z;
    std::uint32_t color;
    float u, v;
};
static_assert(sizeof(CloudVertex) == 24);

// SL::spGS2 vslot 0x18: the draw device. The state calls are kept by vslot; their meaning is not decoded yet.
class IRegionCloudsDevice {
public:
    virtual ~IRegionCloudsDevice() = default;
    virtual void SetState168(int value) = 0;                        // vslot 0x168
    virtual void SetState160(int value) = 0;                        // vslot 0x160
    virtual void SetBlend(int source, int destination) = 0;         // vslot 0xa8 (4, 5)
    virtual void SetState198(int value) = 0;                        // vslot 0x198
    virtual void SetState1a0(int value) = 0;                        // vslot 0x1a0
    virtual void SetState1a8(int a, int b, int c, int d) = 0;       // vslot 0x1a8 (0, 3, 2, 1)
    // vslot 0x28 (10, 0, vertices): the device keeps reading the same four vertices for every draw below.
    virtual void SetVertices(int format, int offset, const CloudVertex* vertices) = 0;
    virtual void SetTexture(std::uint32_t handle, int stage) = 0;   // vslot 0x1d0
    virtual void SetTextureState(int stage, int state, int value) = 0;      // vslot 0xe8 (twice)
    virtual void SetSamplerState(int stage, int state, int value) = 0;      // vslot 0xd8
    virtual void DrawPrimitive(int type, int first, int count) = 0; // vslot 0x18 (1, 0, 4)
};

class IRegionCloudsHost {
public:
    virtual ~IRegionCloudsHost() = default;
    virtual IRegionCloudsDevice* Device() = 0;                      // SL::spGS2 vslot 0x18
    // cIS3DTextureBindingFactory (service 0x1ac0e11a / 0xfac0e219) vslot 0x28: one reference owned by the caller; null if missing.
    virtual IRegionCloudTexture* GetTexture(std::uint32_t id) = 0;
    virtual bool HasTextureFactory() = 0;
};
IRegionCloudsHost* RegionCloudsHost();
void SetRegionCloudsHost(IRegionCloudsHost* host);

class cSC4WinRegionClouds : public cGZWin {
public:
    struct cCloud {
        float x, y;        // position
        float vx, vy;      // velocity per second
        std::int32_t texture;
        std::uint8_t opacity;
    };

    explicit cSC4WinRegionClouds(cIGZWinMgr* mgr);    // @ 0x1003b4380 (+ 0x1003b44bc)
    ~cSC4WinRegionClouds() override;                  // @ 0x1003b44c6 (+ 0x1003b45a8 / 0x1003b45b2)

    bool Init() override;                             // @ 0x1003b45e0
    bool Shutdown() override;                         // @ 0x1003b476a
    bool GZPaint() override;                          // @ 0x1003b4a34

    void SetOffset(const cRZPoint& offset) { offset_ = offset; }         // @ 0x1003b4798
    void SetCloudArea(float left, float top, float right, float bottom);  // @ 0x1003b47a8
    void InitCloud(cCloud& cloud);                    // @ 0x1003b48ec
    void SetDriftVector(float dx, float dy);          // @ 0x1003b49dc
    void SetOpacityRange(std::uint8_t low, std::uint8_t high) { opacityLow_ = low; opacityHigh_ = high; }   // @ 0x1003b4a20

    const std::list<cCloud>& Clouds() const { return clouds_; }
    std::list<cCloud>& MutableClouds() { return clouds_; }
    cRZRandom& Random() { return random_; }

private:
    cRZTimer timer_{cRZTimer::kMilliseconds};   // +0x150
    cRZPoint offset_{0, 0};                     // +0x168
    float area_[4] = {0, 0, 0, 0};              // +0x170: left, top, right, bottom
    float drift_[2] = {0, 0};                   // +0x180
    std::uint8_t opacityLow_ = 0x20, opacityHigh_ = 0x40;   // +0x188 / +0x189
    std::list<cCloud> clouds_;                  // +0x190
    IRegionCloudTexture* textures_[kRegionCloudsTextureCount] = {};   // +0x1a8
    cRZRandom random_{0xffffffffu};             // +0x1c8
};

}  // namespace sc4
