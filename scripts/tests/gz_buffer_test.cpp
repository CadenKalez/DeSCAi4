#include <cstdio>

#include "gz/Buffer.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
cGZBuffer* Make(std::uint32_t w, std::uint32_t h, std::int32_t type, std::uint32_t bits) {
    auto* b = new cGZBuffer();
    b->AddRef();
    CHECK(b->Init(w, h, {type, bits}));
    b->Fill(0, nullptr);
    return b;
}
}  // namespace

int main() {
    auto* b = Make(10, 8, kBufferColorRGB565, 16);
    CHECK(b->Width() == 10 && b->Height() == 8 && b->GetBitsPerPixel() == 16 && b->GetBytesPerPixel() == 2);
    CHECK(b->GetColorSurfaceStride() == 20 && b->GetMemoryUsage() == 2 * 8 * 12);
    CHECK(!b->Init(4, 4, {kBufferColorRGB565, 16}));          // already initialised
    auto* e = new cGZBuffer();
    e->AddRef();
    CHECK(!e->Init(0, 4, {kBufferColorRGB565, 16}) && !e->IsReady());
    e->Release();

    // conversions
    CHECK(b->ConvertRGBValueToNative(255, 255, 255) == 0xffff);
    CHECK(b->ConvertRGBValueToNative(255, 0, 0) == 0xf800);
    std::uint8_t r, g, bl, a;
    CHECK(b->ConvertNativeValueToRGB(0xf800, r, g, bl) && r == 255 && g == 0 && bl == 0);
    CHECK(b->ConvertNativeValueToRGBA(0x07e0, r, g, bl, a) && g == 255 && a == 0xff);
    auto* b32 = Make(4, 4, kBufferColorARGB8888, 32);
    CHECK(b32->ConvertRGBValueToNative(1, 2, 3) == 0xff010203);
    CHECK(b32->ConvertRGBAValueToNative(1, 2, 3, 4) == 0x04010203);
    CHECK(b32->ConvertNativeValueToRGBA(0x80102030, r, g, bl, a) && a == 0x80 && r == 0x10 && g == 0x20 && bl == 0x30);
    auto* b4444 = Make(4, 4, kBufferColorARGB4444, 16);
    CHECK(b4444->ConvertRGBValueToNative(0xff, 0x10, 0x20) == 0xff12);
    CHECK(b4444->ConvertNativeValueToRGBA(0x8f12, r, g, bl, a) && a == 0x80 && r == 0xff && g == 0x11 && bl == 0x22);

    // fill with clipping, pixel access, change counter
    const std::uint32_t before = b->GetChangeCounter();
    cRZRect rect{2, 1, 6, 3};
    CHECK(b->Fill(0xabcd, rect, nullptr));
    CHECK(b->GetChangeCounter() == before + 1);
    CHECK(b->GetPixel(2, 1) == 0xabcd && b->GetPixel(5, 2) == 0xabcd && b->GetPixel(6, 2) == 0 && b->GetPixel(2, 3) == 0);
    cRZRect clip{3, 0, 5, 8};
    CHECK(b->Fill(0x1111, 0, 0, 20, 20, &clip));            // clipped to the buffer and the clip rectangle
    CHECK(b->GetPixel(3, 0) == 0x1111 && b->GetPixel(4, 7) == 0x1111 && b->GetPixel(2, 0) == 0 && b->GetPixel(5, 0) == 0);
    cRZRect outside{20, 20, 30, 30};
    CHECK(!b->Fill(1, outside, nullptr));
    b->SetPixel(9, 7, 0x1234);
    CHECK(b->GetPixel(9, 7) == 0x1234);

    // lines: horizontal, vertical, diagonal
    b->Fill(0, nullptr);
    CHECK(b->DrawLine(1, 2, 7, 2, 0x5555, 1));
    for (int x = 1; x <= 7; ++x) CHECK(b->GetPixel(x, 2) == 0x5555);
    CHECK(b->GetPixel(0, 2) == 0 && b->GetPixel(8, 2) == 0);
    b->Fill(0, nullptr);
    CHECK(b->DrawLine(3, 0, 3, 5, 0x6666, 1));
    for (int y = 0; y <= 5; ++y) CHECK(b->GetPixel(3, y) == 0x6666);
    b->Fill(0, nullptr);
    CHECK(b->DrawLine(0, 0, 4, 4, 0x7777, 1));
    for (int i = 0; i <= 4; ++i) CHECK(b->GetPixel(i, i) == 0x7777);
    CHECK(b->GetPixel(1, 0) == 0);
    b->Fill(0, nullptr);
    CHECK(b->DrawLine(9, 7, 0, 0, 0x8888, 1));               // reversed, exits the clip box corner to corner
    CHECK(b->GetPixel(0, 0) == 0x8888 || b->GetPixel(9, 7) == 0x8888);
    b->Fill(0, nullptr);
    CHECK(b->DrawLine(2, 1, 5, 4, 0x9999, 2));               // thick: 2x2 boxes ahead of the line
    CHECK(b->GetPixel(2, 1) == 0x9999 && b->GetPixel(3, 2) == 0x9999 && b->GetPixel(6, 5) == 0x9999 && b->GetPixel(7, 5) == 0);

    // clipping helper
    int x0 = -5, y0 = 5, x1 = 15, y1 = 5;
    CHECK(cGZBuffer::ClipRect(&x0, &y0, &x1, &y1, 0, 0, 9, 9) && x0 == 0 && x1 == 9 && y0 == 5);
    x0 = -5; y0 = -5; x1 = -1; y1 = -1;
    CHECK(!cGZBuffer::ClipRect(&x0, &y0, &x1, &y1, 0, 0, 9, 9));

    // transparency / colour key
    CHECK(!b->GetTransparency() && !b->IsPixelTransparent(0, 0));
    b->SetTransparency(0);
    std::uint32_t key = 99;
    CHECK(b->GetTransparency() && b->GetTransparentColor(key) && key == 0 && b->IsPixelTransparent(0, 0));
    CHECK(!b->IsPixelTransparent(10, 0));
    b->RemoveTransparency();
    CHECK(!b->GetTransparentColor(key));

    // blit between formats (565 -> 8888), keyed and plain, clipped on the destination
    auto* src = Make(4, 4, kBufferColorRGB565, 16);
    src->Fill(0xf800, nullptr);
    src->SetPixel(0, 0, 0x001f);
    cRZRect srcRect{0, 0, 4, 4};
    cRZRect at{2, 2, 6, 6};
    CHECK(b32->Blt(src, srcRect, at, nullptr));
    CHECK(b32->GetPixel(2, 2) == 0xff0000ff && b32->GetPixel(3, 2) == 0xffff0000);
    CHECK(b32->GetPixel(1, 1) == 0 && b32->GetPixel(3, 3) == 0xffff0000);
    b32->Fill(0, nullptr);
    src->SetTransparency(0xf800);
    CHECK(b32->Blt(src, srcRect, cRZRect{0, 0, 4, 4}, nullptr));
    CHECK(b32->GetPixel(0, 0) == 0xff0000ff && b32->GetPixel(1, 0) == 0);   // keyed pixels skipped
    // overlapping self blit moves rows bottom-up
    auto* self = Make(4, 4, kBufferColorRGB565, 16);
    for (int y = 0; y < 4; ++y) self->Fill(0x100 + y, 0, y, 4, y + 1, nullptr);
    cRZRect two{0, 0, 4, 3};
    CHECK(self->Blt(self, two, cRZRect{0, 1, 4, 4}, nullptr));
    CHECK(self->GetPixel(0, 0) == 0x100 && self->GetPixel(0, 1) == 0x100 && self->GetPixel(0, 2) == 0x101 &&
          self->GetPixel(0, 3) == 0x102);

    // shade colour, locks, hardware cache
    CHECK(!b->GetShadeColor(r, g, bl, a));
    void* shade = nullptr;
    CHECK(b->QueryInterface(kGZIID_cIGZBufferShade, &shade));
    auto* shadeView = static_cast<cIGZBufferShade*>(shade);
    CHECK(shadeView->SetShadeColor(1, 2, 3, 4) && shadeView->GetShadeColor(r, g, bl, a) && r == 1 && a == 4);
    shadeView->Release();
    CHECK(!b->IsLocked() && b->Lock(cIGZBuffer::kLockWrite) && b->IsLocked() && b->Unlock(cIGZBuffer::kLockWrite) && !b->IsLocked());
    CHECK(b->Shutdown() && !b->IsReady() && b->GetColorSurfaceBits() == nullptr && !b->Lock(cIGZBuffer::kLockRead));

    // display format resolution without a graphics system falls back to RGB565
    auto* d = new cGZBuffer();
    d->AddRef();
    CHECK(d->Init(2, 2, {kBufferColorDefault, 16}) && d->GetColorType().type == kBufferColorRGB565);
    d->Release();

    b->Release(); b32->Release(); b4444->Release(); src->Release(); self->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
