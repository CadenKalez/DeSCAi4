#include <cstdio>
#include <string>

#include "fake_drawcontext.h"
#include "gz/DrawContextAsBuffer.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
cGZBuffer* MakeBuffer(std::uint32_t w, std::uint32_t h, std::int32_t type, std::uint32_t bits) {
    auto* b = new cGZBuffer();
    b->AddRef();
    b->Init(w, h, {type, bits});
    return b;
}
bool Has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}
}  // namespace

int main() {
    fake_dc::Ctx ctx;
    auto* target = MakeBuffer(8, 6, kBufferColorRGB565, 16);
    auto* adapter = new cGZDrawContextAsBuffer();
    adapter->AddRef();
    adapter->Init(&ctx, target);
    CHECK(ctx.refs == 2 && ctx.attached == false);                     // the adapter holds the context and detaches it
    CHECK(adapter->Width() == 8 && adapter->Height() == 6 && adapter->GetBufferArea()->right == 8);
    cRZRect area;
    adapter->GetBufferArea(area);
    CHECK(area.bottom == 6);
    const cGZBufferColorType type = adapter->GetColorType();
    CHECK(type.type == kBufferColorRGB565 && type.bitsPerPixel == 16 && adapter->GetBytesPerPixel() == 2 && adapter->GetBitsPerPixel() == 16);
    CHECK(adapter->IsReady() && adapter->GetMemoryUsage() == 0 && !adapter->IsLocked());
    CHECK(!adapter->Init(8, 6, {kBufferColorRGB565, 16}) && adapter->Shutdown() && !adapter->Uninitialize());

    // interfaces
    void* raw = nullptr;
    CHECK(adapter->QueryInterface(kGZIID_cIGZBufferDrawTarget, &raw) && raw == static_cast<cIGZBufferDrawTarget*>(adapter));
    static_cast<cIGZBufferDrawTarget*>(raw)->Release();
    CHECK(adapter->QueryInterface(kGZIID_cIGZBuffer, &raw) && raw == static_cast<cIGZBuffer*>(adapter));
    adapter->Release();
    CHECK(adapter->QueryInterface(kGZIID_cIGZBufferShade, &raw));     // answered by the real buffer
    static_cast<cIGZUnknown*>(raw)->Release();
    CHECK(!adapter->QueryInterface(0x12345678, &raw));
    CHECK(adapter->AsIGZBuffer() == static_cast<cIGZBuffer*>(adapter));

    // the context is attached lazily and left suspended
    CHECK(adapter->GetDrawContext() == &ctx && ctx.attached && ctx.suspended);
    ctx.calls.clear();
    CHECK(adapter->GetDrawContext() == &ctx && ctx.calls.empty());     // already attached

    // locking: the first lock resumes a suspended context, the last unlock suspends it again
    ctx.suspended = true;
    CHECK(adapter->Lock(cIGZBuffer::kLockRead) && adapter->IsLocked());
    CHECK(!ctx.suspended && Has(ctx.calls, "lock w"));      // the context's flag is (int16)flags >= 0: true for a read lock
    ctx.calls.clear();
    CHECK(adapter->Lock(cIGZBuffer::kLockWrite) && Has(ctx.calls, "lock r"));     // nested; the 0x8000 flag makes the context's flag false
    CHECK(adapter->Unlock(cIGZBuffer::kLockWrite) && adapter->IsLocked() && !ctx.suspended);
    CHECK(adapter->Unlock(cIGZBuffer::kLockRead) && !adapter->IsLocked() && ctx.suspended);
    CHECK(adapter->LockContext() == &ctx && adapter->IsLocked());
    adapter->UnlockContext();
    CHECK(!adapter->IsLocked());

    // colour conversion to the context's ARGB32
    CHECK(adapter->NativeToARGB32(0xffff) == 0xffffffffu && adapter->NativeToARGB32(0) == 0xff000000u);
    CHECK(adapter->NativeToARGB32(0xf800) == 0xfff80000u + 0x70000u && adapter->NativeToARGB32(0x001f) == 0xff0000f8u + 7);
    CHECK(adapter->ConvertRGBValueToNative(255, 255, 255) == 0x1efffu);   // the 0xf000 the original adds is kept
    std::uint8_t r = 0, g = 0, b = 0;
    CHECK(adapter->ConvertNativeValueToRGB(0xffff, r, g, b) && r == 255 && g == 255 && b == 255);
    CHECK(adapter->ConvertRGBAValueToNative(1, 2, 3, 4) == 0 && !adapter->ConvertNativeValueToRGBA(0, r, g, b, r));

    // fills and pixels go through the context
    ctx.calls.clear();
    const cRZRect clip{1, 1, 4, 4};
    CHECK(adapter->Fill(0xffff, cRZRect{0, 0, 3, 3}, &clip));
    CHECK(ctx.calls.size() == 5 && ctx.calls[0] == "push" && ctx.calls[1] == "clip 1,1,4,4" && ctx.calls[2] == "color 0 4294967295" && ctx.calls[3] == "pop" && ctx.calls[4] == "flush");
    CHECK(ctx.log.back() == "fill 0,0,3,3");
    ctx.calls.clear();
    CHECK(adapter->Fill(0, 1, 2, 3, 4, nullptr) && ctx.calls[0] == "push" && ctx.calls[1] == "color 0 4278190080");
    ctx.calls.clear();
    CHECK(adapter->Fill(0xffff, nullptr) && Has(ctx.calls, "clear"));
    ctx.calls.clear();
    adapter->SetPixel(3, 4, 0xffff);
    CHECK(ctx.calls.size() == 3 && ctx.calls[1] == "point 3,4" && ctx.calls[2] == "flush");

    // GetPixel reads the locked surface by pixel size
    std::uint8_t surface[64] = {};
    for (int i = 0; i < 64; ++i) surface[i] = std::uint8_t(i + 1);
    ctx.lockDesc.pixels = surface;
    ctx.lockDesc.pitch = 16;
    ctx.lockDesc.bytesPerPixel = 1;
    CHECK(adapter->GetPixel(2, 1) == surface[16 + 2]);
    ctx.lockDesc.bytesPerPixel = 2;
    CHECK(adapter->GetPixel(2, 1) == std::uint32_t(surface[20]) + (std::uint32_t(surface[21]) << 8));
    ctx.lockDesc.bytesPerPixel = 3;
    CHECK(adapter->GetPixel(1, 1) == (std::uint32_t(surface[19]) | std::uint32_t(surface[20]) << 8 | std::uint32_t(surface[21]) << 16));
    ctx.lockDesc.bytesPerPixel = 4;
    CHECK(adapter->GetPixel(1, 0) == (std::uint32_t(surface[4]) | std::uint32_t(surface[5]) << 8 | std::uint32_t(surface[6]) << 16 | std::uint32_t(surface[7]) << 24));
    ctx.lockDesc.bytesPerPixel = 7;
    CHECK(adapter->GetPixel(0, 0) == 0);
    ctx.lockResult = false;
    CHECK(adapter->GetPixel(0, 0) == 0);
    ctx.lockResult = true;

    // transparency is the real buffer's, with the key compared against the locked surface
    CHECK(!adapter->GetTransparency());
    adapter->SetTransparency(0x0201);
    std::uint32_t key = 0;
    CHECK(adapter->GetTransparency() && adapter->GetTransparentColor(key) && key == 0x0201 && target->GetTransparency());
    ctx.lockDesc.bytesPerPixel = 2;
    ctx.lockDesc.width = 8;                                                 // a lock replaces the adapter's surface description
    ctx.lockDesc.height = 6;
    adapter->Lock(cIGZBuffer::kLockRead);
    adapter->Unlock(cIGZBuffer::kLockRead);
    surface[0] = 0x01; surface[1] = 0x02;
    CHECK(adapter->IsPixelTransparent(0, 0) == true);                       // the pixel equals the key
    CHECK(adapter->IsPixelTransparent(1, 0) == false);                      // a different pixel
    ctx.origin = {3, 0};
    CHECK(adapter->IsPixelTransparent(10, 0) == false);                     // beyond the right edge
    ctx.origin = {0, 0};
    adapter->RemoveTransparency();
    CHECK(!adapter->GetTransparency() && !target->GetTransparency() && !adapter->IsPixelTransparent(0, 0));

    // lines: thin ones use the context line, thick ones a shifted polyline
    ctx.log.clear();
    ctx.calls.clear();
    CHECK(adapter->DrawLine(0, 0, 5, 2, 0xffff, 1) && ctx.log.back() == "line 0,0,5,2" && Has(ctx.calls, "thick 1"));
    ctx.calls.clear();
    CHECK(adapter->DrawLine(0, 0, 5, 2, 0xffff, 3) && Has(ctx.calls, "draw 1 0,1,5,3 n=2") && Has(ctx.calls, "thick 3"));   // shallow: moved down by 1
    ctx.calls.clear();
    CHECK(adapter->DrawLine(0, 0, 2, 5, 0xffff, 3) && Has(ctx.calls, "draw 1 1,0,3,5 n=2"));                                // steep: moved right

    // blits take the destination's size at the source's corner, with the source's shade colour
    auto* source = MakeBuffer(8, 8, kBufferColorRGB565, 16);
    source->SetShadeColor(0x10, 0x20, 0x30, 0x40);
    ctx.log.clear();
    ctx.calls.clear();
    CHECK(adapter->Blt(source, cRZRect{2, 3, 99, 99}, cRZRect{10, 10, 14, 12}, &clip));
    CHECK(ctx.calls[0] == "push" && ctx.calls[1] == "clip 1,1,4,4" && ctx.log[0] == "color " + std::to_string(0x40102030u));
    bool found = false;
    for (const auto& l : ctx.log) if (l == "blt 2,3,6,5>10,10,14,12") found = true;
    CHECK(found && ctx.calls.back() == "flush");
    source->RemoveTransparency();
    CHECK(!source->GetTransparency());
    adapter->SetBlitSource(source);
    source->Release();

    // the hardware-cache hooks and change counter are the buffer's
    CHECK(adapter->GetChangeCounter() == target->GetChangeCounter() && adapter->GetHardwareCache() == target->GetHardwareCache());
    CHECK(adapter->GetColorSurfaceBits() == nullptr || adapter->GetColorSurfaceBits() == surface);

    // resetting the size picks up the buffer's new area
    ctx.calls.clear();
    adapter->ResetSize();
    CHECK(Has(ctx.calls, "untarget") && Has(ctx.calls, "target") && ctx.calls.back() == "suspend");

    // the other colour types
    for (int t : {kBufferColorXRGB1555, kBufferColorARGB1555, kBufferColorARGB4444, kBufferColorXRGB8888, kBufferColorARGB8888}) {
        fake_dc::Ctx c2;
        auto* tb = MakeBuffer(2, 2, t, (t >= kBufferColorXRGB8888) ? 32 : 16);
        auto* a2 = new cGZDrawContextAsBuffer();
        a2->AddRef();
        a2->Init(&c2, tb);
        const std::uint32_t white = a2->ConvertRGBValueToNative(255, 255, 255);
        if (t >= kBufferColorXRGB8888) CHECK(white == 0xffffffffu && a2->NativeToARGB32(0x123456) == 0xff123456u && a2->GetColorType().bitsPerPixel == 32);
        if (t == kBufferColorARGB4444) CHECK(a2->NativeToARGB32(0x0fff) == 0xffffffffu && a2->NativeToARGB32(0) == 0xff000000u);
        if (t == kBufferColorXRGB1555 || t == kBufferColorARGB1555) CHECK(a2->NativeToARGB32(0x7fff) == 0xffffffffu);
        a2->Release();
        tb->Release();
    }

    adapter->Release();
    CHECK(ctx.refs == 1);                                                   // destruction released the context
    target->Release();
    std::printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
