#include "render/gd/PrimitiveManager.h"

#include <algorithm>
#include <cstring>

#include "gz/MiscFunctions.h"
#include "render/GDIndexOps.h"
#include "render/SimGL.h"

namespace nSGLDX7 {
namespace {

constexpr long kNotInitialized = 0x80000008L;   // the error Init/Flush return when the manager has no device (D3DERR_NOTINITIALIZED)
constexpr std::uint32_t kVertexFormatV3F_C4UB_2T2F = 0xb;

// CDirect3DVertexBuffer7::GetFVFSize(0x242) for the target layout the manager always uses:
// XYZ (12) + diffuse (4, or 0x14 on the "fast ATI" path) + two 2-float texture coordinates (16).
std::uint32_t TargetBytesPerVertex(bool fastATI) { return fastATI ? 0x30u : 0x20u; }

// D3DFVF_* bits QueueVersionA synthesises for the vertex buffer: XYZ always, DIFFUSE when the source has a diffuse
// element, then one or two texture-coordinate sets (2 sets are encoded as 0x200, not 0x100|0x200).
std::uint32_t FvfForVertexFormat(gd::eGDVertexFormat format) {
    const bool hasPosition = nRZSimGL::StandardVFNumElements(format, 0) != 0;
    std::uint32_t fvf = hasPosition ? 0x42u : 0x40u;   // bPosition + 0x40 + bPosition
    if (nRZSimGL::StandardVFNumElements(format, 5) == 0) fvf = hasPosition ? 2u : 0u;
    const std::uint32_t texCoords = nRZSimGL::StandardVFNumElements(format, 7);
    if (texCoords >= 2) {
        fvf |= 0x200u;
    } else if (texCoords == 1) {
        fvf |= 0x100u;
    }
    return fvf;
}

void PutWhite(float* out) {
    out[0] = 1.0f;
    out[1] = 1.0f;
    out[2] = 1.0f;
    out[3] = 1.0f;
}

// The source colour is a packed C4UB; the C4F target stores one float per channel in the order the original writes
// (bytes 3,2,1,0 of the packed word). 0.003921569f is the literal from 0x10060a74x.
void PutColorFloat(float* out, const std::uint8_t* packed) {
    out[0] = static_cast<float>(packed[3]) * 0.003921569f;
    out[1] = static_cast<float>(packed[2]) * 0.003921569f;
    out[2] = static_cast<float>(packed[1]) * 0.003921569f;
    out[3] = static_cast<float>(packed[0]) * 0.003921569f;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Lifetime and the two entry points cGDriver uses

cPrimitiveManager::~cPrimitiveManager() {
    // Original body (0x100609ac8): drop the cVertexBuffer vector, free the index pool, free the primitive vector,
    // then run ~cRZRefCount. It does not call Stop()/Shutdown().
    vertexBuffers_.clear();
    indices_.clear();
    primitives_.clear();
}

bool cPrimitiveManager::InitInternal(void* driver, void* device, void* directDraw, void* direct3D) {
    (void)driver;   // Init does not record the driver/device; Restart does (the original leaves +0x18/+0x20/+0x28 alone)
    std::uint8_t caps[0xf4] = {};
    std::uint32_t flags = 0x10800;
    if (host_.GetDeviceCaps(device, caps)) {
        std::uint32_t deviceCaps = 0;
        std::memcpy(&deviceCaps, caps, sizeof(deviceCaps));
        flags = ((deviceCaps >> 5) & 0x800u) ^ 0x10800u;
    }
    vertexBuffers_.push_back(host_.CreateVertexBuffer(directDraw, direct3D, kVertexFormatV3F_C4UB_2T2F, flags));
    return true;
}

void cPrimitiveManager::Init(cGDriver* driver, sgl::IDirectDraw7* directDraw, sgl::IDirect3D7* direct3D,
                             sgl::IDirect3DDevice7* device) {
    InitInternal(driver, device, directDraw, direct3D);
}

bool cPrimitiveManager::Init(void* driver, ID3D7Device* device) {
    // The seam has no IDirectDraw7 / IDirect3D7 to pass on; the host creates the buffer from its own environment.
    return InitInternal(driver, device, nullptr, nullptr);
}

void cPrimitiveManager::Shutdown() {
    Stop();                 // clears the primitive queue and releases the device buffers
    indices_.clear();       // +0x48 end = begin (capacity is kept, exactly like the original)
    vertexBuffers_.clear();
}

void cPrimitiveManager::Stop() {
    if (!started_) return;
    device_ = nullptr;          // +0x28
    direct3D_ = nullptr;        // +0x20
    driver_ = nullptr;          // +0x18
    started_ = false;
    primitives_.clear();        // +0x38 = +0x30: Stop drops the queued primitives, not the index pool
    for (auto& buffer : vertexBuffers_) buffer->ReleaseDeviceBuffer();
}

void cPrimitiveManager::Restart(cGDriver* driver, sgl::IDirect3D7* direct3D, sgl::IDirect3DDevice7* device) {
    driver_ = driver;           // +0x18
    direct3D_ = direct3D;       // +0x20
    device_ = device;           // +0x28
    started_ = direct3D != nullptr;
    Reset();                    // original calls vtable slot +0x58, i.e. Reset
}

long cPrimitiveManager::Restart(void* driver, ID3D7Device* device) {
    // Seam overload: no IDirect3D7 is available, so a Restart is the "started" transition (Init never starts the
    // original manager; cGDriver always calls Restart right after Init through Flush).
    driver_ = driver;
    direct3D_ = device;
    device_ = device;
    started_ = true;
    Reset();
    return 0;
}

bool cPrimitiveManager::IsStarted() { return started_; }   // original: *(long*)(this+0x20) != 0

// ---------------------------------------------------------------------------------------------------------------------
// COM-shaped surface (+0x00 vtable, cRZRefCount at +0x08)

bool cPrimitiveManager::QueryInterface(std::uint32_t iid, void** out) {
    // The original does not null-check `out`; IIDs 1 (cIGZGDriver) and 0x9cd86f9 (the vertex-buffer extension)
    // are the only ones it answers. Both hand back the same object and AddRef it through +0x08.
    if (iid == 1u || iid == 0x9cd86f9u) {
        *out = this;
        cRZRefCount::AddRef();
        return true;
    }
    return false;
}

void cPrimitiveManager::AddRef() { cRZRefCount::AddRef(); }

std::int32_t cPrimitiveManager::Release() { return cRZRefCount::Release(); }

const char* cPrimitiveManager::GetVertexBufferName(gd::eGDVertexFormat format) {
    (void)format;
    return nullptr;             // @ 0x100609e82: xor eax,eax
}

std::uint32_t cPrimitiveManager::VertexBufferType(std::uint32_t index) {
    return vertexBuffers_[index]->VertexFormat();   // cVertexBuffer +0x1c, no bounds check
}

std::uint32_t cPrimitiveManager::MaxVertices(std::uint32_t index) {
    return vertexBuffers_[index]->MaxVertices();    // cVertexBuffer +0x18, no bounds check
}

void* cPrimitiveManager::GetVertices(std::uint32_t index, std::uint32_t count) {
    if (!started_) return nullptr;
    if (index == 0) {            // the original only resets the index cursor for buffer 0
        FlushInternal();
        indexCursor_ = 0;
    }
    return vertexBuffers_[index]->GetVertices(static_cast<int>(count), true);
}

void* cPrimitiveManager::ContinueVertices(std::uint32_t index, std::uint32_t count) {
    if (!started_) return nullptr;
    return vertexBuffers_[index]->ContinueVertices(static_cast<int>(count));
}

void cPrimitiveManager::ReleaseVertices(std::uint32_t index) {
    vertexBuffers_[index]->ReleaseVertices();   // no started check in the original
}

void cPrimitiveManager::DrawPrims(std::uint32_t index, gd::eGDPrimType primType, void* vertices,
                                  std::uint32_t count) {
    // The last two parameters are accepted and ignored by the original (0x100609f74); kept in the signature only.
    (void)vertices;
    (void)count;
    if (!started_) return;
    host_.ApplyTextureState();
    IVertexBufferHost& buffer = *vertexBuffers_[index];
    const std::uint32_t start = buffer.DrawStart();          // cVertexBuffer +0x38
    const std::uint32_t verticesInBuffer = buffer.Position();// cVertexBuffer +0x30
    host_.DrawPrimitiveVB(device_, buffer, lookup::kPrimitiveType[primType], start, verticesInBuffer - start);
}

void cPrimitiveManager::DrawPrimsIndexed(std::uint32_t index, gd::eGDPrimType primType,
                                         std::uint32_t indexCount, const std::uint16_t* indices,
                                         void* vertices, std::uint32_t vertexCount) {
    (void)vertices;             // ignored by the original, like DrawPrims
    (void)vertexCount;
    if (!started_) return;
    host_.ApplyTextureState();
    (void)host_.IsVertexArraySupported();   // queried and unused at 0x100609fe4 (result drives nothing)
    IVertexBufferHost& buffer = *vertexBuffers_[index];
    const std::uint32_t start = buffer.DrawStart();
    const std::uint32_t verticesInBuffer = buffer.Position();
    host_.DrawIndexedPrimitiveVB(device_, buffer, lookup::kPrimitiveType[primType], start,
                                 verticesInBuffer - start, indices, indexCount);
}

void cPrimitiveManager::Reset() {
    for (auto& buffer : vertexBuffers_) buffer->AllocateVertexBuffer(0x8000);
}

// ---------------------------------------------------------------------------------------------------------------------
// Index pool (+0x48 vector, cursor +0x60)

std::uint16_t* cPrimitiveManager::GetIndices(int count) {
    const std::uint32_t cursor = indexCursor_;
    const std::uint32_t needed = cursor + static_cast<std::uint32_t>(count);   // 32-bit wrap, as in the original
    if (indices_.size() < needed) {
        if (indices_.capacity() < needed) {
            FlushInternal();
            indexCursor_ = 0;
        }
        indices_.resize(needed);
    }
    const std::uint32_t base = indexCursor_;
    indexCursor_ = base + static_cast<std::uint32_t>(count);
    if (indices_.empty()) return nullptr;
    return indices_.data() + base;
}

// ---------------------------------------------------------------------------------------------------------------------
// Queueing

long cPrimitiveManager::Queue(std::uint32_t vertexFormat, std::uint32_t primitiveType, std::uint32_t stride,
                              std::uint32_t baseVertex, std::uint32_t vertexCount, const void* vertices,
                              std::uint32_t indexCount, const std::uint16_t* indices) {
    return QueueVersionA(vertexFormat, primitiveType, stride, baseVertex, vertexCount, vertices, indexCount, indices);
}

long cPrimitiveManager::QueueVersionA(gd::eGDVertexFormat format, std::uint32_t primitiveType,
                                      std::uint32_t stride, std::uint32_t baseVertex,
                                      std::uint32_t vertexCount, const void* vertices,
                                      std::uint32_t indexCount, const std::uint16_t* indices) {
    if (!started_) return kNotInitialized;

    const std::uint32_t fvf = FvfForVertexFormat(format);
    const int canonicalStride = nRZSimGL::StandardVFStride(format);
    if (canonicalStride > 0x20) {
        // Vertex layouts wider than 32 bytes are copied by the driver straight from client memory.
        return DrawPrimsImmediate(fvf, canonicalStride, primitiveType, static_cast<long>(stride),
                                  static_cast<int>(vertexCount + baseVertex), vertices,
                                  static_cast<int>(indexCount), indices);
    }

    const bool fastATI = host_.IsFastATICard();
    const std::uint32_t bytesPerVertex = TargetBytesPerVertex(fastATI);

    cPrimitive primitive{};
    primitive.type = primitiveType;
    primitive.vertexCount = vertexCount;
    primitive.indices = const_cast<std::uint16_t*>(indices);
    primitive.indexCount = indexCount;

    IVertexBufferHost& buffer = *vertexBuffers_.front();   // FlushInternal also always draws buffer 0
    void* locked = nullptr;
    if (!primitives_.empty()) locked = buffer.GetVertices(static_cast<int>(vertexCount), false);

    if (locked != nullptr) {
        // Fast path: the current buffer has room.
        if (fastATI) {
            CopyToVertexBuffer_V3F_C4F_2T2F(format, static_cast<long>(stride), static_cast<int>(vertexCount),
                                            vertices, locked);
        } else {
            CopyToVertexBuffer_V3F_C4UB_2T2F(format, static_cast<long>(stride), static_cast<int>(vertexCount),
                                             vertices, locked);
        }
        primitive.startVertex = buffer.DrawStart();          // +0x34 captured by GetVertices
        primitive.vertexData = locked;
        primitive.dataBytes = bytesPerVertex * vertexCount;
        buffer.ReleaseVertices();

        // Append to the previous batch when it is a strip/fan/line batch of the same kind.
        if (!primitives_.empty() && primitives_.back().type == primitiveType && primitiveType <= 4 &&
            ((0x16u >> primitiveType) & 1u) != 0) {
            cPrimitive& previous = primitives_.back();
            if (previous.indices != nullptr && indices != nullptr) {
                const int offset = static_cast<int>(primitive.startVertex - baseVertex - previous.startVertex);
                FastAddOffset(primitive.indices, indexCount, offset);
                previous.indexCount += indexCount;
                previous.vertexCount += vertexCount;
                previous.dataBytes += bytesPerVertex * vertexCount;
                return 0;
            }
            if (previous.indices == nullptr && indices == nullptr) {
                previous.vertexCount += vertexCount;
                previous.dataBytes += bytesPerVertex * vertexCount;
                return 0;
            }
        }
        if (baseVertex != 0) {
            FastAddOffset(primitive.indices, indexCount, -static_cast<int>(baseVertex));
        }
        primitives_.push_back(primitive);
        return 0;
    }

    // Slow path: flush the queue, then ask the buffer for a (possibly larger) region.
    FlushInternal();
    locked = buffer.GetVertices(static_cast<int>(vertexCount), true);
    if (locked == nullptr) return 0;

    if (fastATI) {
        CopyToVertexBuffer_V3F_C4F_2T2F(format, static_cast<long>(stride), static_cast<int>(vertexCount), vertices,
                                        locked);
    } else {
        CopyToVertexBuffer_V3F_C4UB_2T2F(format, static_cast<long>(stride), static_cast<int>(vertexCount), vertices,
                                         locked);
    }
    primitive.startVertex = buffer.DrawStart();
    primitive.vertexData = locked;
    primitive.dataBytes = bytesPerVertex * vertexCount;
    buffer.ReleaseVertices();
    if (baseVertex != 0) FastAddOffset(primitive.indices, indexCount, -static_cast<int>(baseVertex));
    primitives_.push_back(primitive);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Flushing

long cPrimitiveManager::Flush() {
    const long result = FlushInternal();
    indexCursor_ = 0;
    return result;
}

long cPrimitiveManager::FlushScene() {
    const long result = FlushInternal();
    indexCursor_ = 0;
    return result;
}

long cPrimitiveManager::FlushInternal() {
    if (primitives_.empty()) return 0;
    if (!started_) return kNotInitialized;
    IVertexBufferHost& buffer = *vertexBuffers_.front();   // the original draws every batch from buffer 0
    long result = 0;
    for (const cPrimitive& primitive : primitives_) {
        // cVertexBuffer::DrawPrim @ 0x1006099e0: indexed -> IDirect3DDevice7 +0x100, else +0xf8.
        if (primitive.indices != nullptr) {
            result = host_.DrawIndexedPrimitiveVB(device_, buffer, primitive.type, primitive.startVertex,
                                                  primitive.vertexCount, primitive.indices, primitive.indexCount);
        } else {
            result = host_.DrawPrimitiveVB(device_, buffer, primitive.type, primitive.startVertex,
                                           primitive.vertexCount);
        }
    }
    primitives_.clear();
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Immediate draws (the source stride does not match the canonical format width)

long cPrimitiveManager::DrawPrimsImmediate(std::uint32_t fvf, int canonicalStride, std::uint32_t primitiveType,
                                           long stride, int count, const void* vertices, int indexCount,
                                           const std::uint16_t* indices) {
    if (stride == canonicalStride) {
        if (indices == nullptr) {
            return host_.DrawPrimitive(device_, primitiveType, fvf, vertices, static_cast<std::uint32_t>(count));
        }
        return host_.DrawIndexedPrimitive(device_, primitiveType, fvf, vertices, static_cast<std::uint32_t>(count),
                                          indices, static_cast<std::uint32_t>(indexCount));
    }

    // Strided path: D3D7 wants per-element pointers/strides. The original points the texture entries at consecutive
    // 2-float pairs starting after position (and after diffuse when FVF has 0x40), six entries in all.
    sPrimitiveStridedData strided{};
    const auto* base = static_cast<const std::uint8_t*>(vertices);
    strided.position.data = vertices;
    strided.position.stride = static_cast<std::uint32_t>(stride);
    const std::uint8_t* textureBase;
    if ((fvf & 0x40u) == 0) {
        textureBase = base + 0xc;
    } else {
        strided.diffuse.data = base + 0xc;
        strided.diffuse.stride = static_cast<std::uint32_t>(stride);
        textureBase = base + 0x10;
    }
    for (int i = 0; i < 6; ++i) {
        strided.texCoord[i].data = textureBase + i * 8;
        strided.texCoord[i].stride = static_cast<std::uint32_t>(stride);
    }
    if (indices == nullptr) {
        return host_.DrawPrimitiveStrided(device_, primitiveType, fvf, strided, static_cast<std::uint32_t>(count));
    }
    return host_.DrawIndexedPrimitiveStrided(device_, primitiveType, fvf, strided,
                                             static_cast<std::uint32_t>(count), indices,
                                             static_cast<std::uint32_t>(indexCount));
}

// ---------------------------------------------------------------------------------------------------------------------
// Source -> target vertex layout conversion (the two copies QueueVersionA calls)

bool cPrimitiveManager::CopyToVertexBuffer_V3F_C4F_2T2F(gd::eGDVertexFormat format, long srcStride,
                                                         int vertexCount, const void* src, void* dst) {
    const std::int32_t packed = nRZSimGL::PackStandardVertexFormat(static_cast<int>(format));
    const long strideFloats = srcStride / 4;          // the original rounds the signed stride towards zero
    const std::int32_t effective = (srcStride & 3) == 0 ? packed : 0;   // unaligned strides copy nothing

    const auto* s = static_cast<const std::uint8_t*>(src);
    auto* d = static_cast<std::uint32_t*>(dst);
    for (int i = 0; i < vertexCount; ++i, s += strideFloats * 4, d += 12) {
        switch (effective) {
        case -0x7fffffff:   // 0x20 V3F
            std::memcpy(d, s, 12);
            PutWhite(reinterpret_cast<float*>(d + 4));
            break;
        case -0x7ffffeff:   // 1 V3F_C4UB
            std::memcpy(d, s, 12);
            PutColorFloat(reinterpret_cast<float*>(d + 4), s + 0xc);
            break;
        case -0x7fffbfff:   // 2 V3F_T2F
            std::memcpy(d, s, 12);
            PutWhite(reinterpret_cast<float*>(d + 4));
            std::memcpy(d + 8, s + 0xc, 8);
            break;
        case -0x7fffbeff:   // 10 V3F_C4UB_T2F
            std::memcpy(d, s, 12);
            PutColorFloat(reinterpret_cast<float*>(d + 4), s + 0xc);
            std::memcpy(d + 8, s + 0x10, 8);
            break;
        case -0x7fff7fff:   // 3 V3F_2T2F
            std::memcpy(d, s, 12);
            PutWhite(reinterpret_cast<float*>(d + 4));
            std::memcpy(d + 8, s + 0xc, 16);
            break;
        case -0x7fff7eff:   // 11 V3F_C4UB_2T2F: only with the canonical 32-byte stride
            if (srcStride != 0x20) break;
            std::memcpy(d, s, 12);
            PutColorFloat(reinterpret_cast<float*>(d + 4), s + 0xc);
            std::memcpy(d + 8, s + 0x10, 16);
            break;
        default:
            break;
        }
    }
    return true;   // the original always reports success, whatever the format
}

bool cPrimitiveManager::CopyToVertexBuffer_V3F_C4UB_2T2F(gd::eGDVertexFormat format, long srcStride,
                                                          int vertexCount, const void* src, void* dst) {
    const std::int32_t packed = nRZSimGL::PackStandardVertexFormat(static_cast<int>(format));
    const long strideFloats = srcStride >> 2;   // logical shift in the original (0x10060aa66)
    const std::int32_t effective = (srcStride & 3) == 0 ? packed : 0;

    auto* out = static_cast<std::uint8_t*>(dst);
    const auto* in = static_cast<const std::uint8_t*>(src);
    switch (effective) {
    case -0x7fffffff:
        ConvertVerticesFromV3F(out, in, strideFloats, vertexCount);
        return true;
    case -0x7ffffeff:
        ConvertVerticesFromV3F_C4UB(out, in, strideFloats, vertexCount);
        return true;
    case -0x7fffbfff:
        ConvertVerticesFromV3F_T2F(out, in, strideFloats, vertexCount);
        return true;
    case -0x7fffbeff:
        ConvertVerticesFromV3F_C4UB_T2F(out, in, strideFloats, vertexCount);
        return true;
    case -0x7fff7fff:
        ConvertVerticesFromV3F_2T2F(out, in, strideFloats, vertexCount);
        return true;
    case -0x7fff7eff:
        // Already in the target 32-byte layout: a straight memmove, but only at the canonical stride.
        (void)host_.IsVertexArraySupported();
        if (srcStride == 0x20 && vertexCount > 0) {
            std::memmove(out, in, static_cast<std::size_t>(vertexCount) << 5);
        }
        return true;
    default:
        return true;   // no converter for this source format / unaligned stride: nothing is copied
    }
}

}  // namespace nSGLDX7
