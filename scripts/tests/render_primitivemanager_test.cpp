// Tests for nSGLDX7::cPrimitiveManager (src/render/gd/PrimitiveManager.{h,cpp}): the batching logic, the index pool,
// the strip/fan merge (including the in-place index offset), both vertex-layout converters and the COM/refcount
// surface. cVertexBuffer and every IDirect3DDevice7 draw are fakes behind IPrimitiveHost, so the test exercises the
// recovered manager itself, not a renderer.
#include "render/gd/PrimitiveManager.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using namespace nSGLDX7;

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Fakes

struct FakePrimitiveHost;

struct FakeVertexBuffer final : IVertexBufferHost {
    explicit FakeVertexBuffer(FakePrimitiveHost& h);
    ~FakeVertexBuffer() override;

    void* GetVertices(int count, bool retry) override;
    void* ContinueVertices(int count) override;
    void ReleaseVertices() override;
    int AllocateVertexBuffer(int maxVertices) override;
    void ReleaseDeviceBuffer() override;
    std::uint32_t MaxVertices() const override { return capacity; }
    std::uint32_t VertexFormat() const override { return 0xb; }
    std::uint32_t Position() const override { return position; }
    std::uint32_t DrawStart() const override { return drawStart; }

    std::uint8_t* Bytes() { return memory.data(); }
    std::vector<std::uint8_t> memory;
    std::uint32_t capacity = 0, position = 0, drawStart = 0, stride = 0x20;
    bool locked = false;
    int allocateCalls = 0;
    int stopCalls = 0;
    FakePrimitiveHost& host;
};

struct FakePrimitiveHost final : IPrimitiveHost {
    struct Created {
        void* directDraw = nullptr;
        void* direct3D = nullptr;
        std::uint32_t format = 0, flags = 0;
        FakeVertexBuffer* buffer = nullptr;
    };
    struct DrawCall {
        enum Kind { Primitive, IndexedPrimitive, PrimitiveStrided, IndexedPrimitiveStrided, PrimitiveVB, IndexedPrimitiveVB } kind{};
        std::uint32_t type = 0, fvf = 0, start = 0, count = 0;
        const void* vertices = nullptr;
        std::vector<std::uint16_t> indices;
        sPrimitiveStridedData strided;
        int buffer = -1;
    };

    bool capsOk = true;
    std::uint32_t capsDword = 0;
    bool fastATI = false;
    bool vertexArraySupported = false;
    int applyTextureStateCalls = 0;
    int isVertexArraySupportedCalls = 0;
    int destroyedBuffers = 0;
    std::vector<Created> created;
    std::vector<DrawCall> draws;

    std::unique_ptr<IVertexBufferHost> CreateVertexBuffer(void* directDraw, void* direct3D, std::uint32_t format,
                                                          std::uint32_t flags) override {
        auto buffer = std::make_unique<FakeVertexBuffer>(*this);
        created.push_back({directDraw, direct3D, format, flags, buffer.get()});
        return buffer;
    }
    bool GetDeviceCaps(void*, std::uint8_t* description) override {
        if (!capsOk) return false;
        std::memset(description, 0, 0xf4);
        std::memcpy(description, &capsDword, sizeof(capsDword));
        return true;
    }
    bool IsFastATICard() override { return fastATI; }
    bool IsVertexArraySupported() override {
        ++isVertexArraySupportedCalls;
        return vertexArraySupported;
    }
    void ApplyTextureState() override { ++applyTextureStateCalls; }

    int BufferIndex(const IVertexBufferHost& buffer) const {
        for (std::size_t i = 0; i < created.size(); ++i)
            if (created[i].buffer == &buffer) return static_cast<int>(i);
        return -1;
    }

