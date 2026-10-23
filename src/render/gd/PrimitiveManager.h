// nSGLDX7::cPrimitiveManager — the driver's geometry batcher (vtable group 0x100ad5b30, 30 hand-written functions).
//
// The manager receives draws from cGDriver as (vertex format, primitive type, stride, first vertex, vertex count,
// source vertices, index count, indices). It appends the vertices to one of its cVertexBuffer objects, queues a
// cPrimitive record for every non-merged batch, and draws the queue in FlushInternal. Consecutive strips / fans /
// lines that use the same primitive type and indexing kind are merged into one batch by patching the queued index
// buffer in place (FastAddOffset).
//
// Original layout (byte offsets in the shipped object):
//   +0x00  vtable (cIGZGDriverVertexBufferExtension: QueryInterface/AddRef/Release, GetVertexBufferName,
//          VertexBufferType, MaxVertices, GetVertices, ContinueVertices, ReleaseVertices, DrawPrims,
//          DrawPrimsIndexed, Reset)
//   +0x08  cRZRefCount (its own vtable; the count is at +0x10)
//   +0x18  cGDriver*                      (set by Restart)
//   +0x20  IDirect3D7*                    (set by Restart; non-null is the "started" flag IsStarted reads)
//   +0x28  IDirect3DDevice7*              (set by Restart)
//   +0x30  std::vector<cPrimitive>        (0x30-byte records)
//   +0x48  std::vector<unsigned short>    (the index pool handed out by GetIndices)
//   +0x60  unsigned index cursor into that vector
//   +0x68  std::vector<cVertexBuffer>     (0x48-byte records; normally exactly one, format 11 = V3F_C4UB_2T2F)
//
// The cVertexBuffer neighbour and the IDirect3D7 / IDirect3DDevice7 calls are unrecovered here, so they live behind
// IPrimitiveHost / IVertexBufferHost below (each method carries the original address). That keeps the batching logic —
// the interesting part — in this class and lets a Metal backend supply the buffers and draws.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "render/gd/SGLDX7Driver.h"
#include "rz/cRZRefCount.h"

namespace sgl {
class IDirectDraw7;
class IDirect3D7;
class IDirect3DDevice7;
}  // namespace sgl

namespace nSGLDX7 {

class cGDriver;
class cPrimitiveManager;

// One queued draw (0x30 bytes in the original vector):
//   +0x00 D3DPRIMITIVETYPE (already looked up through vLookupPrimitiveType)
//   +0x04 first vertex in the vertex buffer
//   +0x08 vertex count
//   +0x0c unused / padding
//   +0x10 index array (null for a non-indexed draw)
//   +0x18 index count
//   +0x1c unused / padding
//   +0x20 pointer to the locked vertex-buffer memory the batch was copied into
//   +0x28 byte count copied for the batch
//   +0x2c unused / padding
struct cPrimitive {
    std::uint32_t type = 0;                  // +0x00
    std::uint32_t startVertex = 0;           // +0x04
    std::uint32_t vertexCount = 0;           // +0x08
    std::uint32_t reserved0c = 0;            // +0x0c
    std::uint16_t* indices = nullptr;        // +0x10
    std::uint32_t indexCount = 0;            // +0x18
    std::uint32_t reserved1c = 0;            // +0x1c
    void* vertexData = nullptr;              // +0x20
    std::uint32_t dataBytes = 0;             // +0x28
    std::uint32_t reserved2c = 0;            // +0x2c
};
static_assert(sizeof(cPrimitive) == 0x30, "cPrimitive is 0x30 bytes in the original");

// One entry of the original D3DDRAWPRIMITIVESTRIDEDDATA: a pointer and a stride. The shipped x86_64 build packs
// the 12 entries 0x0c bytes apart (position +0x00, diffuse +0x18, textureCoords +0x30...); the seam passes a
// naturally laid out record instead, because the real D3D7 call happens behind IPrimitiveHost and is not ABI-visible.
struct sPrimitiveStridedEntry {
    const void* data = nullptr;   // pointer
    std::uint32_t stride = 0;     // stride in bytes
    std::uint32_t reserved = 0;
};
static_assert(sizeof(sPrimitiveStridedEntry) == 0x10, "natural C++ entry");

// The 12-entry descriptor: position, normal, diffuse, specular, textureCoords[8] (D3D7's six source elements plus
// the two the batcher never uses). DrawPrimsImmediate fills position, diffuse (when FVF has 0x40) and the first six
// texture-coordinate entries, exactly like the original; the remaining entries stay zero here (the original passed
// uninitialised stack bytes, which D3D7's shim ignores).
struct sPrimitiveStridedData {
    sPrimitiveStridedEntry position;
    sPrimitiveStridedEntry normal;
    sPrimitiveStridedEntry diffuse;
    sPrimitiveStridedEntry specular;
    sPrimitiveStridedEntry texCoord[8];
};

// One cVertexBuffer (0x48 bytes in the original; six functions, not recovered). The manager only uses the calls
// below. `GetVertices(count, false)` reports "no room" with null instead of growing; `GetVertices(count, true)`
// is the caller that is allowed to flush/reallocate.
class IVertexBufferHost {
public:
    virtual ~IVertexBufferHost() = default;
    virtual void* GetVertices(int count, bool retry) = 0;                    // cVertexBuffer::GetVertices @ 0x100609700
    virtual void* ContinueVertices(int count) = 0;                           // @ 0x100609958
    virtual void ReleaseVertices() = 0;                                      // @ 0x10060999a
    virtual int AllocateVertexBuffer(int maxVertices) = 0;                   // @ 0x10060980e
    virtual void ReleaseDeviceBuffer() = 0;                                  // cVertexBuffer +0x10 release (Stop @ 0x100609d88)
    virtual std::uint32_t MaxVertices() const = 0;                           // +0x18
    virtual std::uint32_t VertexFormat() const = 0;                          // +0x1c
    virtual std::uint32_t Position() const = 0;                              // +0x30
    virtual std::uint32_t DrawStart() const = 0;                             // +0x38
};

// The unrecovered cVertexBuffer constructor plus every IDirect3DDevice7 draw call and capability query the manager
// makes. A Metal implementation returns its own buffers and encodes the draws; the D3D7 vtable slots are in comments.
class IPrimitiveHost {
public:
    virtual ~IPrimitiveHost() = default;

