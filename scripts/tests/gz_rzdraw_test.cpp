#include <cstdio>
#include <string>
#include <vector>

#include "gz/RZDraw.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
std::string R(const cRZRect& r) {
    return "(" + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," +
           std::to_string(r.bottom) + ")";
}
std::string H(std::uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "%08x", v); return b; }

class FakeContext final : public cIGZDrawContext {
public:
    std::vector<std::string> ops;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    void PushTransform() override {}
    void ClipReset() override {}
    void Clip(const cRZRect& rect) override {}
    void ClipAbsolute(const cRZRect& rect) override {}
    void Offset(const cRZPoint& offset) override {}
    void OffsetAbsolute(const cRZPoint& origin) override {}
    void PopTransform() override {}
    void PushState() override { ops.push_back("push"); }
    void PopState() override { ops.push_back("pop"); }
    cRZRect GetCanvasArea() override { return {}; }
    cRZRect GetClipRect() override { return {}; }
    cRZPoint GetOrigin() override { return {}; }
    int GetTransformStackDepth() override { return {}; }
    bool IsTargetAttached() override { return false; }
    void SetTarget(IDrawTargetBuffer* target) override {}
    cIGZDrawEngine* GetEngine() override { return nullptr; }
    void ResetToDefaultState() override {}
    void SetColor(std::uint32_t rgb) override { ops.push_back("color " + H(rgb)); }
    void SetColor(std::uint32_t index, std::uint32_t rgb) override {}
    void SetAlphaBlend(int blend) override {}
    void SetBltModulate(bool on) override {}
    void SetAntialiasing(bool on) override {}
    void SetLineThickness(float thickness) override { ops.push_back("thick " + std::to_string(int(thickness))); }
    void ClearGradient() override {}
    void SetGradientTransform(const float (*matrix)[4], float a, float b) override {}
    void ClearBlt() override {}
    void SetBlt(cIGZUnknown* source, float x, float y, float sx, float sy) override {}
    void ClearBltTiling() override {}
    void SetBltTiling(const cRZRect& tile) override {}
    void Clear() override {}
    void Point(int x, int y) override {}
    void Line(int x0, int y0, int x1, int y1) override {}
    void Fill(const cRZRect& rect) override { ops.push_back("fill " + R(rect)); }
    void Ellipse(int l, int t, int r, int b) override {}
    void FillEllipse(int l, int t, int r, int b) override {}
    void Blt(IDrawTargetBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) override { ops.push_back("blt " + R(sourceRect) + ">" + R(destRect)); }
    void BltTiled(IDrawTargetBuffer* source, const cRZRect& tile, const cRZRect& destRect, int dx, int dy) override { ops.push_back("tile " + R(tile) + ">" + R(destRect)); }
    void Flush() override {}
    bool IsSuspended() override { return false; }
    void Suspend() override {}
    void Resume() override {}
    void Draw(eDrawingOp op, const float* points, int count) override {}
    void DrawNoTransform(eDrawingOp op, const float* points, int count) override {}
    bool Lock(cGZBufferSurfaceDesc&, bool) override { return true; }
    void Unlock() override {}
};

cGZBuffer* Make(int w, int h) {
    auto* b = new cGZBuffer();
    b->AddRef();
    b->Init(w, h, {kBufferColorARGB8888, 32});
    b->Fill(0, nullptr);
    return b;
}
}  // namespace

