#include "render/PrimitivePool.h"

#include <cstring>

#include "render/SimGL.h"

namespace {
constexpr std::uint32_t kStride = 0x18;   // V3F_C4UB_T2F
constexpr std::uint32_t kMaxPerDraw = 0x600;

void FillQuadVertices(void* vertices, std::uint32_t first, std::uint32_t end, std::uint32_t corner,
                      std::uint32_t uBase, bool flipU) {
    auto* p = static_cast<std::uint8_t*>(vertices) + static_cast<std::size_t>(first) * kStride;
    for (std::uint32_t i = first; i != end; ++i, p += kStride, ++corner) {
        const std::uint32_t white = 0xffffffffu;
        std::memcpy(p + 0xc, &white, 4);
        std::uint32_t uBit = ((corner - uBase) >> 1) & 1;
        if (flipU) uBit = (~((corner - uBase) >> 1)) & 1;
        const float u = static_cast<float>(uBit);
        const float v = static_cast<float>((corner >> 1) & 1);
        std::memcpy(p + 0x10, &u, 4);
        std::memcpy(p + 0x14, &v, 4);
    }
}
}  // namespace

void cSC4PrimitivePool::QuadVertexInit_V3F_C4UB_T2F(void* vertices, std::uint32_t first, std::uint32_t end) {
    FillQuadVertices(vertices, first, end, 1, 1, false);
}

void cSC4PrimitivePool::VFlip_QuadVertexInit_V3F_C4UB_T2F(void* vertices, std::uint32_t first, std::uint32_t end) {
    FillQuadVertices(vertices, first, end, 3, 3, false);
}

void cSC4PrimitivePool::HFlip_QuadVertexInit_V3F_C4UB_T2F(void* vertices, std::uint32_t first, std::uint32_t end) {
    FillQuadVertices(vertices, first, end, 1, 1, true);
}

void cSC4PrimitivePool::QuadTriListIndexInit(std::uint16_t* indices, std::uint32_t first, std::uint32_t end) {
    if (first == end) return;
    std::uint32_t quad = (first << 2) / 6;
    for (std::uint16_t* p = indices + first; p != indices + end; p += 6, quad += 4) {
        const auto q = static_cast<std::uint16_t>(quad);
        p[0] = q;
        p[1] = static_cast<std::uint16_t>(q + 1);
        p[2] = static_cast<std::uint16_t>(q + 2);
        p[3] = q;
        p[4] = static_cast<std::uint16_t>(q + 2);
        p[5] = static_cast<std::uint16_t>(q + 3);
    }
}

void cSC4PrimitivePool::Draw(IPrimitiveDrawTarget& target) {
    target.SetVertexBuffer(vertexFormat_, verticesUsed_, vertices_.data());
    if (indicesUsed_ != 0) {
        std::uint32_t remaining = indicesUsed_;
        while (remaining > kMaxPerDraw) {
            target.DrawPrimsIndexed(primType_, static_cast<int>(kMaxPerDraw), indices_.data() + (remaining - kMaxPerDraw));
            remaining = indicesUsed_ - kMaxPerDraw;
            indicesUsed_ = remaining;
        }
        target.DrawPrimsIndexed(primType_, static_cast<int>(remaining), indices_.data());
        return;
    }
    std::uint32_t remaining = verticesUsed_;
    while (remaining > kMaxPerDraw) {
        target.DrawPrims(primType_, static_cast<int>(remaining - kMaxPerDraw), static_cast<int>(kMaxPerDraw));
        remaining = verticesUsed_ - kMaxPerDraw;
        verticesUsed_ = remaining;
    }
    target.DrawPrims(primType_, 0, static_cast<int>(remaining));
}

void cSC4PrimitivePool::SetFormat(std::uint32_t primType, std::uint32_t verticesPerPrim, std::uint32_t indicesPerPrim,
                                  std::uint32_t vertexFormat, VertexInit vertexInit, IndexInit indexInit) {
    primType_ = primType;
    verticesPerPrim_ = verticesPerPrim;
    indicesPerPrim_ = indicesPerPrim;
    vertexFormat_ = vertexFormat;
    stride_ = static_cast<std::uint32_t>(nRZSimGL::StandardVFStride(vertexFormat));
    vertexInit_ = vertexInit;
    indexInit_ = indexInit;
    verticesUsed_ = 0;
    indicesUsed_ = 0;
    indices_.clear();
    vertices_.clear();
}

void* cSC4PrimitivePool::GetPrimVertices(std::uint32_t count) {
    const std::uint32_t add = count * verticesPerPrim_;
    const std::uint64_t need = static_cast<std::uint64_t>(verticesUsed_ + add) * stride_;
    if (vertices_.size() < need) {
        vertices_.resize(static_cast<std::size_t>(need), 0);
        if (vertexInit_) vertexInit_(vertices_.data(), verticesUsed_, verticesUsed_ + add);
    }
    const std::uint32_t first = verticesUsed_;
    verticesUsed_ += add;
    return vertices_.data() + static_cast<std::size_t>(stride_) * first;
}

std::uint16_t* cSC4PrimitivePool::GetPrimIndices(std::uint32_t count) {
    const std::uint32_t add = count * indicesPerPrim_;
    const std::uint32_t need = indicesUsed_ + add;
    const std::size_t oldSize = indices_.size();
    if (oldSize < need) {
        indices_.resize(need);
        if (indexInit_) indexInit_(indices_.data(), static_cast<std::uint32_t>(oldSize), need);
    }
    const std::uint32_t first = indicesUsed_;
    indicesUsed_ += add;
    return indices_.data() + first;
}