    long DrawPrimitive(void*, std::uint32_t type, std::uint32_t fvf, const void* vertices,
                       std::uint32_t count) override {
        DrawCall call;
        call.kind = DrawCall::Primitive;
        call.type = type;
        call.fvf = fvf;
        call.vertices = vertices;
        call.count = count;
        draws.push_back(call);
        return 0;
    }
    long DrawIndexedPrimitive(void*, std::uint32_t type, std::uint32_t fvf, const void* vertices,
                              std::uint32_t count, const std::uint16_t* indices,
                              std::uint32_t indexCount) override {
        DrawCall call;
        call.kind = DrawCall::IndexedPrimitive;
        call.type = type;
        call.fvf = fvf;
        call.vertices = vertices;
        call.count = count;
        call.indices.assign(indices, indices + indexCount);
        draws.push_back(call);
        return 0;
    }
    long DrawPrimitiveStrided(void*, std::uint32_t type, std::uint32_t fvf, const sPrimitiveStridedData& data,
                              std::uint32_t count) override {
        DrawCall call;
        call.kind = DrawCall::PrimitiveStrided;
        call.type = type;
        call.fvf = fvf;
        call.count = count;
        call.strided = data;
        draws.push_back(call);
        return 0;
    }
    long DrawIndexedPrimitiveStrided(void*, std::uint32_t type, std::uint32_t fvf,
                                     const sPrimitiveStridedData& data, std::uint32_t count,
                                     const std::uint16_t* indices, std::uint32_t indexCount) override {
        DrawCall call;
        call.kind = DrawCall::IndexedPrimitiveStrided;
        call.type = type;
        call.fvf = fvf;
        call.count = count;
        call.strided = data;
        call.indices.assign(indices, indices + indexCount);
        draws.push_back(call);
        return 0;
    }
    long DrawPrimitiveVB(void*, IVertexBufferHost& buffer, std::uint32_t type, std::uint32_t start,
                         std::uint32_t vertexCount) override {
        DrawCall call;
        call.kind = DrawCall::PrimitiveVB;
        call.type = type;
        call.start = start;
        call.count = vertexCount;
        call.buffer = BufferIndex(buffer);
        draws.push_back(call);
        return 0;
    }
    long DrawIndexedPrimitiveVB(void*, IVertexBufferHost& buffer, std::uint32_t type, std::uint32_t start,
                                std::uint32_t vertexCount, const std::uint16_t* indices,
                                std::uint32_t indexCount) override {
        DrawCall call;
        call.kind = DrawCall::IndexedPrimitiveVB;
        call.type = type;
        call.start = start;
        call.count = vertexCount;
        call.indices.assign(indices, indices + indexCount);
        call.buffer = BufferIndex(buffer);
        draws.push_back(call);
        return 0;
    }
};

FakeVertexBuffer::FakeVertexBuffer(FakePrimitiveHost& h) : stride(h.fastATI ? 0x30u : 0x20u), host(h) {}
FakeVertexBuffer::~FakeVertexBuffer() { ++host.destroyedBuffers; }

void* FakeVertexBuffer::GetVertices(int count, bool retry) {
    if (count < 0 || locked) return nullptr;
    const std::uint32_t wanted = static_cast<std::uint32_t>(count);
    if (!retry) {
        if (capacity == 0 || position + wanted > capacity) return nullptr;
    } else if (capacity == 0 || position + wanted > capacity) {
        capacity = position + wanted > 0x8000 ? position + wanted : 0x8000;
        memory.assign(static_cast<std::size_t>(capacity) * stride, 0);
        position = 0;
    }
    drawStart = position;
    position += wanted;
    locked = true;
    return memory.data() + static_cast<std::size_t>(drawStart) * stride;
}

void* FakeVertexBuffer::ContinueVertices(int count) {
    if (!locked || count < 0) return nullptr;
    const std::uint32_t wanted = static_cast<std::uint32_t>(count);
    if (position + wanted > capacity) return nullptr;
    drawStart = position;
    position += wanted;
    return memory.data() + static_cast<std::size_t>(drawStart) * stride;
}

void FakeVertexBuffer::ReleaseVertices() { locked = false; }

int FakeVertexBuffer::AllocateVertexBuffer(int maxVertices) {
    ++allocateCalls;
    capacity = maxVertices > 0 ? static_cast<std::uint32_t>(maxVertices) : 0;
    position = 0;
    drawStart = 0;
    locked = false;
    memory.assign(static_cast<std::size_t>(capacity) * stride, 0);
    return 0;
}

void FakeVertexBuffer::ReleaseDeviceBuffer() {
    ++stopCalls;
    capacity = 0;
    position = 0;
    drawStart = 0;
    locked = false;
    memory.clear();
}