    // cVertexBuffer::cVertexBuffer(IDirectDraw7*, IDirect3D7*, eGDVertexFormat, unsigned) @ 0x10060967e.
    virtual std::unique_ptr<IVertexBufferHost> CreateVertexBuffer(void* directDraw, void* direct3D,
                                                                  std::uint32_t vertexFormat,
                                                                  std::uint32_t flags) = 0;
    // IDirect3DDevice7::GetCaps (+0x18), 0xf4-byte description. False is the failed call (Init then uses 0x10800).
    virtual bool GetDeviceCaps(void* device, std::uint8_t* description) = 0;
    virtual bool IsFastATICard() = 0;                                        // MacOpenGL::IsFastATICard @ 0x10063cd8b
    virtual bool IsVertexArraySupported() = 0;                               // @ 0x10063cd9f (result unused by the original)
    virtual void ApplyTextureState() = 0;                                    // cGDriver::ApplyTextureState @ 0x100608914

    // Immediate (client-memory) draws. vtable slots: +0xc8, +0xd0, +0xe8, +0xf0.
    virtual long DrawPrimitive(void* device, std::uint32_t type, std::uint32_t fvf, const void* vertices,
                               std::uint32_t vertexCount) = 0;
    virtual long DrawIndexedPrimitive(void* device, std::uint32_t type, std::uint32_t fvf, const void* vertices,
                                      std::uint32_t vertexCount, const std::uint16_t* indices,
                                      std::uint32_t indexCount) = 0;
    virtual long DrawPrimitiveStrided(void* device, std::uint32_t type, std::uint32_t fvf,
                                      const sPrimitiveStridedData& data, std::uint32_t vertexCount) = 0;
    virtual long DrawIndexedPrimitiveStrided(void* device, std::uint32_t type, std::uint32_t fvf,
                                             const sPrimitiveStridedData& data, std::uint32_t vertexCount,
                                             const std::uint16_t* indices, std::uint32_t indexCount) = 0;

    // Vertex-buffer draws. vtable slots +0xf8 (non-indexed) and +0x100 (indexed).
    virtual long DrawPrimitiveVB(void* device, IVertexBufferHost& buffer, std::uint32_t type, std::uint32_t start,
                                 std::uint32_t vertexCount) = 0;
    virtual long DrawIndexedPrimitiveVB(void* device, IVertexBufferHost& buffer, std::uint32_t type,
                                        std::uint32_t start, std::uint32_t vertexCount,
                                        const std::uint16_t* indices, std::uint32_t indexCount) = 0;
};

// The class implements cIGZGDriverVertexBufferExtension (the vtable group) and the narrow IPrimitiveManagerHost
// interface cGDriver talks to. The dtor slots are compiler-emitted; the four non-virtual thunks in the binary
// (0x100609b82/bbe/e60/e78) adjust to the cRZRefCount sub-object and are compiler-generated too.
class cPrimitiveManager : public IPrimitiveManagerHost, public cRZRefCount {
public:
    explicit cPrimitiveManager(IPrimitiveHost& host) : host_(host) {}
    ~cPrimitiveManager() override;

