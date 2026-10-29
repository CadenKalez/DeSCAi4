// cSTEVolcanoLava flow core — see sc4/VolcanoLava.h. The float expressions follow the disassembly (Ghidra regroups
// them), including the minss/ucomiss operand order, which decides what happens with NaN and with ties.
#pragma STDC FP_CONTRACT OFF
#include "sc4/VolcanoLava.h"

#include <cmath>
#include <cstring>

namespace sc4 {

namespace {
// minss dst, src: dst when dst < src, otherwise src.
inline float MinSS(float dst, float src) { return dst < src ? dst : src; }
}  // namespace

cSTEVolcanoLava::Tuning& cSTEVolcanoLava::TuningValues() {
    static Tuning tuning;
    return tuning;
}

void cSTEVolcanoLava::ReadTuningParameters() {
    if (!host_) return;
    cISCPropertyHolder* holder = host_->LoadTuningExemplar();
    if (!holder) return;
    Tuning& t = TuningValues();
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e60, t.lavaColorGradientMapFactor);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e61, t.maxFlowRate);
    t.maxFlowRate = t.maxFlowRate / 1e+06f;
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e62, t.heightDiffForMaxFlowRate);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e63, t.liquidToSolidConversionFactor);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e64, t.minAlpha);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e65, t.minAlphaDepth);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e66, t.maxAlphaDepth);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e67, t.scorchedTerrainDecalSize);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e68, t.underWaterSolidificationMultiple);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e69, t.lavaHeightBurnThreshold);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e6a, t.lavaHeightDeepFryThreshold);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e6b, t.animationFrameDuration);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e6c, t.minAmountForDepositToHappen);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e6d, t.scorchDecalLifeTime);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e6e, t.scorchDecalFadeInTime);
    cSCPropertyHelp::GetPropertyValue(holder, 0x69987e6f, t.scorchDecalFadeOutTime);
}

void cSTEVolcanoLava::SetGrid(std::int32_t width, std::int32_t height, float cellSize) {
    width_ = width;
    height_ = height;
    cellSize_ = cellSize;
    lava_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    baseHeight_.assign(lava_.size(), 0.0f);
}

float cSTEVolcanoLava::GetLavaHeightByCityCell(int x, int z) {
    if (lava_.empty()) return 0.0f;
    const int index = z * width_ + x;
    // the original allows index == width * height, which reads one float past the grid; that reads 0 here
    if (-1 < index && index <= width_ * height_)
        return static_cast<std::size_t>(index) < lava_.size() ? lava_[static_cast<std::size_t>(index)] : 0.0f;
    return 0.0f;
}

int cSTEVolcanoLava::GetNeighborWithLowestLavaLevel(int index) {
    const std::uint32_t mask = host_->GetNeighbors(index % width_, index / width_);
    const long base = index;
    const float here = lava_[static_cast<std::size_t>(base)] + baseHeight_[static_cast<std::size_t>(base)];
    static const float kRoot2 = 1.4142135f;
    float best = 0.0f;
    int pick = -1;
    for (int direction = 0; direction <= 6; direction += 2) {                  // the straight neighbours
        if (((mask & 0xff) >> direction) & 1) {
            const long other = host_->NeighborOffset(direction) + base;
            const float drop = here - (lava_[static_cast<std::size_t>(other)] + baseHeight_[static_cast<std::size_t>(other)]);
            if (best < drop) {
                pick = direction;
                best = drop;
            }
        }
    }
    best = best * kRoot2;                                                       // a diagonal has to beat that
    for (int direction = 1; direction <= 7; direction += 2) {
        if (((mask & 0xff) >> direction) & 1) {
            const long other = host_->NeighborOffset(direction) + base;
            const float drop = here - (lava_[static_cast<std::size_t>(other)] + baseHeight_[static_cast<std::size_t>(other)]);
            if (best < drop) {
                pick = direction;
                best = drop;
            }
        }
    }
    return pick;
}

void cSTEVolcanoLava::StartLavaFlow(float x, float z, float radius, bool withSteam) {
    // round-to-nearest-even through the 1.5 * 2^23 trick, then the tie fix: this is a floor for finite values
    auto cell = [](float position) {
        const float scaled = position * 0.0625f;
        const float shifted = scaled + -0.5f + 12582912.0f;
        const std::int32_t bits = std::bit_cast<std::int32_t>(shifted);
        const std::int32_t low = bits - 0x4b400000;
        std::int32_t result = bits - 0x4b3fffff;
        const float candidate = static_cast<float>(result);
        if (scaled != candidate) result = low;
        if (std::isnan(scaled) || std::isnan(candidate)) result = low;
        return result;
    };
    const std::int32_t cellX = cell(x);
    const std::int32_t cellZ = cell(z);
    host_->CreateVertRectAroundVertex(startRect_, cellX, cellZ, static_cast<int>(radius / cellSize_));
    bounds_ = startRect_;
    flowEpoch_ = 0;
    host_->CreateBurnAroundRegion(cellX, cellZ);
    if (!registered_) registered_ = host_->RegisterWithRenderer();
    if (withSteam && !audioStarted_) audioStarted_ = host_->StartFlowAudio();
}

