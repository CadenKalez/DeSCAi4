// cGZPersistBufferResource: BMP (24/8/4 bit, RLE8) and TGA (raw/RLE, 16/24/32) loading, dithering, file loading, interfaces.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#include "gz/PersistBufferResource.h"
#include "gz/cGZPersistDBSerialRecord.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

class MemoryRecord final : public cGZPersistDBSerialRecord {
public:
    MemoryRecord() : cGZPersistDBSerialRecord(nullptr, true) {}
    std::vector<std::uint8_t> bytes;
    std::size_t position = 0;
    bool GetFieldVoid(void* d, std::uint32_t n) override {
        if (position + n > bytes.size()) return false;
        std::memcpy(d, bytes.data() + position, n);
        position += n;
        return true;
    }
    bool SetFieldVoid(const void* s, std::uint32_t n) override { auto* p = static_cast<const std::uint8_t*>(s); bytes.insert(bytes.end(), p, p + n); return true; }
    std::uint32_t GetSize() override { return static_cast<std::uint32_t>(bytes.size()); }
    bool SetSize(std::uint32_t) override { return true; }
    std::uint32_t GetPosition() override { return static_cast<std::uint32_t>(position); }
    bool SeekAbsolute(std::uint32_t p) override { position = p; return p <= bytes.size(); }
    bool SeekRelative(std::int32_t d) override { position += d; return position <= bytes.size(); }
    void DoPostClose() override {}
};

struct Host : IPersistBufferHost {
    cIGZBuffer* CreateAggregatableBuffer() override { return nullptr; }
    cIGZBuffer* CreateBuffer() override { auto* b = new cGZBuffer(); b->AddRef(); return b; }
    cIGZBuffer* CreateGraphicSystemBuffer() override { return CreateBuffer(); }
    bool ConvertBufferFromFormatToFormat(cIGZBuffer*, std::int32_t) override { return true; }
};

void Put16(std::vector<std::uint8_t>& v, std::uint16_t x) { v.push_back(x & 0xff); v.push_back(x >> 8); }
void Put32(std::vector<std::uint8_t>& v, std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xff); }

// A BMP file image: palette (BGRA entries, may be empty), pixel rows already padded.
std::vector<std::uint8_t> MakeBmp(int w, int h, int bits, const std::vector<std::uint8_t>& palette, const std::vector<std::uint8_t>& pixels,
                                  std::uint32_t compression = 0, bool paletteHeader = true) {
    std::vector<std::uint8_t> v = {'B', 'M'};
    const std::uint32_t offset = 54 + static_cast<std::uint32_t>(palette.size());
    Put32(v, offset + static_cast<std::uint32_t>(pixels.size()));
    Put32(v, 0);
    Put32(v, offset);
    Put32(v, 40); Put32(v, w); Put32(v, h);
    Put16(v, 1); Put16(v, bits);
    Put32(v, compression); Put32(v, compression ? static_cast<std::uint32_t>(pixels.size()) : 0);
    Put32(v, 0); Put32(v, 0);
    Put32(v, paletteHeader ? static_cast<std::uint32_t>(palette.size() / 4) : 0); Put32(v, 0);
    v.insert(v.end(), palette.begin(), palette.end());
    v.insert(v.end(), pixels.begin(), pixels.end());
    return v;
}

cGZPersistBufferResource* Make(Host& h, std::int32_t type, std::uint32_t bits) {
    auto* r = new cGZPersistBufferResource(&h, cGZBufferColorType{type, bits});
    r->AddRef();
    return r;
}

cIGZBuffer* Buf(cGZPersistBufferResource* r) { return static_cast<cIGZPersistBufferResource*>(r)->GetBuffer(); }

}  // namespace