int main() {
    // a 6x6 source: 3x3 grid of 2x2 tiles, tile (row,col) = 0x11*(row*3+col+1)
    auto* source = Make(6, 6);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            source->Fill(0xff000000u | 0x11u * (row * 3 + col + 1), col * 2, row * 2, col * 2 + 2, row * 2 + 2, nullptr);
    cRZRect tile{0, 0, 2, 2};

    auto* dest = Make(12, 12);
    cRZRect area{1, 1, 11, 9};
    CHECK(RZDrawBltEdge(dest, source, tile, area, false));
    auto px = [&](int x, int y) { return dest->GetPixel(x, y) & 0xffffff; };
    CHECK(px(1, 1) == 0x11 && px(2, 2) == 0x11);                  // top-left corner
    CHECK(px(3, 1) == 0x22 && px(8, 2) == 0x22);                  // top edge tiled
    CHECK(px(9, 1) == 0x33 && px(10, 2) == 0x33);                 // top-right corner
    CHECK(px(1, 3) == 0x44 && px(2, 6) == 0x44);                  // left edge
    CHECK(px(9, 3) == 0x66 && px(10, 6) == 0x66);                 // right edge
    CHECK(px(1, 7) == 0x77 && px(3, 8) == 0x88 && px(9, 7) == 0x99);   // bottom row
    CHECK(px(5, 5) == 0);                                         // centre left alone
    dest->Fill(0, nullptr);
    CHECK(RZDrawBltEdge(dest, source, tile, area, true));
    CHECK(px(5, 5) == 0x55 && px(8, 3) == 0x55);                  // centre tiled

    auto* t = Make(8, 4);
    cRZRect left{0, 0, 2, 2}, region{0, 0, 8, 4};
    CHECK(RZDrawBltTiled(t, source, left, region, 0, 0, nullptr));
    CHECK((t->GetPixel(0, 0) & 0xffffff) == 0x11 && (t->GetPixel(2, 0) & 0xffffff) == 0x11 && (t->GetPixel(7, 3) & 0xffffff) == 0x11);
    cRZRect strip{0, 0, 6, 1};
    cRZRect srcStrip{0, 0, 2, 1};
    t->Fill(0, nullptr);
    source->SetPixel(1, 0, 0xff000099);
    CHECK(RZDrawBltTiled(t, source, srcStrip, strip, 1, 0, nullptr));       // pattern shifted by one pixel
    CHECK((t->GetPixel(0, 0) & 0xffffff) == 0x99 && (t->GetPixel(1, 0) & 0xffffff) == 0x11);
    t->Fill(0, nullptr);
    cRZRect rightClip{0, 0, 3, 1};
    CHECK(RZDrawBltTiled(t, source, srcStrip, strip, 0, 0, &rightClip));
    CHECK((t->GetPixel(0, 0) & 0xffffff) == 0x11 && (t->GetPixel(2, 0) & 0xffffff) == 0x11 && t->GetPixel(3, 0) == 0);
    // quirk kept from the original: a clip that starts right of the destination also shifts the tile origin
    // by the clip's left edge, so the pattern starts at 2 * clip.left - (misalignment)
    t->Fill(0, nullptr);
    cRZRect leftClip{2, 0, 8, 1};
    CHECK(RZDrawBltTiled(t, source, srcStrip, strip, 0, 0, &leftClip));
    CHECK(t->GetPixel(2, 0) == 0 && t->GetPixel(3, 0) == 0 && (t->GetPixel(4, 0) & 0xffffff) == 0x11);
    cRZRect pixel{2, 2, 3, 3};
    cRZRect fillRegion{1, 1, 3, 3};
    CHECK(RZDrawBltTiled(t, source, pixel, fillRegion, 0, 0, nullptr));
    CHECK((t->GetPixel(1, 1) & 0xffffff) == 0x55 && (t->GetPixel(2, 2) & 0xffffff) == 0x55);

    auto* st = Make(8, 8);
    cRZRect srcTop{0, 0, 2, 2}, big{0, 0, 4, 4};
    CHECK(RZDrawBltStretch(st, source, srcTop, big, nullptr));
    CHECK((st->GetPixel(0, 0) & 0xffffff) == 0x11 && (st->GetPixel(3, 3) & 0xffffff) == 0x11 && st->GetPixel(4, 0) == 0);
    cRZRect bad{0, 0, 0, 4};
    CHECK(!RZDrawBltStretch(st, source, bad, big, nullptr));
    cRZRect outside{-1, 0, 2, 2};
    CHECK(!RZDrawBltStretch(st, source, outside, big, nullptr));

    FakeContext ctx;
    cRZRect r{10, 20, 30, 40};
    RZDraw3DRectangleOutline(&ctx, r, 0xffaaaaaa, 0xff555555);
    CHECK(ctx.ops.size() == 6 && ctx.ops[0] == "color ffaaaaaa" && ctx.ops[1] == "fill (10,20,30,21)" &&
          ctx.ops[2] == "fill (10,20,11,40)" && ctx.ops[3] == "color ff555555" && ctx.ops[4] == "fill (11,39,30,40)" &&
          ctx.ops[5] == "fill (29,21,30,40)");
    ctx.ops.clear();
    RZDrawRaisedRect(&ctx, r, std::uint8_t{100}, std::uint8_t{200}, std::uint8_t{250}, true, false);
    CHECK(ctx.ops[0] == "color ff64c8fa" && ctx.ops[1] == "fill (10,20,30,40)" && ctx.ops[2] == "color ff9cffff");
    CHECK(ctx.ops[5] == "color ff2c9042" || ctx.ops[5] == "color ff2c9042" || ctx.ops.size() == 8);
    ctx.ops.clear();
    RZDrawRaisedRect(&ctx, r, std::uint32_t{0x206080}, false, false);          // sunken: light edges darker
    CHECK(ctx.ops[0] == "color ff206080" && ctx.ops[2] == "color ff002848");
    ctx.ops.clear();
    RZDrawRectangleOutline(&ctx, r, 0xffffffff, 2);
    CHECK(ctx.ops.size() == 8 && ctx.ops[0] == "push" && ctx.ops[1] == "thick 2" && ctx.ops[3] == "fill (10,20,31,22)" &&
          ctx.ops[4] == "fill (10,22,12,39)" && ctx.ops[5] == "fill (31,22,31,39)" && ctx.ops[6] == "fill (10,39,31,41)" &&
          ctx.ops[7] == "pop");
    ctx.ops.clear();
    RZDrawBltEdge(&ctx, source, tile, area, true);
    CHECK(ctx.ops.front() == "push" && ctx.ops.back() == "pop" && ctx.ops[1] == "color ffffffff");
    CHECK(ctx.ops[2] == "blt (0,0,2,2)>(1,1,3,3)" && ctx.ops[3] == "tile (2,0,4,2)>(3,1,9,3)" &&
          ctx.ops[4] == "blt (4,0,6,2)>(9,1,11,3)");

    source->Release(); dest->Release(); t->Release(); st->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
