#include "render/D3DVertexBuffer7.h"

#include <cstdint>

#include "render/GLApi.h"
#include "render/MacOpenGL.h"

namespace sgl {

std::int32_t g_CurrVBO = 0, g_CurrVarId = 0, g_MacFlushSizeOverride = 0, g_MacVBSizeToFlush = 0, g_MacVBOffsetToFlush = 0;

namespace {
// Floats per texcoord set indexed by (format bits ^ 2): bits 0 -> 2, 1 -> 3, 2 -> 4, 3 -> 1.
const std::uint32_t kTexCoordFloats[4] = {4, 1, 2, 3};
// Offset of whatever follows the position, by position type (only plain XYZ is understood).
const std::int32_t kAfterPosition[5] = {12, 0, 0, 0, 0};
}  // namespace

void CDirect3DVertexBuffer7::GetTexCoordSizes(std::vector<std::uint32_t>& out, std::uint32_t fvf) {
    const std::uint32_t count = fvf >> 8 & 0xf;
    for (std::uint32_t i = 0, shift = 0x10; i < count; ++i, shift += 2) out[i] = kTexCoordFloats[(fvf >> shift & 3) ^ 2];
}

std::int32_t CDirect3DVertexBuffer7::GetFVFSize(std::uint32_t fvf) {
    const std::uint8_t position = static_cast<std::uint8_t>(fvf) & 0xe;
    std::int32_t size = 0xc;
    if (position != 2) size = position == 4 ? 0x10 : 0;
    std::int32_t total = (fvf & 0x10) ? size + 0xc : size;
    if (fvf & 0x40) total += MacOpenGL::IsFastATICard() ? 0x14 : 4;
    if (static_cast<std::int8_t>(static_cast<std::uint8_t>(fvf)) < 0) total += MacOpenGL::IsFastATICard() ? 0x10 : 4;
    const std::uint32_t sets = fvf >> 8 & 0xf;
    std::vector<std::uint32_t> sizes;
    sizes.reserve(sets);
    sizes.resize(sets);
    GetTexCoordSizes(sizes, fvf);
    for (std::uint32_t i = 0; i < sets; ++i) total += static_cast<std::int32_t>(sizes[i] * 4);
    return total;
}

CDirect3DVertexBuffer7::CDirect3DVertexBuffer7(_D3DVERTEXBUFFERDESC* d, std::uint32_t) {
    desc = *d;
    const std::uint32_t vertexBytes = static_cast<std::uint32_t>(GetFVFSize(d->dwFVF)) * d->dwNumVertices;
    if (vertexBytes != 0) {
        byteSize = vertexBytes;
        rawBlock = new std::uint8_t[vertexBytes + 0x1000];
        storage = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(rawBlock) + 0xfff) & ~std::uintptr_t{0xfff});
    }
    const std::int32_t vertices = static_cast<std::int32_t>(d->dwNumVertices);
    const std::int32_t stride = GetFVFSize(d->dwFVF);
    IGLApi* g = GL();
    if (MacOpenGL::IsVertexArraySupported()) {
        std::int32_t previous = 0;
        g->GetIntegerv(0x85b5, &previous);
        g->GenVertexArraysAPPLE(1, &vertexArrayObject);
        g->BindVertexArrayAPPLE(vertexArrayObject);
        g->EnableClientState(0x851d);
        g->VertexArrayRangeAPPLE(stride * vertices, storage);
        if ((d->dwCaps & 0x800) == 0) g->VertexArrayParameteriAPPLE(0x851f, 0x85bf);
        g->BindVertexArrayAPPLE(0);
    }
    SetFVF(d->dwFVF);
    AddRef();
    sizeBytes = static_cast<std::uint32_t>(stride * vertices);
    serializedModify = -1;
    g->GenBuffers(1, &bufferObject);
    g->BindBuffer(gl::ARRAY_BUFFER, bufferObject);
    g_CurrVBO = static_cast<std::int32_t>(bufferObject);
    g->BufferData(gl::ARRAY_BUFFER, sizeBytes, nullptr, 0x88e0);
    g->BufferParameteriAPPLE(gl::ARRAY_BUFFER, 0x8a13, 0);
}