int main() {
    Host host;
    // ---- 24 bit BMP -> ARGB8888 / XRGB with row flip and opaque alpha ----
    {
        // 2x2: bottom row first (BGR): (1,2,3) (4,5,6) / top row (7,8,9) (10,11,12); rows padded to 8 bytes
        std::vector<std::uint8_t> px = {1, 2, 3, 4, 5, 6, 0, 0, 7, 8, 9, 10, 11, 12, 0, 0};
        MemoryRecord rec;
        rec.bytes = MakeBmp(2, 2, 24, {}, px);
        auto* r = Make(host, kBufferColorARGB8888, 32);
        CHECK(r->LoadFromRecord(&rec));
        cIGZBuffer* b = Buf(r);
        CHECK(b && b->Width() == 2 && b->Height() == 2);
        CHECK(b->GetPixel(0, 0) == 0xff090807 && b->GetPixel(1, 0) == 0xff0c0b0a && b->GetPixel(0, 1) == 0xff030201 && b->GetPixel(1, 1) == 0xff060504);
        b->Release();
        r->Release();
        // 16 bit RGB565 target
        MemoryRecord rec2;
        rec2.bytes = MakeBmp(2, 2, 24, {}, px);
        auto* r2 = Make(host, kBufferColorRGB565, 16);
        CHECK(r2->LoadFromRecord(&rec2));
        cIGZBuffer* b2 = Buf(r2);
        CHECK(b2->GetPixel(0, 0) == b2->ConvertRGBValueToNative(9, 8, 7) && b2->GetPixel(1, 1) == b2->ConvertRGBValueToNative(6, 5, 4));
        b2->Release(); r2->Release();
        // 24 bit surface is not a cGZBuffer depth; a 32 bit request with type 1 also works
        MemoryRecord bad;
        bad.bytes = {'X', 'Y'};
        auto* r3 = Make(host, kBufferColorARGB8888, 32);
        CHECK(!r3->LoadFromRecord(&bad));
        r3->Release();
    }
    // ---- 8 bit BMP with a palette -> 16 bit and 8 bit ----
    {
        std::vector<std::uint8_t> pal(1024, 0);
        pal[4] = 0; pal[5] = 0; pal[6] = 255;     // 1: red
        pal[8] = 255; pal[9] = 0; pal[10] = 0;    // 2: blue
        std::vector<std::uint8_t> px = {1, 2, 1, 0,  2, 1, 2, 0};   // bottom row, top row (3 wide, padded to 4)
        MemoryRecord rec;
        rec.bytes = MakeBmp(3, 2, 8, pal, px);
        auto* r = Make(host, kBufferColorRGB565, 16);
        CHECK(r->LoadFromRecord(&rec));
        cIGZBuffer* b = Buf(r);
        CHECK(b->GetPixel(0, 0) == b->ConvertRGBValueToNative(0, 0, 255) && b->GetPixel(1, 0) == b->ConvertRGBValueToNative(255, 0, 0) && b->GetPixel(0, 1) == b->ConvertRGBValueToNative(255, 0, 0));
        b->Release(); r->Release();
        MemoryRecord rec2;
        rec2.bytes = MakeBmp(3, 2, 8, pal, px);
        auto* r2 = Make(host, kBufferColorARGB8888, 32);
        CHECK(r2->LoadFromRecord(&rec2));
        cIGZBuffer* b2 = Buf(r2);
        CHECK(b2->GetPixel(2, 1) == b2->ConvertRGBValueToNative(255, 0, 0));
        b2->Release(); r2->Release();
        // RLE8: a run of 3 x index 1, end of line, a run of 2 x index 2 and an absolute run of {2, 1, 2}? (1 row each)
        std::vector<std::uint8_t> rle = {3, 1, 0, 0, 2, 2, 0, 1};   // row0: 1 1 1 ; row1: 2 2 ; end
        MemoryRecord rec3;
        rec3.bytes = MakeBmp(3, 2, 8, pal, rle, 1);
        auto* r3 = Make(host, kBufferColorARGB8888, 32);
        CHECK(r3->LoadFromRecord(&rec3));
        cIGZBuffer* b3 = Buf(r3);
        // the first decoded row is the bottom one
        CHECK(b3->GetPixel(0, 1) == b3->ConvertRGBValueToNative(255, 0, 0) && b3->GetPixel(2, 1) == b3->ConvertRGBValueToNative(255, 0, 0));
        CHECK(b3->GetPixel(0, 0) == b3->ConvertRGBValueToNative(0, 0, 255) && b3->GetPixel(1, 0) == b3->ConvertRGBValueToNative(0, 0, 255));
        b3->Release(); r3->Release();
        // absolute mode: 00 03 a b c pad
        std::vector<std::uint8_t> rle2 = {0, 3, 1, 2, 1, 0, 0, 1};
        MemoryRecord rec4;
        rec4.bytes = MakeBmp(3, 1, 8, pal, rle2, 1);
        auto* r4 = Make(host, kBufferColorARGB8888, 32);
        CHECK(r4->LoadFromRecord(&rec4));
        cIGZBuffer* b4 = Buf(r4);
        CHECK(b4->GetPixel(1, 0) == b4->ConvertRGBValueToNative(0, 0, 255) && b4->GetPixel(2, 0) == b4->ConvertRGBValueToNative(255, 0, 0));
        b4->Release(); r4->Release();
    }
    // ---- 4 bit BMP -> 16 bit ----
    {
        std::vector<std::uint8_t> pal(1024, 0);
        pal[4 * 3 + 2] = 255;   // 3: red
        pal[4 * 5] = 255;       // 5: blue
        std::vector<std::uint8_t> px = {0x35, 0x30, 0, 0};   // 3 pixels: 3,5,3 (padded to 4 bytes)
        MemoryRecord rec;
        rec.bytes = MakeBmp(3, 1, 4, pal, px);
        auto* r = Make(host, kBufferColorRGB565, 16);
        CHECK(r->LoadFromRecord(&rec));
        cIGZBuffer* b = Buf(r);
        CHECK(b->GetPixel(0, 0) == b->ConvertRGBValueToNative(255, 0, 0) && b->GetPixel(1, 0) == b->ConvertRGBValueToNative(0, 0, 255) &&
              b->GetPixel(2, 0) == b->ConvertRGBValueToNative(255, 0, 0));
        b->Release(); r->Release();
    }
    // ---- TGA ----
    {
        auto tga = [](int w, int h, int depth, int type, std::uint8_t desc, const std::vector<std::uint8_t>& data) {
            std::vector<std::uint8_t> v = {0, 0, static_cast<std::uint8_t>(type)};
            Put16(v, 0); Put16(v, 0); v.push_back(0);
            Put16(v, 0); Put16(v, 0); Put16(v, w); Put16(v, h);
            v.push_back(depth); v.push_back(desc);
            v.insert(v.end(), data.begin(), data.end());
            return v;
        };
        // 2x2 24 bit, bottom-up (descriptor 0): first stored row is the bottom row
        std::vector<std::uint8_t> d24 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        MemoryRecord rec;
        rec.bytes = tga(2, 2, 24, 2, 0, d24);
        auto* r = Make(host, kBufferColorARGB8888, 32);
        CHECK(r->LoadFromRecord(&rec));   // not a bitmap: rewinds and reads a Targa
        cIGZBuffer* b = Buf(r);
        CHECK(b->Width() == 2 && b->GetPixel(0, 1) == 0xff030201 && b->GetPixel(1, 1) == 0xff060504 && b->GetPixel(0, 0) == 0xff090807);
        b->Release(); r->Release();
        // top-down (descriptor bit 5)
        MemoryRecord rec2;
        rec2.bytes = tga(2, 2, 24, 2, 0x20, d24);
        auto* r2 = Make(host, kBufferColorARGB8888, 32);
        CHECK(r2->LoadTGAFromRecord(&rec2));
        cIGZBuffer* b2 = Buf(r2);
        CHECK(b2->GetPixel(0, 0) == 0xff030201 && b2->GetPixel(0, 1) == 0xff090807);
        b2->Release(); r2->Release();
        // 32 bit: alpha is not read (opaque)
        MemoryRecord rec3;
        rec3.bytes = tga(1, 1, 32, 2, 0x20, {1, 2, 3, 0x40});
        auto* r3 = Make(host, kBufferColorARGB8888, 32);
        CHECK(r3->LoadTGAFromRecord(&rec3));
        cIGZBuffer* b3 = Buf(r3);
        CHECK(b3->GetPixel(0, 0) == 0xff030201);
        b3->Release(); r3->Release();
        // 16 bit with an attribute bit -> ARGB4444 keeps alpha
        MemoryRecord rec4;
        rec4.bytes = tga(1, 1, 16, 2, 0x28, {0x1f, 0x80 | 0x7c});   // 0x807f? little endian 0xfc1f
        auto* r4 = Make(host, kBufferColorARGB4444, 16);
        CHECK(r4->LoadTGAFromRecord(&rec4));
        cIGZBuffer* b4 = Buf(r4);
        CHECK((b4->GetPixel(0, 0) >> 12) == 0xf && ((b4->GetPixel(0, 0) >> 8) & 0xf) == 0xf);
        b4->Release(); r4->Release();
        // RLE: a run of 3 pixels and 2 raw pixels in a 5x1 image
        std::vector<std::uint8_t> rle = {0x82, 9, 8, 7,  0x01, 1, 2, 3, 4, 5, 6};
        MemoryRecord rec5;
        rec5.bytes = tga(5, 1, 24, 10, 0x20, rle);
        auto* r5 = Make(host, kBufferColorARGB8888, 32);
        CHECK(r5->LoadTGAFromRecord(&rec5));
        cIGZBuffer* b5 = Buf(r5);
        CHECK(b5->GetPixel(0, 0) == 0xff070809 && b5->GetPixel(2, 0) == 0xff070809 && b5->GetPixel(3, 0) == 0xff030201 && b5->GetPixel(4, 0) == 0xff060504);
        b5->Release(); r5->Release();
        // palette types are refused
        MemoryRecord rec6;
        rec6.bytes = tga(1, 1, 8, 1, 0, {0});
        auto* r6 = Make(host, kBufferColorARGB8888, 32);
        CHECK(!r6->LoadTGAFromRecord(&rec6));
        r6->Release();
        // 32 bit TGA into a type-1 request on a 4444 buffer takes the dither path: flat opaque white stays white
        MemoryRecord rec7;
        std::vector<std::uint8_t> white;
        for (int i = 0; i < 4 * 4; ++i) { white.push_back(255); white.push_back(255); white.push_back(255); white.push_back(255); }
        rec7.bytes = tga(4, 4, 32, 2, 0x20, white);
        struct H4444 : Host {
            cIGZBuffer* CreateBuffer() override {
                auto* b = new cGZBuffer(); b->AddRef(); b->Init(4, 4, {kBufferColorARGB4444, 16}); return b;
            }
        } h4;
        auto* r7 = new cGZPersistBufferResource(&h4, cGZBufferColorType{1, 0});
        r7->AddRef();
        // Init in LoadTGA re-initialises with {1,0} which this buffer rejects (already initialised): the load fails cleanly
        CHECK(!r7->LoadTGAFromRecord(&rec7));
        r7->Release();
    }
    // ---- dithering to ARGB4444 when the display format (type 1) resolves to it ----
    {
        struct Display : IBufferHost {
            bool HasGraphicsSystem() override { return true; }
            cGZBufferColorType DisplayColorType() override { return {kBufferColorARGB4444, 16}; }
            bool QueryTextureBitCount(std::uint32_t& bits) override { bits = 16; return true; }
        } display;
        SetBufferHost(&display);
        const int w = 16, h = 4;
        std::vector<std::uint8_t> px;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { px.push_back(0x25); px.push_back(0x47); px.push_back(0x8b); }
        MemoryRecord rec;
        rec.bytes = MakeBmp(w, h, 24, {}, px);
        auto* r = Make(host, 1, 0);
        CHECK(r->LoadFromRecord(&rec));
        cIGZBuffer* b = Buf(r);
        CHECK(b->GetColorType().type == kBufferColorARGB4444);
        double sr = 0, sg = 0, sb = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const std::uint32_t p = b->GetPixel(x, y);
                CHECK((p >> 12) == 0xf);
                sr += (p >> 8) & 0xf; sg += (p >> 4) & 0xf; sb += p & 0xf;
            }
        // the diffusion keeps the mean within a fraction of a level of the true value (level = value / 16)
        CHECK(std::abs(sr / (w * h) - 0x8b / 16.0) < 0.6 && std::abs(sg / (w * h) - 0x47 / 16.0) < 0.6 && std::abs(sb / (w * h) - 0x25 / 16.0) < 0.6);
        // a plain (non display) request does not dither: exact truncation
        b->Release(); r->Release();
        // 32 bit Targa takes the alpha-aware dither: opaque white stays white, transparent black stays black
        std::vector<std::uint8_t> v = {0, 0, 2};
        Put16(v, 0); Put16(v, 0); v.push_back(0); Put16(v, 0); Put16(v, 0); Put16(v, 4); Put16(v, 1); v.push_back(32); v.push_back(0x20);
        const std::uint8_t px4[16] = {255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0};
        v.insert(v.end(), px4, px4 + 16);
        MemoryRecord t;
        t.bytes = v;
        auto* r2 = Make(host, 1, 0);
        CHECK(r2->LoadTGAFromRecord(&t));
        cIGZBuffer* b2 = Buf(r2);
        CHECK(b2->GetPixel(0, 0) == 0xffff && b2->GetPixel(1, 0) == 0xffff && b2->GetPixel(2, 0) == 0 && b2->GetPixel(3, 0) == 0);
        b2->Release(); r2->Release();
        SetBufferHost(nullptr);
    }
    // ---- BMP file ----
    {
        std::vector<std::uint8_t> px = {1, 2, 3, 4, 5, 6, 0, 0, 7, 8, 9, 10, 11, 12, 0, 0};
        auto bytes = MakeBmp(2, 2, 24, {}, px);
        const char* tmp = std::getenv("TMPDIR");
        std::string path = std::string(tmp ? tmp : "/tmp") + "/sc4_persistbuf_" + std::to_string(getpid()) + ".bmp";
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
        auto* r = Make(host, kBufferColorARGB8888, 32);
        cRZString p(path.c_str());
        CHECK(r->LoadFromFile(p, false, cGZBufferColorType{kBufferColorARGB8888, 32}, false));
        cIGZBuffer* b = Buf(r);
        CHECK(b->GetPixel(0, 0) == 0xff090807 && b->GetPixel(1, 1) == 0xff060504);
        b->Release();
        cRZString none("");
        CHECK(!r->LoadFromFile(none));
        cRZString missing("/definitely/missing.bmp");
        CHECK(!r->LoadFromFile(missing));
        r->Release();
        std::remove(path.c_str());
    }
    // ---- interfaces, set/get buffer, debug tag ----
    {
        auto* r = Make(host, kBufferColorARGB8888, 32);
        void* q = nullptr;
        CHECK(r->QueryInterface(0x06f1568e, &q) && q == static_cast<cIGZPersistBufferResource*>(r));
        r->Release();
        void* ib = nullptr;
        CHECK(r->QueryInterface(kGZIID_cIGZBuffer, &ib) && ib != nullptr);   // forwarded to the buffer
        static_cast<cIGZBuffer*>(ib)->Release();
        CHECK(!r->QueryInterface(0xdeadbeef, &ib));
        cIGZBuffer* fresh = host.CreateBuffer();
        static_cast<cIGZPersistBufferResource*>(r)->SetBuffer(fresh);
        CHECK(Buf(r) == fresh);
        fresh->Release(); fresh->Release();
        cRZString tag;
        CHECK(r->DebugClassTag(tag) && std::strcmp(tag.ToChar(), "cSC3Buffer : ") == 0 && !r->DebugInstanceTag(tag) && !r->DebugTypeTag(tag));
        void* gb = nullptr;
        CHECK(static_cast<cIGZPersistBufferResource*>(r)->GetBuffer(kGZIID_cIGZBuffer, &gb));
        static_cast<cIGZBuffer*>(gb)->Release();
        r->Release();
    }
    if (failures == 0) std::printf("gz_persistbufferresource OK\n");
    return failures == 0 ? 0 : 1;
}
