// cWinBMPView: the zoom / scroll / centring geometry, the image rectangle, painting through a real GZ buffer, the
// right-button drag message, and the Gimex-backed file helpers.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "fake_winmgr.h"
#include "gz/GimexFactory.h"
#include "gz/WinBMP.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "sc4/WinBMPView.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

constexpr std::uint32_t kImageColor = 0xff112233u;
constexpr std::uint32_t kFillColor = 0xff336699u;

using Mgr = FakeMgr;

// A cGZGimexReader that serves one 8-bit image, so the real factory load path can run without an image file.
struct Reader final : cIGZGimexReader {
    int refs = 0;
    cGZGimexInfo info;
    std::uint8_t pixels[128 * 64];
    std::uint32_t imageCount = 1;
    bool GetFramePixelsCalled = false;
    Reader() {
        std::memset(pixels, 0, sizeof pixels);
        info.width = 4;
        info.height = 4;
        info.bitsPerPixel = 8;
        // palette entry bytes are blue, green, red
        info.palette[4] = 0x44; info.palette[5] = 0x55; info.palette[6] = 0x66;
        info.palette[0] = 0x11; info.palette[1] = 0x22; info.palette[2] = 0x33;
        pixels[0] = 1;   // the first pixel uses the second palette entry
    }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (--refs <= 0) { delete this; return 0; } return refs; }
    bool SetInput(cIGZFile*) override { return true; }
    bool SetInput(cIGZPersistDBSerialRecord*) override { return true; }
    bool BeginRead(std::uint32_t format, std::uint32_t& count) override { (void)format; count = imageCount; return true; }
    bool BeginRead(const char* extension, std::uint32_t& count) override { (void)extension; count = imageCount; return true; }
    cGZGimexInfo* GetFrameInfo(std::uint32_t index) override { return index == 0 ? &info : nullptr; }
    bool GetFramePixels(cGZGimexInfo*, void* out, std::uint32_t bytes) override {
        GetFramePixelsCalled = true;
        const std::size_t total = static_cast<std::size_t>(info.width) * static_cast<std::size_t>(info.height);
        std::memcpy(out, pixels, std::min<std::size_t>(total, bytes * static_cast<std::size_t>(info.height)));
        return true;
    }
    void FreeGInfo(cGZGimexInfo*) override {}
    bool EndRead() override { return true; }
};

struct Host final : IWinBMPViewHost {
    cIGZBuffer* drawBuffer = nullptr;
    cIGZBuffer* scaled = nullptr;
    int factoryRequests = 0, scaleRequests = 0, readersMade = 0;
    int readerWidth = 4, readerHeight = 4;
    cIGZBuffer* DrawBuffer(cGZWin&) override { return drawBuffer; }
    cGZGimexFactory* CreateGimexFactory() override {
        ++factoryRequests;
        auto* factory = new cGZGimexFactory;
        factory->makeReader = [this]() -> cIGZGimexReader* {
            ++readersMade;
            auto* reader = new Reader;
            reader->info.width = readerWidth;
            reader->info.height = readerHeight;
            return reader;
        };
        factory->makeBuffer = []() -> cIGZBuffer* {
            auto* buffer = new cGZBuffer;
            buffer->AddRef();
            return buffer;
        };
        return factory;
    }
    cIGZBuffer* DownscaleImageToFit(cIGZBuffer*, int, int) override { ++scaleRequests; return scaled; }
};

std::uint64_t g_now = 1000;
std::uint64_t Clock() { return g_now; }

cGZBuffer* NewImage(int w, int h, std::uint32_t color) {
    auto* image = new cGZBuffer;
    image->AddRef();
    image->Init(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), {kBufferColorARGB8888, 32});
    cRZRect area{0, 0, w, h};
    image->Fill(color, area, nullptr);
    return image;
}

cWinBMPView* NewView(Mgr& mgr, int w, int h) {
    auto* view = new cWinBMPView(&mgr);
    view->AddRef();
    view->SetArea(0, 0, w, h);
    return view;
}

bool SameRect(const cRZRect& r, int l, int t, int rr, int b) {
    return r.left == l && r.top == t && r.right == rr && r.bottom == b;
}

}  // namespace

