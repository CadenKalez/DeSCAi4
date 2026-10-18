#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gz/Cursor.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct Platform final : ICursorPlatform {
    std::vector<std::string> log;
    std::vector<std::uint32_t> lastPixels;
    int lastW = 0, lastH = 0, lastStride = 0;
    double lastHotX = -1, lastHotY = -1;
    bool takesHardware = false, graphics = true, bufferFails = false;
    cIGZBuffer* CreateBuffer() override {
        if (bufferFails) return nullptr;
        auto* buffer = new cGZBuffer();
        buffer->AddRef();
        return buffer;
    }
    void SetHardwareCursor(cIGZBuffer* buffer, int x, int y) override {
        log.push_back("hw " + std::string(buffer ? "buf" : "null") + " " + std::to_string(x) + "," + std::to_string(y));
        if (buffer && takesHardware) gMacHardwareCursorSet = true;
    }
    void SetSystemCursor(int index) override { log.push_back("sys " + std::to_string(index)); }
    void SetNamedCursor(const char* name) override { log.push_back(std::string("named ") + name); }
    void AddNamedCursor(const char* name, const std::uint32_t* argb, int w, int h, int stride, double hx, double hy) override {
        log.push_back(std::string("add ") + name);
        lastPixels.assign(argb, argb + w * h);
        lastW = w; lastH = h; lastStride = stride; lastHotX = hx; lastHotY = hy;
    }
    void ShowCursor() override { log.push_back("show"); }
    void HideCursor() override { log.push_back("hide"); }
    bool HasGraphicsSystem() override { return graphics; }
};

void Put16(std::vector<std::uint8_t>& v, std::uint16_t x) { v.push_back(x & 0xff); v.push_back(x >> 8); }
void Put32(std::vector<std::uint8_t>& v, std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(std::uint8_t(x >> (8 * i))); }
void PutBE32(std::vector<std::uint8_t>& v, std::uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back(std::uint8_t(x >> (8 * i))); }

// A 32x32 cursor file. bits = 1, 4, 8 or 32; colorCount goes into the directory entry; the pixel at (x, y) takes
// palette index (x + y) % paletteSize (or the packed value for 32 bit); the AND mask hides the pixel (31, 31) only.
std::vector<std::uint8_t> MakeCursor(int bits, std::uint8_t colorCount, std::uint16_t hotX, std::uint16_t hotY, std::uint16_t type = 2) {
    std::vector<std::uint8_t> f;
    Put16(f, 0); Put16(f, type); Put16(f, 1);
    f.push_back(32); f.push_back(32); f.push_back(colorCount); f.push_back(0);
    Put16(f, hotX); Put16(f, hotY);
    Put32(f, 0); Put32(f, 22);
    std::uint8_t header[0x28] = {};
    header[0] = 0x28;
    header[0x0e] = std::uint8_t(bits);
    f.insert(f.end(), header, header + 0x28);
    const int palette = bits == 32 ? 1 : (colorCount ? colorCount : 1 << bits);
    for (int i = 0; i < palette; ++i) Put32(f, 0x00000000u | (i * 3 << 16) | (i * 2 << 8) | i);   // R, G, B grow with the index
    for (int y = 31; y >= 0; --y) {
        if (bits == 8) for (int x = 0; x < 32; ++x) f.push_back(std::uint8_t((x + y) % 256));
        else if (bits == 4) for (int x = 0; x < 32; x += 2) f.push_back(std::uint8_t(((x + y) % 16) << 4 | ((x + 1 + y) % 16)));
        else if (bits == 1) { std::uint32_t row = 0; for (int x = 0; x < 32; ++x) if (((x + y) & 1)) row |= 1u << (31 - x); PutBE32(f, row); }
        else for (int x = 0; x < 32; ++x) Put32(f, 0x80000000u | (y << 8) | x);
    }
    for (int y = 31; y >= 0; --y) PutBE32(f, y == 31 ? 1u : 0u);       // bit 0 = pixel x = 31
    return f;
}
std::string Write(const std::vector<std::uint8_t>& bytes, const char* name) {
    std::string path = std::string("/tmp/") + name;
    FILE* fp = std::fopen(path.c_str(), "wb");
    std::fwrite(bytes.data(), 1, bytes.size(), fp);
    std::fclose(fp);
    return path;
}
struct FakeCursor final : cIGZCursor {
    int refs = 0, applied = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void SetCursor(bool apply) override { applied += apply ? 1 : 0; }
};
}  // namespace