    // ---- cIGZGDriverVertexBufferExtension (vtable 0x100ad5b30, slots 0..11) ----
    virtual bool QueryInterface(std::uint32_t iid, void** out);                          // slot 0 @ 0x100609e2c
    virtual void AddRef();                                                               // slot 1 @ 0x100609e52
    virtual std::int32_t Release();                                                      // slot 2 @ 0x100609e6a
    virtual const char* GetVertexBufferName(gd::eGDVertexFormat format);                 // slot 3 @ 0x100609e82
    virtual std::uint32_t VertexBufferType(std::uint32_t index);                         // slot 4 @ 0x100609e8a
    virtual std::uint32_t MaxVertices(std::uint32_t index);                              // slot 5 @ 0x100609e9e
    virtual void* GetVertices(std::uint32_t index, std::uint32_t count);                 // slot 6 @ 0x100609eb2
    virtual void* ContinueVertices(std::uint32_t index, std::uint32_t count);            // slot 7 @ 0x100609f30
    virtual void ReleaseVertices(std::uint32_t index);                                   // slot 8 @ 0x100609f58
    virtual void DrawPrims(std::uint32_t index, gd::eGDPrimType primType, void* vertices,
                           std::uint32_t count);                                         // slot 9 @ 0x100609f74
    virtual void DrawPrimsIndexed(std::uint32_t index, gd::eGDPrimType primType, std::uint32_t indexCount,
                                  const std::uint16_t* indices, void* vertices,
                                  std::uint32_t vertexCount);                            // slot 10 @ 0x100609fe4
    virtual void Reset();                                                                // slot 11 @ 0x10060a518

    // ---- cPrimitiveManager's own methods ----
    void Init(cGDriver* driver, sgl::IDirectDraw7* directDraw, sgl::IDirect3D7* direct3D,
              sgl::IDirect3DDevice7* device);                                            // @ 0x100609bcc (void)
    long FlushInternal();                                                                // @ 0x10060a4a2
    void Restart(cGDriver* driver, sgl::IDirect3D7* direct3D, sgl::IDirect3DDevice7* device);   // @ 0x10060a574
    long QueueVersionA(gd::eGDVertexFormat format, std::uint32_t primitiveType, std::uint32_t stride,
                       std::uint32_t baseVertex, std::uint32_t vertexCount, const void* vertices,
                       std::uint32_t indexCount, const std::uint16_t* indices);          // @ 0x10060a0f6
    long DrawPrimsImmediate(std::uint32_t fvf, int canonicalStride, std::uint32_t primitiveType, long stride,
                            int count, const void* vertices, int indexCount,
                            const std::uint16_t* indices);                               // @ 0x10060a58c
    bool CopyToVertexBuffer_V3F_C4F_2T2F(gd::eGDVertexFormat format, long srcStride, int vertexCount,
                                          const void* src, void* dst);                   // @ 0x10060a6c0
    bool CopyToVertexBuffer_V3F_C4UB_2T2F(gd::eGDVertexFormat format, long srcStride, int vertexCount,
                                           const void* src, void* dst);                  // @ 0x10060aa3e

    // ---- IPrimitiveManagerHost (what cGDriver calls; see SGLDX7Driver.h) ----
    bool IsStarted() override;                                                           // @ 0x100609670
    long Flush() override;                                                               // @ 0x100609f14 (void in the original)
    long FlushScene() override;                                                          // @ 0x10060a4fc
    long Queue(std::uint32_t vertexFormat, std::uint32_t primitiveType, std::uint32_t stride,
               std::uint32_t baseVertex, std::uint32_t vertexCount, const void* vertices,
               std::uint32_t indexCount, const std::uint16_t* indices) override;         // @ 0x10060a0ec
    std::uint16_t* GetIndices(int count) override;                                       // @ 0x10060a420
    long Restart(void* driver, ID3D7Device* device) override;                            // @ 0x10060a574 (seam overload)
    void Stop() override;                                                                // @ 0x100609d88
    bool Init(void* driver, ID3D7Device* device) override;                               // @ 0x100609bcc (seam overload)
    void Shutdown() override;                                                            // @ 0x100609d32

    // ---- observable state for tests/hook-up ----
    std::size_t PrimitiveCount() const { return primitives_.size(); }
    const cPrimitive& PrimitiveAt(std::size_t index) const { return primitives_[index]; }
    std::size_t VertexBufferCount() const { return vertexBuffers_.size(); }
    std::uint32_t IndexCursor() const { return indexCursor_; }
    const std::uint16_t* IndexPool() const { return indices_.data(); }
    std::size_t IndexPoolSize() const { return indices_.size(); }
    IPrimitiveHost& Host() const { return host_; }

private:
    bool InitInternal(void* driver, void* device, void* directDraw, void* direct3D);

    IPrimitiveHost& host_;
    void* driver_ = nullptr;                                   // +0x18 (cGDriver*)
    void* direct3D_ = nullptr;                                 // +0x20 (IDirect3D7*; also the started flag)
    void* device_ = nullptr;                                   // +0x28 (IDirect3DDevice7*)
    std::vector<cPrimitive> primitives_;                       // +0x30
    std::vector<std::uint16_t> indices_;                       // +0x48
    std::uint32_t indexCursor_ = 0;                            // +0x60
    std::vector<std::unique_ptr<IVertexBufferHost>> vertexBuffers_;   // +0x68
    bool started_ = false;                                     // mirrors "+0x20 != 0"
};

}  // namespace nSGLDX7