int main() {
    Mgr mgr;
    FakeWinGraphics graphics;
    SetWinGraphics(&graphics);
    SetWinBMPViewClock(&Clock);
    SetWinBMPViewHost(nullptr);   // the null host first: painting and loading do nothing

    Host host;
    auto* view = NewView(mgr, 64, 32);
    view->SetFillColorRGB(0x336699);
    CHECK(view->Zoom() == 1.0f && view->ZoomMin() == 0.25f && view->ZoomMax() == 4.0f);
    CHECK(view->ZoomingEnabled() && view->ScrollingEnabled() && !view->ShouldFitImageInView());
    CHECK(view->MinMsPerScroll() == 0x14 && view->GetAlignment() == cIWinBMPView::kAlignTopLeft);
    CHECK(SameRect(view->ImageDest(), 0, 0, 0, 0));
    CHECK(view->GetImageWidth() == 0 && view->GetImageHeight() == 0);

    // the viewer interface is a real base subobject and QueryInterface hands it out with a reference
    void* out = nullptr;
    CHECK(!view->QueryInterface(0x1234, &out));
    CHECK(view->QueryInterface(kGZIID_cIWinBMPView, &out) && out == static_cast<cIWinBMPView*>(view));
    CHECK(view->QueryInterface(gz::kGZIID_cIGZWinBMP, &out) && out == static_cast<gz::cIGZWinBMP*>(view));
    CHECK(view->QueryInterface(1, &out) && out == static_cast<cGZWin*>(view));
    CHECK(view->QueryInterface(0x22ba0121, &out) && view->QueryInterface(0xe98b2f57, &out));
    for (int i = 0; i < 5; ++i) static_cast<cIGZWin*>(view)->Release();   // the references QueryInterface took

    // ---- metrics: top-left alignment, scroll clamping, the image rectangle ----
    auto* image = NewImage(100, 50, kImageColor);
    view->SetBMP(image);
    CHECK(view->GetImageWidth() == 100 && view->GetImageHeight() == 50);
    CHECK(SameRect(view->ImageDest(), 0, 0, 100, 50));
    view->SetOrigin(20, 10);
    CHECK(SameRect(view->ImageDest(), -20, -10, 80, 40));
    view->SetOrigin(1000, 1000);   // clamped to scaled size - window size
    CHECK(view->OriginX() == 36 && view->OriginY() == 18);
    CHECK(SameRect(view->ImageDest(), -36, -18, 64, 32));
    view->SetOrigin(-7, -7);       // clamped to zero
    CHECK(view->OriginX() == 0 && view->OriginY() == 0);

    // ---- zoom: clamp in the setter, 1:1 blit threshold, integer truncation of the scaled size ----
    view->SetZoom(2.0f);          // zooms about the window centre: the origin moves with it
    CHECK(view->Zoom() == 2.0f && view->OriginX() == 32 && view->OriginY() == 16);
    view->SetOrigin(0, 0);
    CHECK(SameRect(view->ImageDest(), 0, 0, 200, 100));
    view->SetZoom(100.0f);
    CHECK(view->Zoom() == 4.0f);
    view->SetZoom(0.1f);
    CHECK(view->Zoom() == 0.25f);
    view->SetOrigin(0, 0);
    CHECK(SameRect(view->ImageDest(), 0, 0, 25, 12));   // 50 * 0.25 = 12.5 truncated
    view->SetZoomRangeMinMax(0.5f, 3.0f);
    CHECK(view->ZoomMin() == 0.5f && view->ZoomMax() == 3.0f);
    view->SetZoom(10.0f);
    CHECK(view->Zoom() == 3.0f);
    view->SetZoom(0.1f);
    CHECK(view->Zoom() == 0.5f);
    view->SetZoomRangeMinMax(-5.0f, 1000.0f);   // clamped to 0.25 .. 256
    CHECK(view->ZoomMin() == 0.25f && view->ZoomMax() == 256.0f);
    // a minimum above the maximum is not rejected: CalculateAllMetrics clamps to the minimum first (kept quirk)
    view->SetZoomRangeMinMax(5.0f, 1.0f);
    view->SetZoom(10.0f);
    CHECK(view->Zoom() == 5.0f);
    view->SetZoomRangeMinMax(0.25f, 4.0f);
    view->SetZoom(1.0f);
    CHECK(view->Zoom() == 1.0f);
    CHECK(view->ZoomOut(0.1f) == false);   // below the minimum: refused
    CHECK(view->Zoom() == 1.0f);
    view->ZoomIn(2.0f);
    CHECK(view->Zoom() == 2.0f);

    // ---- centred alignment: origin is the image pixel at the window centre ----
    view->SetZoom(1.0f);
    view->SetAlignment(cIWinBMPView::kAlignCenter);
    CHECK(view->OriginX() == 32 && view->OriginY() == 16);   // clamped to half the window
    CHECK(SameRect(view->ImageDest(), 0, 0, 100, 50));
    view->SetOrigin(50, 30);
    CHECK(view->OriginX() == 50 && view->OriginY() == 30);
    CHECK(SameRect(view->ImageDest(), -18, -14, 82, 36));
    view->SetAlignment(cIWinBMPView::kAlignTopLeft);
    CHECK(view->OriginX() == 36 && view->OriginY() == 18);
    CHECK(SameRect(view->ImageDest(), -36, -18, 64, 32));

    // ---- centre on an image pixel / convert a window pixel ----
    view->SetOrigin(0, 0);
    CHECK(view->CenterOnImagePixel(50, 25));
    CHECK(view->OriginX() == 18 && view->OriginY() == 9);
    CHECK(SameRect(view->ImageDest(), -18, -9, 82, 41));
    CHECK(view->CenterOnImagePixel(99, 49));
    CHECK(view->OriginX() == 36 && view->OriginY() == 18);
    CHECK(!view->CenterOnImagePixel(100, 10));
    CHECK(!view->CenterOnImagePixel(-1, 10));
    CHECK(!view->CenterOnImagePixel(10, 50));

    view->SetOrigin(0, 0);
    view->CenterOnImagePixel(50, 25);
    int ix = 0, iy = 0;
    CHECK(view->WindowPixelToImagePixel(0, 0, ix, iy) && ix == 18 && iy == 9);
    CHECK(!view->WindowPixelToImagePixel(-19, 0, ix, iy) && ix == -1);
    CHECK(view->WindowPixelToImagePixel(63, 31, ix, iy) && ix == 81 && iy == 40);
    CHECK(!view->WindowPixelToImagePixel(63, 41, ix, iy) && iy == 50);
    view->SetZoom(2.0f);
    view->SetOrigin(0, 0);
    CHECK(view->WindowPixelToImagePixel(10, 10, ix, iy) && ix == 5 && iy == 5);
    CHECK(!view->WindowPixelToImagePixel(10, 201, ix, iy));

    // ---- centre on a window pixel (left mouse button) ----
    view->SetZoom(1.0f);
    view->SetOrigin(0, 0);
    view->GZOnMouseDownL(40, 20, 0);
    CHECK(view->OriginX() == 8 && view->OriginY() == 4);
    view->EnableScrolling(false);
    CHECK(!view->ScrollingEnabled());
    view->GZOnMouseDownL(60, 30, 0);   // scrolling off: no centring
    CHECK(view->OriginX() == 8 && view->OriginY() == 4);
    view->EnableScrolling(true);

    // ---- scroll steps are image size / 32 (kept quirk), at least one ----
    view->SetOrigin(0, 0);
    view->Scroll(1, 1);
    CHECK(view->OriginX() == 3 && view->OriginY() == 1);   // 100/32 = 3, 50/32 = 1
    view->Scroll(-1, -1);
    CHECK(view->OriginX() == 0 && view->OriginY() == 0);
    view->Scroll(5, 4);
    CHECK(view->OriginX() == 5 && view->OriginY() == 4);
    view->SetOrigin(0, 0);
    CHECK(view->Scroll(0, 0));

    // ---- keys: arrows scroll, k/m zoom, the enable flags gate them ----
    view->SetZoom(1.0f);
    view->GZOnKeyDown(0x27, 0);
    CHECK(view->OriginX() == 3);
    view->GZOnKeyDown(0x28, 0);
    CHECK(view->OriginY() == 1);
    view->GZOnKeyDown(0x25, 0);
    CHECK(view->OriginX() == 0);
    view->GZOnKeyDown(0x26, 0);
    CHECK(view->OriginY() == 0);
    view->GZOnKeyDown(0x6b, 0);
    CHECK(view->Zoom() == 2.0f);
    view->GZOnKeyDown(0x6d, 0);
    CHECK(view->Zoom() == 1.0f);
    view->EnableZooming(false);
    view->GZOnKeyDown(0x6b, 0);
    CHECK(view->Zoom() == 1.0f);
    view->ZoomIn(2.0f);   // kept quirk: the methods themselves do not check the flag
    CHECK(view->Zoom() == 2.0f);
    view->EnableZooming(true);
    view->SetZoom(1.0f);
    view->EnableScrolling(false);
    view->GZOnKeyDown(0x27, 0);
    CHECK(view->OriginX() == 0);
    view->EnableScrolling(true);
    CHECK(view->GZOnKeyUp(0x27, 0));

    // ---- centre-on refusal when the image is wider but shorter than the window (kept quirk) ----
    auto* wide = NewImage(100, 20, kImageColor);
    view->SetBMP(wide);
    CHECK(!view->CenterOnImagePixel(10, 10));
    view->SetBMP(image);

    // ---- the right-button drag message: capture, anchor, throttle, re-post ----
    g_now = 1000;
    view->SetMinMsPerScroll(20);
    mgr.posted.clear();
    CHECK(view->GZOnMouseDownR(30, 20, 0));
    CHECK(mgr.capture == view && view->AnchorX() == 30 && view->AnchorY() == 20);
    CHECK(mgr.posted.size() == 1 && mgr.posted[0].type == kWinBMPViewScrollMsg);
    view->SetOrigin(0, 0);
    view->SetLastScrollTime(0);
    mgr.cursorX = 50;      // 30 + 8 < 50: one step right
    mgr.cursorY = 20;
    cGZMessage drag;
    drag.type = kWinBMPViewScrollMsg;
    CHECK(view->DoMessage(drag));
    CHECK(view->OriginX() == 3 && view->OriginY() == 0);
    CHECK(view->LastScrollTime() == 1000);
    CHECK(mgr.posted.size() == 2);   // re-posted while capturing
    // the same millisecond is throttled
    CHECK(view->DoMessage(drag));
    CHECK(view->OriginX() == 3 && view->LastScrollTime() == 1000);
    CHECK(mgr.posted.size() == 3);
    // the dead zone: exactly 8 pixels away does not scroll
    g_now = 1100;
    mgr.cursorX = 38;
    CHECK(view->DoMessage(drag));
    CHECK(view->OriginX() == 3 && view->LastScrollTime() == 1100);
    // and a diagonal move scrolls both axes
    g_now = 1200;
    mgr.cursorX = 0;
    mgr.cursorY = 0;
    CHECK(view->DoMessage(drag));
    CHECK(view->OriginX() == 0 && view->OriginY() == 0);   // clamped at the image edge
    CHECK(view->GZOnMouseUpR(40, 24, 0));
    CHECK(mgr.capture == nullptr && view->AnchorX() == 40 && view->AnchorY() == 24);
    const std::size_t posted = mgr.posted.size();
    CHECK(view->DoMessage(drag));
    CHECK(mgr.posted.size() == posted);   // no capture: no re-post

    // ---- message routing: mouse messages go to cGZWin, the rest to cGZWinBMP ----
    view->SetOrigin(0, 0);
    cGZMessage down;
    down.type = kGZMsgMouseDownL;
    CHECK(view->DoMessage(down));
    view->SetBMP(image);   // reset the origin
    view->SetOrigin(0, 0);
    cGZMessage command;
    command.type = kGZMsgCommand;
    CHECK(!view->DoMessage(command));   // cGZWinBMP: flag 1 clear, no parent

    // ---- painting through a real GZ buffer ----
    auto* drawBuffer = new cGZBuffer;
    drawBuffer->AddRef();
    drawBuffer->Init(64, 32, {kBufferColorARGB8888, 32});
    drawBuffer->Fill(0, cRZRect{0, 0, 64, 32}, nullptr);
    host.drawBuffer = drawBuffer;
    SetWinBMPViewHost(&host);
    auto* small = NewImage(10, 10, kImageColor);
    view->SetBMP(small);
    view->SetZoom(1.0f);
    view->SetOrigin(0, 0);
    CHECK(SameRect(view->ImageDest(), 0, 0, 10, 10));
    // flag 2 (set by default) skips the fill, the 1:1 blit lands at the window corner
    CHECK(view->GZPaint());
    CHECK(drawBuffer->GetPixel(0, 0) == kImageColor && drawBuffer->GetPixel(9, 9) == kImageColor);
    CHECK(drawBuffer->GetPixel(10, 10) == 0);
    // with the flag cleared the background is filled first
    view->SetWinBMPFlag(gz::kWinBMPNoFill, false);
    CHECK(view->GZPaint());
    CHECK(drawBuffer->GetPixel(0, 0) == kImageColor && drawBuffer->GetPixel(40, 20) == kFillColor);
    // zoomed: the stretch path fills the destination rectangle (nearest neighbour, 19 -> image 9)
    view->SetZoom(2.0f);
    CHECK(SameRect(view->ImageDest(), 0, 0, 20, 20));
    CHECK(view->GZPaint());
    CHECK(drawBuffer->GetPixel(19, 19) == kImageColor);
    CHECK(drawBuffer->GetPixel(20, 20) == kFillColor);
    // the destination rectangle is offset by the draw area's corner
    view->SetArea(10, 20, 74, 52);
    view->SetZoom(1.0f);
    view->SetOrigin(0, 0);
    drawBuffer->Fill(0, cRZRect{0, 0, 64, 32}, nullptr);
    CHECK(view->GZPaint());
    CHECK(drawBuffer->GetPixel(10, 20) == kImageColor && drawBuffer->GetPixel(19, 29) == kImageColor);
    CHECK(drawBuffer->GetPixel(9, 19) == 0);
    // no draw buffer at all: painting is a no-op
    host.drawBuffer = nullptr;
    CHECK(view->GZPaint());

    // ---- Init creates the Gimex factory once, Shutdown releases it ----
    host.factoryRequests = 0;
    view->SetArea(0, 0, 64, 32);
    CHECK(view->Init());
    CHECK(host.factoryRequests == 1);
    CHECK(view->Shutdown());
    CHECK(view->Init());
    CHECK(host.factoryRequests == 2);   // the first one was released by Shutdown

    // ---- loading an image through the factory: 8-bit palette expanded to ARGB8888 ----
    const char* imagePath = "/tmp/sc4_winbmpview_test_image.bmp";
    std::FILE* file = std::fopen(imagePath, "wb");
    CHECK(file != nullptr);
    if (file) { std::fputs("not really a bitmap", file); std::fclose(file); }
    cRZString imageName(imagePath);
    view->SetShouldFitImageInView(false);
    CHECK(view->SetBMPFromFile(imageName));
    CHECK(view->GetImageWidth() == 4 && view->GetImageHeight() == 4);
    cIGZBuffer* loaded = nullptr;
    view->GetBMP(&loaded);   // borrowed, not AddRef'd (kept quirk)
    CHECK(loaded != nullptr);
    if (loaded) {
        CHECK(loaded->GetPixel(0, 0) == 0x665544u);   // palette entry 1: b 0x44, g 0x55, r 0x66
        CHECK(loaded->GetPixel(1, 0) == 0x332211u);   // palette entry 0: b 0x11, g 0x22, r 0x33
    }
    // a path that cannot be opened clears the view
    cRZString missing("/tmp/sc4_winbmpview_no_such_file.bmp");
    CHECK(!view->SetBMPFromFile(missing));
    loaded = nullptr;
    view->GetBMP(&loaded);
    CHECK(loaded == nullptr);

    // ---- fit to view: the downscale is a host seam (not recovered) ----
    host.readerWidth = 128;
    host.readerHeight = 64;
    host.scaled = NewImage(64, 32, kImageColor);
    view->SetShouldFitImageInView(true);
    CHECK(view->SetBMPFromFile(imageName));
    CHECK(host.scaleRequests == 1);
    CHECK(view->GetImageWidth() == 64 && view->GetImageHeight() == 32);   // the scaled buffer
    view->SetShouldFitImageInView(false);

    // ---- WriteBMPToFile: extension, the caller's string, and the remove-original quirk ----
    auto* outImage = NewImage(8, 8, kImageColor);
    const char* outPath = "/tmp/sc4_winbmpview_out";
    std::FILE* existing = std::fopen(outPath, "wb");
    CHECK(existing != nullptr);
    if (existing) std::fclose(existing);
    CHECK(cRZFile::FileExists(cRZString(outPath)));
    cRZString outName(outPath);
    // no Gimex writer exists in the port, so the save itself fails, but the name handling runs
    CHECK(!cWinBMPView::WriteBMPToFile(outImage, 1, outName));
    CHECK(outName.str() == std::string(outPath) + ".png");
    CHECK(!cRZFile::FileExists(cRZString(outPath)));            // kept quirk: the original name is removed
    CHECK(cRZFile::FileExists(outName));                        // the save target was created
    cRZFile::Remove(outName);
    // no extension for an unknown format
    cRZString plain("/tmp/sc4_winbmpview_plain");
    CHECK(!cWinBMPView::WriteBMPToFile(outImage, 5, plain));
    CHECK(plain.str() == "/tmp/sc4_winbmpview_plain");
    cRZFile::Remove(plain);
    // 0 means .jpg
    cRZString jpeg("/tmp/sc4_winbmpview_jpeg");
    CHECK(!cWinBMPView::WriteBMPToFile(outImage, 0, jpeg));
    CHECK(jpeg.str() == "/tmp/sc4_winbmpview_jpeg.jpg");
    cRZFile::Remove(jpeg);
    outImage->Release();

    std::remove(imagePath);
    view->Release();
    drawBuffer->Release();
    image->Release();
    small->Release();
    wide->Release();
    SetWinBMPViewHost(nullptr);
    std::printf(failures ? "FAILED (%d)\n" : "sc4_winbmpview ok\n", failures);
    return failures != 0;
}
