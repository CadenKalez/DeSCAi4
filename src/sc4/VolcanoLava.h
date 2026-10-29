// cSTEVolcanoLava — the lava flow simulation of the volcano disaster (vtable 0x100aeae60, 53 functions at
// 0x100751d20..0x100754c20). This file carries the flow core, recovered from the disassembly:
//   StartLavaFlow, DoLavaFlow, GetNeighborWithLowestLavaLevel, ReadTuningParameters, the rate setters and
//   GetLavaHeightByCityCell. The drawing, steam, decal and burn code (AddDecal, AddSteamEffect, CreateBurnAroundRegion,
//   SetThingsOnFire, PrepareForDisplay, the DrawLava* family, Init/Shutdown, DoAnimationTick, ...) is not recovered; the
//   flow calls it through ILavaHost so it can be supplied later.
//
// Layout used: +0x20 flow active, +0x21 audio started, +0x22 registered with the renderer, +0x28 terrain, +0x38 cell size,
// +0x54/+0x58 lava grid width/height (vertices), +0x68 lava depth per vertex (float), +0x70 start rect {left, top, right,
// bottom}, +0x80 active bounds, +0xb0 lava vertices (0x24 bytes each, the float at +0 is the terrain height under the
// lava), +0x138 production rate (initially 0x36a7c5ac), +0x13c solidification rate, +0x1cc frame counter (initially 100).
#pragma once
#include <bit>
#include <cstdint>
#include <vector>

#include "sc4/SCPropertyHelp.h"

namespace sc4 {

struct LavaRect {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// The terrain and city services the flow touches (cSTETerrain, spRenderer, the audio manager, the unrecovered parts of
// the lava object itself).
class ILavaHost {
public:
    virtual ~ILavaHost() = default;
    // terrain
    virtual std::uint8_t GetNeighbors(std::int32_t x, std::int32_t z) = 0;          // cSTETerrain::GetNeighbors
    virtual std::int32_t NeighborOffset(int direction) = 0;                         // terrain +0xa4 + direction * 4
    virtual std::int32_t TerrainStride() = 0;                                       // terrain +0x50
    virtual std::int32_t TerrainMaxX() = 0;                                         // terrain +0x40
    virtual std::int32_t TerrainMaxZ() = 0;                                         // terrain +0x44
    virtual bool IsWaterVertex(std::int32_t index) = 0;
    virtual void RaiseVertex(std::uint32_t index, float amount) = 0;
    virtual void PadRectangle(LavaRect& rect, int amount) = 0;
    virtual void CreateVertRectAroundVertex(LavaRect& rect, int x, int z, int radius) = 0;
    virtual void TerrainRegionChanged(const LavaRect& changed) = 0;                 // terrain vslot 0x228 (1, 1, rect, 3)
    virtual void TerrainPeriodicRefresh() = 0;                                      // terrain vslot 0x278 (every 100 flows)
    virtual void TerrainRefresh() = 0;                                              // terrain vslot 0x270
    virtual cISCPropertyHolder* LoadTuningExemplar() = 0;                           // {0x6534284a, 0x88cd66e9, 5}, borrowed
    // the unrecovered parts of the lava object
    virtual void AddDecal(int x, int z) = 0;
    virtual void SetThingsOnFire(int index, float lavaDepth, float amount) = 0;
    virtual void PrepareForDisplay() = 0;
    virtual void CreateBurnAroundRegion(int x, int z) = 0;
    virtual bool RegisterWithRenderer() = 0;                                        // spRenderer vslot 0x100 (this, 5, 1000)
    virtual bool StartFlowAudio() = 0;                                              // the audio manager's vslot 0x28 (0xc0)
};

class cSTEVolcanoLava {
public:
    explicit cSTEVolcanoLava(ILavaHost* host = nullptr) : host_(host) {}

    // Tuning constants (statics in the original; defaults are the values the binary starts with).
    struct Tuning {
        float lavaColorGradientMapFactor = 6.4f;
        float maxFlowRate = 8.0000001e-7f;            // stored divided by 1e6 after loading
        float heightDiffForMaxFlowRate = 12.0f;
        float liquidToSolidConversionFactor = 0.8f;
        std::uint32_t minAlpha = 20;
        float minAlphaDepth = 0.6f, maxAlphaDepth = 2.0f;
        float scorchedTerrainDecalSize = 60.0f;
        float underWaterSolidificationMultiple = 5.0f;
        float lavaHeightBurnThreshold = 0.01f, lavaHeightDeepFryThreshold = 2.0f;
        std::uint32_t animationFrameDuration = 80000;
        float minAmountForDepositToHappen = 2.0f;
        std::uint32_t scorchDecalLifeTime = 20000000;
        float scorchDecalFadeInTime = 2.2f, scorchDecalFadeOutTime = 6.1f;
    };
    static Tuning& TuningValues();
    void ReadTuningParameters();                                  // @ 0x100754492

    // The grid the flow runs on: `width` x `height` vertices, `cellSize` metres per vertex, base heights of the lava surface.
    void SetGrid(std::int32_t width, std::int32_t height, float cellSize);
    std::vector<float>& LavaDepth() { return lava_; }
    std::vector<float>& BaseHeight() { return baseHeight_; }       // the float at +0 of each 0x24-byte lava vertex

    void SetLavaProductionRate(float rate) { productionRate_ = rate; }          // @ 0x1007547e8
    void SetSolidificationRate(float rate) { solidificationRate_ = rate; }      // @ 0x1007547f6
    float LavaProductionRate() const { return productionRate_; }
    float SolidificationRate() const { return solidificationRate_; }
    void StartLavaFlow(float x, float z, float radius, bool withSteam);          // @ 0x100754804
    void DoLavaFlow(std::uint32_t deltaTime);                                    // @ 0x10075283e
    int GetNeighborWithLowestLavaLevel(int index);                               // @ 0x100752ebc
    float GetLavaHeightByCityCell(int x, int z);                                 // @ 0x100754b46

    bool FlowActive() const { return active_; }
    bool Registered() const { return registered_; }
    bool AudioStarted() const { return audioStarted_; }
    const LavaRect& StartRect() const { return startRect_; }
    const LavaRect& Bounds() const { return bounds_; }
    int FrameCounter() const { return frameCounter_; }
    void SetActive(bool active) { active_ = active; }

private:
    ILavaHost* host_;
    bool active_ = false;                // +0x20
    bool audioStarted_ = false;          // +0x21
    bool registered_ = false;            // +0x22
    float cellSize_ = 16.0f;             // +0x38
    std::int32_t width_ = 0, height_ = 0;     // +0x54, +0x58
    std::vector<float> lava_;            // +0x68
    std::vector<float> baseHeight_;      // +0xb0 (+0)
    LavaRect startRect_;                 // +0x70
    LavaRect bounds_;                    // +0x80
    float productionRate_ = std::bit_cast<float>(0x36a7c5acu);   // +0x138
    float solidificationRate_ = 0.0f;    // +0x13c
    std::int32_t frameCounter_ = 100;    // +0x1cc
    std::int32_t flowEpoch_ = 0;         // +0xb8
};

}  // namespace sc4
