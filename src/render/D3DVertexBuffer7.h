// CDirect3DVertexBuffer7 (0x10063aebe..0x10063b418): a vertex buffer backed by a GL buffer object, optionally also
// registered as an APPLE vertex-array range. Only the position / diffuse / two texcoord sets of a flexible vertex
// format are understood. Layout: +8 refcount, +0xc size, +0x10 buffer object, +0x14 serialized-modify flag,
// +0x18 vertex-array object, +0x1c diffuse offset, +0x20 texcoord offsets, +0x28 texcoord sizes, +0x30 client-state
// flags, +0x38 cached pointers, +0x58 the creation description, +0x68 byte size, +0x70 page-aligned storage, +0x78 raw block.
#pragma once
#include <cstdint>
#include <vector>

#include "render/DX7Structs.h"
#include "render/DX7Types.h"

namespace sgl {

// The C globals the buffer code shares with the device (bound GL objects and the pending flush range).
extern std::int32_t g_CurrVBO, g_CurrVarId, g_MacFlushSizeOverride, g_MacVBSizeToFlush, g_MacVBOffsetToFlush;

class CDirect3DVertexBuffer7 final : public IDirect3DVertexBuffer7 {
public:
    static std::int32_t GetFVFSize(std::uint32_t fvf);                                        // @ 0x10063af00
    static void GetTexCoordSizes(std::vector<std::uint32_t>& out, std::uint32_t fvf);          // @ 0x10063aebe

    CDirect3DVertexBuffer7(_D3DVERTEXBUFFERDESC* desc, std::uint32_t flags);                  // @ 0x10063affa
    ~CDirect3DVertexBuffer7() override;                                                        // @ 0x10063b238
    void SetFVF(std::uint32_t fvf);                                                            // @ 0x10063b1c0

    std::uint32_t QueryInterface(const _GUID&, void** out) override;                           // @ 0x10063b30e
    void AddRef() override { ++refCount; }                                                     // @ 0x10063b320
    std::int32_t Release() override;                                                           // @ 0x10063b32e
    std::uint32_t Lock(std::uint32_t flags, void** data, std::uint32_t* size) override;        // @ 0x10063b36c
    std::uint32_t Unlock() override;                                                           // @ 0x10063b418
    std::uint32_t ProcessVertices(std::uint32_t, std::uint32_t, std::uint32_t, IDirect3DVertexBuffer7*, std::uint32_t, IDirect3DDevice7*, std::uint32_t) override { return 0; }   // @ 0x10063b408
    std::uint32_t GetVertexBufferDesc(_D3DVERTEXBUFFERDESC* out) override;                     // @ 0x10063b354
    std::uint32_t Optimize(IDirect3DDevice7*, std::uint32_t) override { return 0; }            // @ 0x10063b400
    std::uint32_t ProcessVerticesStrided(std::uint32_t, std::uint32_t, std::uint32_t, _D3DDRAWPRIMITIVESTRIDEDDATA*, std::uint32_t, IDirect3DDevice7*, std::uint32_t) override { return 0; }   // @ 0x10063b410

    std::int32_t refCount = 0;                       // +8
    std::uint32_t sizeBytes = 0;                     // +0xc
    std::uint32_t bufferObject = 0;                  // +0x10
    std::int32_t serializedModify = -1;              // +0x14
    std::uint32_t vertexArrayObject = 0;             // +0x18
    std::int32_t diffuseOffset = -1;                 // +0x1c
    std::int32_t texOffset[2] = {-1, -1};            // +0x20
    std::int32_t texSize[2] = {2, 2};                // +0x28
    bool vertexArrayOn = false, colorArrayOn = false, texArrayOn[2] = {false, false};   // +0x30..+0x33
    const void* vertexPointer = nullptr;             // +0x38
    const void* colorPointer = nullptr;              // +0x40
    const void* texPointer[2] = {nullptr, nullptr};  // +0x48
    _D3DVERTEXBUFFERDESC desc{};                     // +0x58 (FVF at +0x60)
    std::uint64_t byteSize = 0;                      // +0x68
    std::uint8_t* storage = nullptr;                 // +0x70 page-aligned
    std::uint8_t* rawBlock = nullptr;                // +0x78
};

}  // namespace sgl
