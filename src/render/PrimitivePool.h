// cSC4PrimitivePool — a growable batch of vertices (and optionally indices) that effects fill with primitives and then
// draw in one go (14 functions at 0x1005fd0de..0x1005fd5f2). SetFormat picks the primitive type, how many vertices / indices
// one primitive takes, the vertex format (stride from nRZSimGL::StandardVFStride) and optional initialisers that run over
// freshly grown ranges (the quad initialisers below fill in texture coordinates, colour and the triangle list).
//
// Draw submits at most 0x600 vertices / indices per call, taking chunks from the END of the batch and shrinking the used
// counters as it goes, then the remainder from the start (so drawing consumes the pool).
// Layout: +8 primitive type (6), +0xc vertices per primitive (4), +0x10 indices per primitive (0), +0x14 vertex format (10),
// +0x18 vertex bytes, +0x30 stride, +0x34 vertices used, +0x38 vertex init, +0x40 indices, +0x58 indices used, +0x60 index init.
#pragma once
#include <cstdint>
#include <vector>

// What Draw needs from SC4DrawContext.
class IPrimitiveDrawTarget {
public:
    virtual ~IPrimitiveDrawTarget() = default;
    virtual void SetVertexBuffer(std::uint32_t format, std::uint32_t count, const void* vertices) = 0;                 // @ 0x100773440
    virtual void DrawPrims(std::uint32_t primType, int first, int count) = 0;                                          // @ 0x1007734a8
    virtual void DrawPrimsIndexed(std::uint32_t primType, int count, const std::uint16_t* indices) = 0;                // @ 0x1007734e8
};

class cSC4PrimitivePool {
public:
    using VertexInit = void (*)(void* vertices, std::uint32_t first, std::uint32_t end);
    using IndexInit = void (*)(std::uint16_t* indices, std::uint32_t first, std::uint32_t end);

    cSC4PrimitivePool() = default;                          // @ 0x1005fd262
    cSC4PrimitivePool(const cSC4PrimitivePool&) = default;  // @ 0x1005fd2aa
    virtual ~cSC4PrimitivePool() = default;                 // @ 0x1005fd354

    // Initialisers for 24-byte V3F_C4UB_T2F quads: white colour and the four corner texture coordinates, optionally
    // mirrored. The corner counter restarts at 1 (3 for the vertical flip) at `first`, whatever its value.
    static void QuadVertexInit_V3F_C4UB_T2F(void* vertices, std::uint32_t first, std::uint32_t end);        // @ 0x1005fd0de
    static void VFlip_QuadVertexInit_V3F_C4UB_T2F(void* vertices, std::uint32_t first, std::uint32_t end);  // @ 0x1005fd144
    static void HFlip_QuadVertexInit_V3F_C4UB_T2F(void* vertices, std::uint32_t first, std::uint32_t end);  // @ 0x1005fd1a8
    // Triangle-list indices (0,1,2, 0,2,3) for consecutive quads, numbered from index `first`.
    static void QuadTriListIndexInit(std::uint16_t* indices, std::uint32_t first, std::uint32_t end);        // @ 0x1005fd210

    void Draw(IPrimitiveDrawTarget& target);                // @ 0x1005fd3d0
    void SetFormat(std::uint32_t primType, std::uint32_t verticesPerPrim, std::uint32_t indicesPerPrim,
                   std::uint32_t vertexFormat, VertexInit vertexInit, IndexInit indexInit);   // @ 0x1005fd4a0
    std::uint32_t VerticesUsed() const { return verticesUsed_; }                 // @ 0x1005fd4f8
    std::uint16_t* Indices() { return indices_.data(); }                          // @ 0x1005fd502
    std::uint32_t IndicesUsed() const { return indicesUsed_; }                    // @ 0x1005fd50c
    // Reserves room for `count` more primitives and returns where their vertices / indices start.
    void* GetPrimVertices(std::uint32_t count);                                   // @ 0x1005fd516
    std::uint16_t* GetPrimIndices(std::uint32_t count);                           // @ 0x1005fd57e
    void RestartArrays() { verticesUsed_ = 0; indicesUsed_ = 0; }                 // @ 0x1005fd5f2

private:
    std::uint32_t primType_ = 6;          // +8
    std::uint32_t verticesPerPrim_ = 4;   // +0xc
    std::uint32_t indicesPerPrim_ = 0;    // +0x10
    std::uint32_t vertexFormat_ = 10;     // +0x14
    std::vector<std::uint8_t> vertices_;  // +0x18
    std::uint32_t stride_ = 0;            // +0x30
    std::uint32_t verticesUsed_ = 0;      // +0x34
    VertexInit vertexInit_ = nullptr;     // +0x38
    std::vector<std::uint16_t> indices_;  // +0x40
    std::uint32_t indicesUsed_ = 0;       // +0x58
    IndexInit indexInit_ = nullptr;       // +0x60
};
