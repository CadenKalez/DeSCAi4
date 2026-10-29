// Tests s3d::FshFile (the FSH texture container reader, cS3DShapeFile's job): container validation, the directory,
// frame headers (code, size, mip count, centre) and pixel decoding for the uncompressed and DXT formats, on
// containers built in memory.
#include <cstdio>
#include <cstring>
#include <vector>

#include "s3d/FSH.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

namespace {
template <class T> void Put(std::vector<std::uint8_t>& v, std::size_t at, T value) {
    if (v.size() < at + sizeof(T)) v.resize(at + sizeof(T));
    std::memcpy(&v[at], &value, sizeof(T));
}
// One-frame (or n-frame) SHPI container: header, directory, then each frame header + pixels.
std::vector<std::uint8_t> Container(const std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>>& frames,
                                    std::int16_t w, std::int16_t h, std::uint32_t misc) {
    std::vector<std::uint8_t> v(16 + frames.size() * 8, 0);
    std::memcpy(v.data(), "SHPI", 4);
    Put<std::uint32_t>(v, 8, std::uint32_t(frames.size()));
    std::memcpy(&v[12], "G264", 4);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const std::size_t off = v.size();
        std::memcpy(&v[16 + i * 8], "fr00", 4);
        v[16 + i * 8 + 3] = std::uint8_t('0' + i);
        Put<std::uint32_t>(v, 16 + i * 8 + 4, std::uint32_t(off));
        v.resize(off + 16, 0);
        v[off] = frames[i].first;
        Put<std::int16_t>(v, off + 4, w);
        Put<std::int16_t>(v, off + 6, h);
        Put<std::uint32_t>(v, off + 12, misc);
        v.insert(v.end(), frames[i].second.begin(), frames[i].second.end());
    }
    Put<std::uint32_t>(v, 4, std::uint32_t(v.size()));
    return v;
}
}  // namespace