void cSTEVolcanoLava::DoLavaFlow(std::uint32_t deltaTime) {
    float production = productionRate_;
    LavaRect dirty{0x7fffffff, 0x7fffffff, static_cast<std::int32_t>(0x80000000), static_cast<std::int32_t>(0x80000000)};
    if (!active_) return;
    const Tuning& t = TuningValues();
    const float dt = static_cast<float>(deltaTime);
    const float solid = solidificationRate_ * dt;
    const float underwater = t.underWaterSolidificationMultiple * solid;

    // new lava at the vent
    if (startRect_.top <= startRect_.bottom) {
        production = production * dt;
        for (std::int32_t z = startRect_.top; z <= startRect_.bottom; ++z) {
            for (std::int32_t x = startRect_.left; x <= startRect_.right; ++x) {
                float& cellDepth = lava_[static_cast<std::size_t>(width_ * z + x)];
                cellDepth = cellDepth + production;
                if (x < bounds_.left) bounds_.left = x;
                if (bounds_.right < x) bounds_.right = x;
                if (z < bounds_.top) bounds_.top = z;
                if (bounds_.bottom < z) bounds_.bottom = z;
            }
        }
    }

    // flow, solidification and deposit over the active area
    if (bounds_.top <= bounds_.bottom) {
        const float cap = MinSS(1.0f, solid);
        const float capUnderwater = MinSS(1.0f, underwater);
        const float edgeLoss = 1.0000000116860974e-07f * dt;                    // 0x33d6bf95
        const std::int32_t stride = host_->TerrainStride();
        for (std::int32_t z = bounds_.top; z <= bounds_.bottom; ++z) {
            if (bounds_.left > bounds_.right) continue;
            for (std::int32_t x = bounds_.left; x <= bounds_.right; ++x) {
                const std::int32_t index = width_ * z + x;
                const std::size_t slot = static_cast<std::size_t>(index);
                float depth = lava_[slot];
                if (0.0f >= depth) continue;
                const std::uint32_t row = static_cast<std::uint32_t>(index) / static_cast<std::uint32_t>(stride);
                const std::int32_t rowStart = static_cast<std::int32_t>(row) * stride;
                float flow;
                int target = -1;
                bool measured = true;
                if (index - rowStart == 0 || static_cast<std::uint32_t>(index) < static_cast<std::uint32_t>(stride) ||
                    index - rowStart == host_->TerrainMaxX() || static_cast<std::int32_t>(row) == host_->TerrainMaxZ()) {
                    flow = depth * edgeLoss;                                    // lava at the rim of the terrain drains away
                } else {
                    const int direction = GetNeighborWithLowestLavaLevel(index);
                    if (direction < 0) {
                        measured = false;
                        flow = 0.0f;
                    } else {
                        target = index + host_->NeighborOffset(direction);
                        const std::size_t other = static_cast<std::size_t>(target);
                        float drop = (baseHeight_[slot] + depth) - (lava_[other] + baseHeight_[other]);
                        drop = drop * 0.5f;
                        float rate = t.maxFlowRate;
                        if (!(drop >= t.heightDiffForMaxFlowRate)) rate = (rate * drop) / t.heightDiffForMaxFlowRate;
                        const float movable = MinSS(depth, drop);
                        const float wanted = (rate * movable) * dt;
                        flow = MinSS(movable, wanted);
                    }
                }
                if (measured && flow > 0.0f) {
                    if (target >= 0) {
                        const std::size_t other = static_cast<std::size_t>(target);
                        host_->SetThingsOnFire(target, lava_[other], flow);
                        lava_[other] = lava_[other] + flow;
                    }
                    lava_[slot] = lava_[slot] - flow;
                }
                if (solid > 0.0f) {
                    const float remaining = lava_[slot];
                    if (t.minAmountForDepositToHappen <= remaining || std::isnan(remaining)) {
                        const bool water = host_->IsWaterVertex(index);
                        const float settled = (water ? capUnderwater : cap) * lava_[slot];
                        lava_[slot] = lava_[slot] - settled;
                        host_->AddDecal(x, z);
                        host_->RaiseVertex(static_cast<std::uint32_t>(index), settled * t.liquidToSolidConversionFactor);
                        if (x < dirty.left) dirty.left = x;
                        if (dirty.right < x) dirty.right = x;
                        if (z < dirty.top) dirty.top = z;
                        if (dirty.bottom < z) dirty.bottom = z;
                    } else {
                        lava_[slot] = (remaining < 0.1f) ? 0.0f : remaining * 0.9f;
                    }
                }
            }
        }
    }
    host_->PadRectangle(bounds_, 1);
    host_->PrepareForDisplay();
    if (dirty.left <= dirty.right && dirty.top <= dirty.bottom) {
        host_->TerrainRegionChanged(dirty);
        const std::int32_t counter = frameCounter_;
        frameCounter_ = counter + 1;
        if (counter >= 99) {
            host_->TerrainPeriodicRefresh();
            frameCounter_ = 0;
        }
        host_->TerrainRefresh();
    }
}

}  // namespace sc4