// A manager that has been Init'ed and Restart'ed, like cGDriver::Flush does after SetupViewport.
struct Fixture {
    FakePrimitiveHost host;
    cPrimitiveManager manager{host};
    Fixture() {
        manager.Init(nullptr, nullptr);
        manager.Restart(nullptr, nullptr);
    }
    FakeVertexBuffer* buffer() { return host.created.empty() ? nullptr : host.created.back().buffer; }
};

std::vector<std::uint8_t> MakeVertices(int count, std::uint32_t stride, std::uint8_t seed = 0) {
    std::vector<std::uint8_t> v(static_cast<std::size_t>(count) * stride);
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<std::uint8_t>(seed + i);
    return v;
}

bool NearlyEqual(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// ---------------------------------------------------------------------------------------------------------------------
// Tests

void TestInitCapsAndStart() {
    FakePrimitiveHost host;
    cPrimitiveManager manager{host};
    CHECK(!manager.IsStarted());
    CHECK(manager.VertexBufferCount() == 0);

    host.capsOk = false;
    manager.Init(nullptr, nullptr);
    CHECK(host.created.size() == 1);
    CHECK(host.created[0].format == 0xb);
    CHECK(host.created[0].flags == 0x10800u);   // GetCaps failed -> WRITEONLY | SYSTEMMEMORY

    host.capsOk = true;
    host.capsDword = 0;                          // bit 16 clear -> SYSTEMMEMORY stays set
    manager.Init(nullptr, nullptr);
    CHECK(host.created.size() == 2);
    CHECK(host.created[1].flags == 0x10800u);

    host.capsDword = 1u << 16;                   // bit 16 set -> the XOR clears SYSTEMMEMORY
    manager.Init(nullptr, nullptr);
    CHECK(host.created.size() == 3);
    CHECK(host.created[2].flags == 0x10000u);

    CHECK(!manager.IsStarted());                 // Init never starts the original manager
    manager.Restart(nullptr, nullptr);
    CHECK(manager.IsStarted());
    CHECK(manager.VertexBufferCount() == 3);
}

void TestVertexBufferAccessors() {
    Fixture f;
    CHECK(f.manager.VertexBufferType(0) == 0xb);
    CHECK(f.manager.GetVertexBufferName(0xb) == nullptr);
    CHECK(f.manager.MaxVertices(0) == 0x8000);   // Fixture::Restart already ran Reset()

    f.manager.Reset();
    CHECK(f.buffer()->allocateCalls == 2);
    CHECK(f.buffer()->capacity == 0x8000);
}

void TestIndexPool() {
    Fixture f;
    std::uint16_t* first = f.manager.GetIndices(64);
    CHECK(first != nullptr);
    CHECK(f.manager.IndexCursor() == 64);
    f.manager.Flush();
    CHECK(f.manager.IndexCursor() == 0);

    std::uint16_t* a = f.manager.GetIndices(10);
    CHECK(a == first);                             // vector did not have to grow
    CHECK(f.manager.IndexCursor() == 10);
    std::uint16_t* b = f.manager.GetIndices(10);
    CHECK(b == first + 10);
    CHECK(f.manager.IndexCursor() == 20);

    // Needs more than the current capacity: FlushInternal (no queued draws) and the cursor starts over.
    std::uint16_t* c = f.manager.GetIndices(100);
    CHECK(c == f.manager.IndexPool());             // cursor reset to 0 before the resize
    CHECK(f.manager.IndexCursor() == 100);
}

void TestQueueCopyAndFlush() {
    Fixture f;
    const auto vertices = MakeVertices(3, 0x20, 0x10);
    CHECK(f.manager.Queue(0xb, 4, 0x20, 0, 3, vertices.data(), 0, nullptr) == 0);
    CHECK(f.manager.PrimitiveCount() == 1);
    CHECK(f.manager.PrimitiveAt(0).type == 4);
    CHECK(f.manager.PrimitiveAt(0).startVertex == 0);
    CHECK(f.manager.PrimitiveAt(0).vertexCount == 3);
    CHECK(f.manager.PrimitiveAt(0).dataBytes == 3 * 0x20);
    CHECK(std::memcmp(f.buffer()->Bytes(), vertices.data(), vertices.size()) == 0);   // 0xb at stride 0x20 = memmove

    CHECK(f.manager.Flush() == 0);
    CHECK(f.manager.PrimitiveCount() == 0);
    CHECK(f.host.draws.size() == 1);
    CHECK(f.host.draws[0].kind == FakePrimitiveHost::DrawCall::PrimitiveVB);
    CHECK(f.host.draws[0].type == 4);              // vLookupPrimitiveType[Triangles]
    CHECK(f.host.draws[0].start == 0);
    CHECK(f.host.draws[0].count == 3);
    CHECK(f.host.draws[0].buffer == 0);

    // FlushScene has exactly Flush's body (FlushInternal + index cursor = 0).
    CHECK(f.manager.GetIndices(4) != nullptr);
    CHECK(f.manager.Queue(0xb, 4, 0x20, 0, 3, vertices.data(), 0, nullptr) == 0);
    CHECK(f.manager.FlushScene() == 0);
    CHECK(f.manager.PrimitiveCount() == 0);
    CHECK(f.manager.IndexCursor() == 0);
    CHECK(f.host.draws.size() == 2);
    CHECK(f.host.draws[1].start == 3 && f.host.draws[1].count == 3);   // second batch in the same buffer
}

void TestMergeIndexedStrips() {
    Fixture f;
    const auto vertices = MakeVertices(6, 0x20, 0x20);
    std::uint16_t* pool = f.manager.GetIndices(64);
    std::memset(pool, 0, 64 * sizeof(std::uint16_t));
    f.manager.Flush();

    std::uint16_t* first = f.manager.GetIndices(3);
    first[0] = 0; first[1] = 1; first[2] = 2;
    CHECK(f.manager.Queue(0xb, 4, 0x20, 0, 3, vertices.data(), 3, first) == 0);

    std::uint16_t* second = f.manager.GetIndices(3);
    second[0] = 0; second[1] = 1; second[2] = 2;
    CHECK(f.manager.Queue(0xb, 4, 0x20, 0, 3, vertices.data() + 3 * 0x20, 3, second) == 0);

    CHECK(f.manager.PrimitiveCount() == 1);        // merged into the first batch
    CHECK(f.manager.PrimitiveAt(0).indexCount == 6);
    CHECK(f.manager.PrimitiveAt(0).vertexCount == 6);
    CHECK(f.manager.PrimitiveAt(0).dataBytes == 6 * 0x20);
    CHECK(second[0] == 3 && second[1] == 4 && second[2] == 5);   // offset = start(3) - base(0) - previous.start(0)

    CHECK(f.manager.Flush() == 0);
    CHECK(f.host.draws.size() == 1);
    CHECK(f.host.draws[0].kind == FakePrimitiveHost::DrawCall::IndexedPrimitiveVB);
    CHECK(f.host.draws[0].count == 6);
    CHECK(f.host.draws[0].indices == std::vector<std::uint16_t>({0, 1, 2, 3, 4, 5}));
}

void TestMergeNonIndexedStrips() {
    Fixture f;
    const auto vertices = MakeVertices(6, 0x20, 0x30);
    CHECK(f.manager.Queue(0xb, 4, 0x20, 0, 3, vertices.data(), 0, nullptr) == 0);
    CHECK(f.manager.Queue(0xb, 4, 0x20, 0, 3, vertices.data() + 3 * 0x20, 0, nullptr) == 0);
    CHECK(f.manager.PrimitiveCount() == 1);
    CHECK(f.manager.PrimitiveAt(0).vertexCount == 6);
    CHECK(f.manager.PrimitiveAt(0).indexCount == 0);
    f.manager.Flush();
    CHECK(f.host.draws.size() == 1);
    CHECK(f.host.draws[0].kind == FakePrimitiveHost::DrawCall::PrimitiveVB);
    CHECK(f.host.draws[0].count == 6);
}

void TestNoMergeOnDifferentPrimitiveType() {
    Fixture f;
    const auto vertices = MakeVertices(6, 0x20, 0x40);
    CHECK(f.manager.Queue(0xb, 4, 0x20, 0, 3, vertices.data(), 0, nullptr) == 0);
    CHECK(f.manager.Queue(0xb, 0, 0x20, 0, 3, vertices.data() + 3 * 0x20, 0, nullptr) == 0);
    CHECK(f.manager.PrimitiveCount() == 2);
    f.manager.Flush();
    CHECK(f.host.draws.size() == 2);
    CHECK(f.host.draws[0].start == 0 && f.host.draws[0].count == 3);
    CHECK(f.host.draws[1].start == 3 && f.host.draws[1].count == 3);
}

void TestBaseVertexOffsetsIndices() {
    Fixture f;
    const auto vertices = MakeVertices(3, 0x20, 0x50);
    std::uint16_t* pool = f.manager.GetIndices(8);
    std::memset(pool, 0, 8 * sizeof(std::uint16_t));
    f.manager.Flush();
    std::uint16_t* indices = f.manager.GetIndices(3);
    indices[0] = 0; indices[1] = 1; indices[2] = 2;
    CHECK(f.manager.Queue(0xb, 4, 0x20, 2, 3, vertices.data(), 3, indices) == 0);   // baseVertex 2
    CHECK(indices[0] == 0xfffe && indices[1] == 0xffff && indices[2] == 0);        // uint16 wrap, as the original
    f.manager.Flush();
    CHECK(f.host.draws.size() == 1);
    CHECK(f.host.draws[0].indices == std::vector<std::uint16_t>({0xfffe, 0xffff, 0}));
}

void TestDrawPrimsExtension() {
    Fixture f;
    f.manager.Reset();
    CHECK(f.manager.GetVertices(0, 3) != nullptr);           // drawStart 0, position 3
    f.manager.DrawPrims(0, gd::prim::Triangles, nullptr, 0);
    CHECK(f.host.applyTextureStateCalls == 1);
    CHECK(f.host.draws.size() == 1);
    CHECK(f.host.draws[0].kind == FakePrimitiveHost::DrawCall::PrimitiveVB);
    CHECK(f.host.draws[0].type == 4);
    CHECK(f.host.draws[0].start == 0 && f.host.draws[0].count == 3);

    CHECK(f.manager.ContinueVertices(0, 2) != nullptr);      // drawStart 3, position 5
    f.manager.DrawPrims(0, gd::prim::Triangles, nullptr, 0);
    CHECK(f.host.draws[1].start == 3 && f.host.draws[1].count == 2);

    const std::uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
    f.manager.DrawPrimsIndexed(0, gd::prim::Triangles, 6, indices, nullptr, 0);
    CHECK(f.host.isVertexArraySupportedCalls == 1);
    CHECK(f.host.draws[2].kind == FakePrimitiveHost::DrawCall::IndexedPrimitiveVB);
    CHECK(f.host.draws[2].type == 4);
    CHECK(f.host.draws[2].start == 3 && f.host.draws[2].count == 2);
    CHECK(f.host.draws[2].indices == std::vector<std::uint16_t>(std::begin(indices), std::end(indices)));
}

void TestImmediateDirectDraw() {
    Fixture f;
    const auto vertices = MakeVertices(3, 0x24, 0x60);
    // 0x25 = V3F_C4UB_T2F, canonical stride 0x24 (> 0x20) -> immediate path; FVF = XYZ|DIFFUSE|TEX1 = 0x142.
    CHECK(f.manager.Queue(0x25, 4, 0x24, 0, 3, vertices.data(), 0, nullptr) == 0);
    CHECK(f.host.draws.size() == 1);
    CHECK(f.host.draws[0].kind == FakePrimitiveHost::DrawCall::Primitive);
    CHECK(f.host.draws[0].type == 4);
    CHECK(f.host.draws[0].fvf == 0x142);
    CHECK(f.host.draws[0].vertices == vertices.data());
    CHECK(f.host.draws[0].count == 3);

    const std::uint16_t indices[6] = {0, 1, 2, 2, 1, 0};
    CHECK(f.manager.Queue(0x25, 4, 0x24, 2, 3, vertices.data(), 6, indices) == 0);
    CHECK(f.host.draws.size() == 2);
    CHECK(f.host.draws[1].kind == FakePrimitiveHost::DrawCall::IndexedPrimitive);
    CHECK(f.host.draws[1].count == 5);            // vertexCount + baseVertex, as the original passes it
    CHECK(f.host.draws[1].indices == std::vector<std::uint16_t>(std::begin(indices), std::end(indices)));
}

void TestImmediateStridedDraw() {
    Fixture f;
    const auto vertices = MakeVertices(2, 0x28, 0x70);
    // Same format, caller stride 0x28 != canonical 0x24 -> strided descriptor.
    CHECK(f.manager.Queue(0x25, 4, 0x28, 0, 2, vertices.data(), 0, nullptr) == 0);
    CHECK(f.host.draws.size() == 1);
    const FakePrimitiveHost::DrawCall& call = f.host.draws[0];
    CHECK(call.kind == FakePrimitiveHost::DrawCall::PrimitiveStrided);
    CHECK(call.fvf == 0x142);
    CHECK(call.strided.position.data == vertices.data());
    CHECK(call.strided.position.stride == 0x28);
    CHECK(call.strided.diffuse.data == vertices.data() + 0xc);
    CHECK(call.strided.diffuse.stride == 0x28);
    for (int i = 0; i < 6; ++i) {
        CHECK(call.strided.texCoord[i].data == vertices.data() + 0x10 + i * 8);
        CHECK(call.strided.texCoord[i].stride == 0x28);
    }

    // A packed format with a diffuse element and two texture-coordinate sets, stride > 0x20.
    const std::uint32_t packed = 0x8000810fu;   // pos 3, normal 3, diffuse 1, tex2
    const int canonical = nRZSimGL::StandardVFStride(packed);
    CHECK(canonical > 0x20);
    const auto wide = MakeVertices(2, static_cast<std::uint32_t>(canonical + 8), 0x80);
    CHECK(f.manager.Queue(packed, 1, static_cast<std::uint32_t>(canonical + 8), 0, 2, wide.data(), 0, nullptr) == 0);
    const FakePrimitiveHost::DrawCall& second = f.host.draws[1];
    CHECK(second.kind == FakePrimitiveHost::DrawCall::PrimitiveStrided);
    CHECK(second.fvf == 0x242);                  // XYZ|DIFFUSE|TEX2
    CHECK(second.strided.diffuse.data == wide.data() + 0xc);
    CHECK(second.strided.texCoord[0].data == wide.data() + 0x10);
}

void TestCopyC4UB() {
    Fixture f;
    std::vector<std::uint8_t> src = MakeVertices(2, 0x18, 0x90);
    std::vector<std::uint8_t> dst(2 * 0x20, 0xaa);

    // Format 10 = V3F_C4UB_T2F (24 bytes): the converter copies 24 bytes and leaves the last 8 untouched.
    CHECK(f.manager.CopyToVertexBuffer_V3F_C4UB_2T2F(10, 0x18, 2, src.data(), dst.data()));
    CHECK(std::memcmp(dst.data(), src.data(), 24) == 0);
    CHECK(dst[24] == 0xaa && dst[31] == 0xaa);                       // bytes past the 24 copied are untouched
    CHECK(std::memcmp(dst.data() + 32, src.data() + 24, 24) == 0);   // second vertex
    CHECK(dst[56] == 0xaa && dst[63] == 0xaa);

    // Format 0xb at stride 0x20 is a straight 32-byte memmove.
    std::vector<std::uint8_t> src32 = MakeVertices(2, 0x20, 0x40);
    std::vector<std::uint8_t> dst32(2 * 0x20, 0);
    CHECK(f.manager.CopyToVertexBuffer_V3F_C4UB_2T2F(0xb, 0x20, 2, src32.data(), dst32.data()));
    CHECK(dst32 == src32);

    // Non-canonical stride for 0xb: no converter matched, nothing is copied.
    std::vector<std::uint8_t> untouched(2 * 0x20, 0x5a);
    CHECK(f.manager.CopyToVertexBuffer_V3F_C4UB_2T2F(0xb, 0x24, 2, src32.data(), untouched.data()));
    for (std::uint8_t b : untouched) CHECK(b == 0x5a);

    // Unaligned stride: effective format becomes 0, again no copy.
    std::vector<std::uint8_t> untouched2(2 * 0x20, 0x6b);
    CHECK(f.manager.CopyToVertexBuffer_V3F_C4UB_2T2F(10, 0x19, 2, src.data(), untouched2.data()));
    for (std::uint8_t b : untouched2) CHECK(b == 0x6b);

    // V3F (format 0x20): position + opaque white, texture bytes untouched.
    std::vector<std::uint8_t> srcV3F = MakeVertices(1, 0x20, 0x11);
    std::vector<std::uint8_t> dstV3F(0x20, 0xaa);
    CHECK(f.manager.CopyToVertexBuffer_V3F_C4UB_2T2F(0x20, 0x20, 1, srcV3F.data(), dstV3F.data()));
    CHECK(std::memcmp(dstV3F.data(), srcV3F.data(), 12) == 0);
    CHECK(dstV3F[12] == 0xff && dstV3F[13] == 0xff && dstV3F[14] == 0xff && dstV3F[15] == 0xff);
    CHECK(dstV3F[16] == 0xaa);
}

void TestCopyC4F() {
    Fixture f;
    // 0xb at the canonical stride: colour bytes become floats, both texture coords are copied.
    std::vector<std::uint8_t> src(0x20, 0);
    for (int i = 0; i < 12; ++i) src[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i + 1);
    src[0xc] = 0x11; src[0xd] = 0x22; src[0xe] = 0x33; src[0xf] = 0x44;
    for (int i = 0; i < 16; ++i) src[0x10 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(0xa0 + i);
    std::vector<std::uint8_t> dst(0x30, 0xcc);
    CHECK(f.manager.CopyToVertexBuffer_V3F_C4F_2T2F(0xb, 0x20, 1, src.data(), dst.data()));
    CHECK(std::memcmp(dst.data(), src.data(), 12) == 0);
    const float* colour = reinterpret_cast<const float*>(dst.data() + 0x10);
    CHECK(NearlyEqual(colour[0], 0x44 / 255.0f));
    CHECK(NearlyEqual(colour[1], 0x33 / 255.0f));
    CHECK(NearlyEqual(colour[2], 0x22 / 255.0f));
    CHECK(NearlyEqual(colour[3], 0x11 / 255.0f));
    CHECK(std::memcmp(dst.data() + 0x20, src.data() + 0x10, 16) == 0);

    // V3F: position + white; the texture area is not written (0xcc survives).
    std::vector<std::uint8_t> srcV3F(0x20, 0x11);
    std::vector<std::uint8_t> dstV3F(0x30, 0xcc);
    CHECK(f.manager.CopyToVertexBuffer_V3F_C4F_2T2F(0x20, 0x20, 1, srcV3F.data(), dstV3F.data()));
    CHECK(std::memcmp(dstV3F.data(), srcV3F.data(), 12) == 0);
    const float* white = reinterpret_cast<const float*>(dstV3F.data() + 0x10);
    CHECK(white[0] == 1.0f && white[1] == 1.0f && white[2] == 1.0f && white[3] == 1.0f);
    CHECK(dstV3F[0x20] == 0xcc);

    // 0xb with a non-canonical stride falls through without copying.
    std::vector<std::uint8_t> untouched(0x30, 0xdd);
    CHECK(f.manager.CopyToVertexBuffer_V3F_C4F_2T2F(0xb, 0x24, 1, src.data(), untouched.data()));
    for (std::uint8_t b : untouched) CHECK(b == 0xdd);
}

void TestFastATICopyPath() {
    FakePrimitiveHost host;
    host.fastATI = true;
    cPrimitiveManager manager{host};
    manager.Init(nullptr, nullptr);
    manager.Restart(nullptr, nullptr);
    CHECK(host.created[0].buffer->stride == 0x30);

    const auto vertices = MakeVertices(2, 0x20, 0x20);
    CHECK(manager.Queue(0xb, 4, 0x20, 0, 2, vertices.data(), 0, nullptr) == 0);
    CHECK(manager.PrimitiveAt(0).dataBytes == 2 * 0x30);   // GetFVFSize(0x242) == 0x30 on the fast-ATI path
    CHECK(std::memcmp(host.created[0].buffer->Bytes(), vertices.data(), 12) == 0);
    const float* colour = reinterpret_cast<const float*>(host.created[0].buffer->Bytes() + 0x10);
    CHECK(NearlyEqual(colour[0], static_cast<float>(vertices[0xf]) / 255.0f));
}

void TestStopShutdownRestart() {
    Fixture f;
    f.manager.Reset();
    CHECK(f.buffer()->allocateCalls == 2);           // once from Fixture::Restart, once here
    // GetIndices first: growing the pool would FlushInternal and drop a queued batch.
    CHECK(f.manager.GetIndices(7) != nullptr);
    const auto vertices = MakeVertices(1, 0x20, 0x90);
    CHECK(f.manager.Queue(0xb, 4, 0x20, 0, 1, vertices.data(), 0, nullptr) == 0);
    CHECK(f.manager.PrimitiveCount() == 1);
    CHECK(f.manager.IndexPoolSize() == 7);
    CHECK(f.manager.IndexCursor() == 7);

    f.manager.Stop();
    CHECK(!f.manager.IsStarted());
    CHECK(f.buffer()->stopCalls == 1);
    CHECK(f.manager.VertexBufferCount() == 1);
    CHECK(f.manager.PrimitiveCount() == 0);          // Stop clears +0x30/+0x38 ...
    CHECK(f.manager.IndexPoolSize() == 7);           // ... but not the index pool (+0x48)
    CHECK(f.manager.Queue(0xb, 4, 0x20, 0, 1, nullptr, 0, nullptr) == 0x80000008L);   // 0x80000008 as a long

    f.manager.Restart(nullptr, nullptr);       // Reset runs again
    CHECK(f.manager.IsStarted());
    CHECK(f.buffer()->allocateCalls == 3);

    f.manager.Shutdown();                       // Shutdown drops the index pool as well
    CHECK(!f.manager.IsStarted());
    CHECK(f.host.destroyedBuffers == 1);
    CHECK(f.manager.VertexBufferCount() == 0);
    CHECK(f.manager.IndexPoolSize() == 0);
    CHECK(f.manager.IndexCursor() == 7);       // Stop/Shutdown never touch +0x60 (kept quirk)

    // A fresh Init works after Shutdown (the original just pushes a new cVertexBuffer).
    f.manager.Init(nullptr, nullptr);
    CHECK(f.manager.VertexBufferCount() == 1);
}

void TestQueryInterfaceAndRefCount() {
    FakePrimitiveHost host;
    auto* manager = new cPrimitiveManager(host);
    manager->Init(nullptr, nullptr);
    CHECK(manager->RefCount() == 0);

    void* out = nullptr;
    CHECK(manager->QueryInterface(1, &out));
    CHECK(out == static_cast<IPrimitiveManagerHost*>(manager));
    CHECK(manager->RefCount() == 1);
    CHECK(manager->QueryInterface(0x9cd86f9, &out));
    CHECK(out == static_cast<IPrimitiveManagerHost*>(manager));
    CHECK(manager->RefCount() == 2);
    CHECK(!manager->QueryInterface(0x12345678, &out));
    CHECK(manager->RefCount() == 2);

    CHECK(manager->Release() == 1);
    manager->Release();                        // count reaches 0: the refcount deletes the object

    auto* second = new cPrimitiveManager(host);
    second->AddRef();
    CHECK(second->RefCount() == 1);
    second->Release();
}

}  // namespace

int main() {
    TestInitCapsAndStart();
    TestVertexBufferAccessors();
    TestIndexPool();
    TestQueueCopyAndFlush();
    TestMergeIndexedStrips();
    TestMergeNonIndexedStrips();
    TestNoMergeOnDifferentPrimitiveType();
    TestBaseVertexOffsetsIndices();
    TestDrawPrimsExtension();
    TestImmediateDirectDraw();
    TestImmediateStridedDraw();
    TestCopyC4UB();
    TestCopyC4F();
    TestFastATICopyPath();
    TestStopShutdownRestart();
    TestQueryInterfaceAndRefCount();
    if (g_failures == 0) std::printf("render_primitivemanager: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