int main() {
    using namespace s3d;
    // bytes per pixel (x2) per frame code
    CHECK(PixelWidthMul2(0x60) == 1 && PixelWidthMul2(0x61) == 2 && PixelWidthMul2(0x62) == 2 && PixelWidthMul2(0x7b) == 2);
    CHECK(PixelWidthMul2(0x78) == 4 && PixelWidthMul2(0x7e) == 4 && PixelWidthMul2(0x6d) == 4);
    CHECK(PixelWidthMul2(0x7f) == 6 && PixelWidthMul2(0x7d) == 8 && PixelWidthMul2(0x12) == 0);

    // container validation
    FshFile bad;
    std::vector<std::uint8_t> junk(32, 0);
    CHECK(!bad.Open(junk.data(), junk.size()));                       // no SHPI magic
    auto tooShort = Container({{0x7d, std::vector<std::uint8_t>(16, 0)}}, 2, 2, 0);
    Put<std::uint32_t>(tooShort, 4, std::uint32_t(tooShort.size() + 100));
    CHECK(!bad.Open(tooShort.data(), tooShort.size()));               // declared size beyond the data
    auto noFrames = Container({}, 1, 1, 0);
    CHECK(!bad.Open(noFrames.data(), noFrames.size()));               // an empty directory opens nothing

    // ARGB8888 (stored B,G,R,A) with a mip count and a negative centre
    std::vector<std::uint8_t> argb = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160};
    const std::uint32_t misc = (2u << 28) | 0xffeu;                   // 3 levels, centre x = -2
    auto c1 = Container({{0x7d, argb}}, 2, 2, misc);
    FshFile f;
    CHECK(f.Open(c1.data(), c1.size()) && f.FrameCount() == 1);
    FshFrameInfo info;
    CHECK(f.GetFrame(0, info) && info.code == 0x7d && info.width == 2 && info.height == 2);
    CHECK(info.mipCount == 2 && info.centerX == -2 && std::memcmp(&info.tag, "fr00", 4) == 0);
    CHECK(!f.GetFrame(1, info));
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    CHECK(f.DecodeRgba8(0, rgba, w, h) && w == 2 && h == 2 && rgba.size() == 16);
    CHECK(rgba[0] == 30 && rgba[1] == 20 && rgba[2] == 10 && rgba[3] == 40);   // B,G,R,A -> R,G,B,A
    CHECK(rgba[12] == 150 && rgba[15] == 160);

    // several frames: RGB888, RGB565, ARGB1555, ARGB4444, an unsupported code and data cut off by the end of the file
    std::vector<std::uint8_t> rgb = {1, 2, 3, 4, 5, 6};
    std::vector<std::uint8_t> p565 = {0x00, 0xf8, 0xe0, 0x07};        // pure red, pure green
    std::vector<std::uint8_t> p1555 = {0x00, 0xfc, 0x1f, 0x00};       // opaque red, transparent blue
    std::vector<std::uint8_t> p4444 = {0x21, 0xf3, 0x00, 0x00};       // a=15 r=3 g=2 b=1
    auto multi = Container({{0x7f, rgb}, {0x78, p565}, {0x7e, p1555}, {0x6d, p4444}, {0x7b, {0, 0}}, {0x7f, {1, 2}}}, 2, 1, 0);
    FshFile m;
    CHECK(m.Open(multi.data(), multi.size()) && m.FrameCount() == 6);
    CHECK(m.DecodeRgba8(0, rgba, w, h) && rgba[0] == 3 && rgba[1] == 2 && rgba[2] == 1 && rgba[3] == 255);
    CHECK(m.DecodeRgba8(1, rgba, w, h) && rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[4] == 0 && rgba[5] == 255);
    CHECK(m.DecodeRgba8(2, rgba, w, h) && rgba[0] == 255 && rgba[3] == 255 && rgba[6] == 255 && rgba[7] == 0);
    CHECK(m.DecodeRgba8(3, rgba, w, h) && rgba[0] == 51 && rgba[1] == 34 && rgba[2] == 17 && rgba[3] == 255);
    CHECK(!m.DecodeRgba8(4, rgba, w, h));                            // paletted frames are not decoded here
    CHECK(!m.DecodeRgba8(5, rgba, w, h));                            // pixel data runs past the end of the file

    // DXT1: one 4x4 block, colour0 = white, colour1 = black, every index 0 -> all white, opaque
    std::vector<std::uint8_t> dxt1 = {0xff, 0xff, 0x00, 0x00, 0, 0, 0, 0};
    auto c2 = Container({{0x60, dxt1}}, 4, 4, 0);
    FshFile d;
    CHECK(d.Open(c2.data(), c2.size()) && d.DecodeRgba8(0, rgba, w, h) && w == 4 && h == 4);
    bool allWhite = true;
    for (int i = 0; i < 16; ++i) allWhite = allWhite && rgba[i * 4] == 255 && rgba[i * 4 + 1] == 255 && rgba[i * 4 + 3] == 255;
    CHECK(allWhite);

    {   // current frame and its palette attachment
        std::vector<std::uint8_t> v(72, 0);
        std::memcpy(v.data(), "SHPI", 4);
        Put<std::uint32_t>(v, 4, 72);
        Put<std::uint32_t>(v, 8, 1);
        std::memcpy(&v[16], "pal0", 4);
        Put<std::uint32_t>(v, 20, 24);                        // frame offset
        Put<std::uint32_t>(v, 24, 0x7b | (32u << 8));         // 8-bit frame, attachment 32 bytes on
        Put<std::int16_t>(v, 28, 4);
        Put<std::int16_t>(v, 30, 4);
        Put<std::uint32_t>(v, 56, 0x29);                      // a palette-type attachment, end of chain
        FshFile pf;
        CHECK(pf.Open(v.data(), v.size()) && pf.GetNumFrames() == 1);
        FshFrameInfo info;
        CHECK(!pf.GetCurrentFrameInfo(info));                 // no frame selected yet
        CHECK(!pf.SetCurrentFrameByIndex(1) && pf.SetCurrentFrameByIndex(0) && pf.SetCurrentFrameByIndex(0));
        CHECK(pf.GetCurrentFrameInfo(info) && info.code == 0x7b && info.width == 4 && info.attachment != nullptr);
        CHECK(info.attachment[0] == 0x29);
        Put<std::uint32_t>(v, 56, 0x30);                      // not a palette type: nothing is found
        FshFile other;
        CHECK(other.Open(v.data(), v.size()) && other.SetCurrentFrameByIndex(0));
        CHECK(other.GetCurrentFrameInfo(info) && info.attachment == nullptr);
    }

    std::printf(g_fail ? "FAILED %d\n" : "s3d_fsh OK\n", g_fail);
    return g_fail ? 1 : 0;
}