int main() {
    Platform platform;
    SetCursorPlatform(&platform);

    // ---- system cursors and the manager
    auto* manager = new cRZCursorManager();
    manager->AddRef();
    void* view = nullptr;
    CHECK(manager->QueryInterface(kGZIID_cIGZCursorManager, &view) && view == static_cast<cIGZCursorManager*>(manager));
    manager->Release();
    CHECK(!manager->QueryInterface(5, &view));
    CHECK(manager->Init() && manager->Init());
    for (std::uint32_t id : {1u, 3u, 0xfu, 0x10u, 0x11u}) CHECK(manager->LoadCursor(id) != nullptr);
    CHECK(manager->LoadCursor(2) == nullptr);
    CHECK(manager->GetCursor() == manager->LoadCursor(1));
    CHECK(platform.log.size() == 2 && platform.log[0] == "sys 0" && platform.log[1] == "hw null 0,0");   // applied, then the hardware cursor is cleared
    CHECK(manager->SetCursor(3u) && manager->GetCursor() == manager->LoadCursor(3) && platform.log[platform.log.size() - 2] == "sys 4");
    CHECK(!manager->SetCursor(77u));
    CHECK(manager->PushAutoCursor(0xfu) && platform.log[platform.log.size() - 2] == "sys 7");
    CHECK(manager->PushAutoCursor(0x11u) && platform.log[platform.log.size() - 2] == "sys 10");
    CHECK(!manager->PushAutoCursor(55u));
    CHECK(manager->PopAutoCursor() && manager->GetCursor() == manager->LoadCursor(0xf));
    CHECK(manager->PopAutoCursor() && manager->GetCursor() == manager->LoadCursor(3));
    CHECK(!manager->PopAutoCursor());
    FakeCursor mine;
    CHECK(manager->AddCursor(&mine, 9) && mine.refs == 1 && manager->LoadCursor(9) == &mine);
    FakeCursor other;
    CHECK(manager->AddCursor(&other, 9) && mine.refs == 0 && other.refs == 1 && manager->LoadCursor(9) == &other);   // replaced
    CHECK(manager->RemoveCursor(9) && other.refs == 0 && !manager->RemoveCursor(9));
    manager->SetCursor(&mine);
    CHECK(mine.refs == 1 && mine.applied == 1 && manager->GetCursor() == &mine);
    manager->SetCursor(&mine);
    CHECK(mine.refs == 1 && mine.applied == 2);
    manager->ShowCursor(); manager->HideCursor();
    CHECK(platform.log[platform.log.size() - 2] == "show" && platform.log.back() == "hide");
    CHECK(manager->Shutdown() && manager->GetCursor() == nullptr && manager->LoadCursor(1) == nullptr && mine.refs == 0);
    CHECK(manager->Shutdown());
    manager->Release();

    // ---- system cursor class
    {
        cRZSystemCursorW95 stock(4);
        CHECK(stock.IsLoaded() && !stock.LoadCursorA(cRZString("x")));
        stock.SetCursorHandle(reinterpret_cast<void*>(9));
        platform.log.clear();
        stock.SetCursor(true);
        CHECK(platform.log.size() == 2 && platform.log[0] == "sys 8");
        stock.SetCursor(false);
        CHECK(platform.log.size() == 2);
    }

    // ---- loading .cur files
    {
        auto bytes = MakeCursor(8, 0, 3, 4);
        std::string path = Write(bytes, "sc4_cursor_8.cur");
        cRZCursor cursor;
        CHECK(!cursor.IsLoaded() && !cursor.SystemHasCursorSupport());
        platform.log.clear();
        CHECK(cursor.LoadCursorA(cRZString(path.c_str())));
        CHECK(cursor.IsLoaded() && cursor.SystemHasCursorSupport());
        int hx, hy;
        cursor.GetCursorHotSpot(hx, hy);
        CHECK(hx == 3 && hy == 4);
        CHECK(platform.log.size() == 1 && platform.log[0].find("-SC4") != std::string::npos);
        cIGZBuffer* buffer = cursor.GetCursorBuffer();
        CHECK(buffer && buffer->Width() == 32 && buffer->Height() == 32);
        // pixel (x,y) holds palette[(x + y) % 256] (rows are stored bottom-up); (31,31) is masked
        const std::uint32_t expect = buffer->ConvertRGBValueToNative(3 * 3, 3 * 2, 3);   // index 3: R=9, G=6, B=3
        CHECK(buffer->GetPixel(1, 2) == expect);
        const std::uint32_t key = buffer->ConvertRGBValueToNative(0xff, 0, 0xff);
        CHECK(buffer->GetPixel(31, 31) == key);
        CHECK(platform.lastW == 32 && platform.lastH == 32 && platform.lastHotX == 3 && platform.lastHotY == 4);
        CHECK(platform.lastPixels[31 * 32 + 31] == (key & 0xffffff) && platform.lastPixels[2 * 32 + 1] == expect);
        // applying a named cursor
        platform.log.clear();
        cursor.SetCursor(true);
        CHECK(platform.log.size() == 2 && platform.log[0].rfind("named ", 0) == 0 && platform.log[1] == "hw null 0,0");
        CHECK(gMacHardwareCursorSet);
        // hot spots are clamped, set and read
        cursor.SetCursorHotSpot(7, 8);
        cursor.GetCursorHotSpot(hx, hy);
        CHECK(hx == 7 && hy == 8);
        // reloading gives a new name
        const std::string firstName = platform.log[0];
        CHECK(cursor.LoadCursorA(cRZString(path.c_str())));
    }
    {   // 4 bit and 1 bit images
        std::string p4 = Write(MakeCursor(4, 16, 0, 0), "sc4_cursor_4.cur");
        cRZCursor c4;
        CHECK(c4.LoadCursorA(cRZString(p4.c_str())));
        cIGZBuffer* b = c4.GetCursorBuffer();
        CHECK(b->GetPixel(0, 0) == b->ConvertRGBValueToNative(0, 0, 0));
        CHECK(b->GetPixel(5, 0) == b->ConvertRGBValueToNative(15, 10, 5));
        CHECK(b->GetPixel(17, 3) == b->ConvertRGBValueToNative(12, 8, 4));         // (17+3) % 16 = 4
        std::string p1 = Write(MakeCursor(1, 2, 0, 0), "sc4_cursor_1.cur");
        cRZCursor c1;
        CHECK(c1.LoadCursorA(cRZString(p1.c_str())));
        b = c1.GetCursorBuffer();
        CHECK(b->GetPixel(1, 0) == b->ConvertRGBValueToNative(3, 2, 1));
        CHECK(b->GetPixel(0, 0) == b->ConvertRGBValueToNative(0, 0, 0));
    }
    {   // an icon (type 1) has no hot spot; 32 bit swallows one palette entry
        std::string p = Write(MakeCursor(32, 0, 0, 0, 1), "sc4_cursor_icon.cur");
        cRZCursor icon;
        CHECK(icon.LoadCursorA(cRZString(p.c_str())));
        int hx = 9, hy = 9;
        icon.GetCursorHotSpot(hx, hy);
        CHECK(hx == 0 && hy == 0);
    }
    {   // rejected files
        std::vector<std::uint8_t> bad = MakeCursor(8, 0, 0, 0);
        bad[0] = 1;                                                          // reserved word must be 0
        cRZCursor a;
        CHECK(!a.LoadCursorA(cRZString(Write(bad, "sc4_cursor_bad1.cur").c_str())) && !a.IsLoaded());
        bad = MakeCursor(8, 0, 0, 0);
        bad[2] = 3;                                                          // not an icon or cursor
        cRZCursor b;
        CHECK(!b.LoadCursorA(cRZString(Write(bad, "sc4_cursor_bad2.cur").c_str())));
        bad = MakeCursor(8, 0, 0, 0);
        bad[6] = 16;                                                         // 16 x 32
        cRZCursor c;
        CHECK(!c.LoadCursorA(cRZString(Write(bad, "sc4_cursor_bad3.cur").c_str())));
        bad = MakeCursor(8, 0, 0, 0);
        bad[8] = 5;                                                          // a colour count that is not 0 / 2 / 8 / 16
        cRZCursor d;
        CHECK(!d.LoadCursorA(cRZString(Write(bad, "sc4_cursor_bad4.cur").c_str())));
        cRZCursor missing;
        CHECK(!missing.LoadCursorA(cRZString("/tmp/sc4_cursor_nothing_here.cur")));
        platform.bufferFails = true;
        cRZCursor nobuf;
        CHECK(!nobuf.LoadCursorA(cRZString(Write(MakeCursor(8, 0, 0, 0), "sc4_cursor_ok.cur").c_str())) && !nobuf.IsLoaded());
        platform.bufferFails = false;
    }
    {   // buffer cursors and hardware cursors
        cRZCursor cursor;
        auto* buffer = platform.CreateBuffer();
        buffer->Init(8, 8, cGZBufferColorType{kBufferColorARGB8888, 32});
        cursor.SetCursorBuffer(buffer);
        CHECK(buffer->Release() == 1 && cursor.GetCursorBuffer() == buffer && cursor.IsLoaded());
        cursor.SetCursorHotSpot(2, 3);
        platform.log.clear();
        platform.takesHardware = false;
        cursor.SetCursor(true);
        CHECK(platform.log.size() == 1 && platform.log[0] == "hw buf 2,3");        // not taken: nothing else happens
        platform.log.clear();
        platform.takesHardware = true;
        cursor.SetCursor(true);
        CHECK(platform.log.size() == 2 && platform.log[1] == "hw null 0,0");
        platform.log.clear();
        cursor.SetCursor(false);
        CHECK(platform.log.empty());
        cursor.SetCursorBuffer(nullptr);
        CHECK(!cursor.IsLoaded());
        cursor.ShowCursor(true); cursor.ShowCursor(false);
        CHECK(platform.log.size() == 2 && platform.log[0] == "show" && platform.log[1] == "hide");
        void* v = nullptr;
        CHECK(cursor.QueryInterface(kGZIID_cIGZCursor, &v) && v == static_cast<cIGZCursor*>(&cursor));
    }
    SetCursorPlatform(nullptr);
    std::printf(failures ? "FAILED\n" : "gz_cursor ok\n");
    return failures != 0;
}