void CDirect3DVertexBuffer7::SetFVF(std::uint32_t fvf) {
    diffuseOffset = -1;
    const std::uint8_t kind = static_cast<std::uint8_t>((((static_cast<std::uint8_t>(fvf) & 0xe) >> 1) + 7) & 7);
    std::int32_t offset = kind < 5 ? kAfterPosition[kind] : 0;
    if (fvf & 0x40) {
        diffuseOffset = offset;
        offset += 4;
    }
    if (fvf & 0xf00) {
        // both sets are always filled in, whatever the format's coordinate-set count is
        for (int set = 0, shift = 0x10; set < 2; ++set, shift += 2) {
            const std::int32_t floats = static_cast<std::int32_t>(kTexCoordFloats[(static_cast<std::int32_t>(fvf) >> shift & 3) ^ 2]);
            texSize[set] = floats;
            texOffset[set] = offset;
            offset += floats * 4;
        }
    }
}

CDirect3DVertexBuffer7::~CDirect3DVertexBuffer7() {
    IGLApi* g = GL();
    if (bufferObject != 0) {
        g->BindBuffer(gl::ARRAY_BUFFER, 0);
        g->DeleteBuffers(1, &bufferObject);
        g_CurrVBO = 0;
    }
    if (MacOpenGL::IsVertexArraySupported()) {
        g->DisableClientState(0x851d);
        g->VertexArrayRangeAPPLE(0, nullptr);
    }
    delete[] rawBlock;
    rawBlock = nullptr;
    storage = nullptr;
}

std::uint32_t CDirect3DVertexBuffer7::QueryInterface(const _GUID&, void** out) {
    *out = this;
    AddRef();
    return 0;
}

std::int32_t CDirect3DVertexBuffer7::Release() {
    const std::int32_t count = --refCount;
    if (count == 0) delete this;
    return count;
}

std::uint32_t CDirect3DVertexBuffer7::GetVertexBufferDesc(_D3DVERTEXBUFFERDESC* out) {
    *out = desc;
    return 0;
}

std::uint32_t CDirect3DVertexBuffer7::Lock(std::uint32_t flags, void** data, std::uint32_t*) {
    if (bufferObject != 0) {
        IGLApi* g = GL();
        if (static_cast<std::int32_t>(bufferObject) != g_CurrVBO) {
            g->BindBuffer(gl::ARRAY_BUFFER, bufferObject);
            g_CurrVBO = static_cast<std::int32_t>(bufferObject);
        }
        if (flags & 0x2000) g->BufferData(gl::ARRAY_BUFFER, sizeBytes, nullptr, 0x88e0);
        const std::int32_t serialized = ~(flags >> 12 & 0xfffff) & 1;
        if (serialized != serializedModify) {
            serializedModify = serialized;
            g->BufferParameteriAPPLE(gl::ARRAY_BUFFER, 0x8a12, serialized);
        }
        *data = g->MapBuffer(gl::ARRAY_BUFFER, 0x88b9);
    }
    return 0;
}

std::uint32_t CDirect3DVertexBuffer7::Unlock() {
    IGLApi* g = GL();
    if (bufferObject != 0) {
        if (static_cast<std::int32_t>(bufferObject) != g_CurrVBO) {
            g->BindBuffer(gl::ARRAY_BUFFER, bufferObject);
            g_CurrVBO = static_cast<std::int32_t>(bufferObject);
        }
        const std::int32_t flushSize = g_MacFlushSizeOverride != 0 ? g_MacFlushSizeOverride : g_MacVBSizeToFlush;
        g->FlushMappedBufferRangeAPPLE(gl::ARRAY_BUFFER, g_MacVBOffsetToFlush, flushSize);
        g->UnmapBuffer(gl::ARRAY_BUFFER);
        g_MacVBOffsetToFlush = 0;
        g_MacVBSizeToFlush = 0;
    }
    if (MacOpenGL::IsVertexArraySupported()) {
        if (g_CurrVarId != static_cast<std::int32_t>(vertexArrayObject)) {
            g_CurrVarId = static_cast<std::int32_t>(vertexArrayObject);
            g->BindVertexArrayAPPLE(vertexArrayObject);
        }
        g->FlushVertexArrayRangeAPPLE(static_cast<GLsizei>(byteSize), storage);
    }
    return 0;
}

}  // namespace sgl
