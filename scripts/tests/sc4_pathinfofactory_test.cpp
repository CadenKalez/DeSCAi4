// cSC4PathInfoFactory: instance creation and the SC4PATHS text reader.
#include <cmath>
#include <cstdio>
#include <string>

#include "sc4/PathInfoFactory.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Text final : IPathTextSource {
    std::string data;
    std::size_t pos = 0;
    bool ReadByte(std::uint8_t& b) override { if (pos >= data.size()) return false; b = std::uint8_t(data[pos++]); return true; }
};
bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
}  // namespace

int main() {
    cSC4PathInfoFactory factory;
    // --- instances ---
    void* out = nullptr;
    CHECK(!factory.CreateInstance(0x1234, kGZIID_cISC4PathInfo, &out) && out == nullptr);
    CHECK(!factory.CreateInstance(kPathInfoResourceType, 0x99, &out) && out == nullptr);
    CHECK(factory.CreateInstance(kPathInfoResourceType, kGZIID_cISC4PathInfo, &out) && out);
    cISC4PathInfo* iface = static_cast<cISC4PathInfo*>(out);
    void* persist = nullptr;
    CHECK(iface->QueryInterface(kGZIID_PathInfoPersistResource, &persist) && persist);
    static_cast<cGZPersistResource*>(persist)->Release();
    iface->Release();
    CHECK(factory.CreateInstance(kPathInfoResourceType, kGZIID_PathInfoPersistResource, &out) && out);
    static_cast<cGZPersistResource*>(out)->Release();

    // --- line reader ---
    {
        Text t;
        t.data = "-- comment\nSC4PATHS\r\n42\n\n";
        std::uint32_t remaining = std::uint32_t(t.data.size());
        std::string line;
        CHECK(factory.ReadString(t, line, remaining) && line == "SC4PATHS");   // comment skipped; CR swallows the LF
        std::uint32_t v = 0;
        CHECK(factory.ReadUint32(t, v, remaining) && v == 42);
        CHECK(!factory.ReadUint32(t, v, remaining) && v == 0);   // empty line
        CHECK(!factory.ReadString(t, line, remaining));          // budget used up
        Text u;
        u.data = "1.5,2.5,3.5\n7";
        remaining = std::uint32_t(u.data.size());
        cS3DVector3 p;
        CHECK(factory.ReadVector3(u, p, remaining) && Near(p.x, 1.5f) && Near(p.z, -2.5f) && Near(p.y, 3.5f));   // x, -z, y
        CHECK(remaining == 1);
    }

    // --- a whole file ---
    const std::string text =
        "SC4PATHS\n1.2\n2\n1\n1\n"
        "1\n0\n0\n2\n0\n3\n"            // type 1, lane 0, 0 -> 2, flags 0, 3 points
        "0,8,0\n8,8,0\n16,8,0\n"
        "2\n1\n255\n3\n1\n2\n"          // type 2, lane 1, any -> 3, flags 1, 2 points
        "4,0,1\n4,0,1\n"                // identical points: the second is dropped
        "1\n1\n0\n0\n2\n3,4,5\n";       // stop: kind 1, type 1, lane 0, 0 -> 2
    {
        cSC4PathInfo info;
        Text t;
        t.data = text;
        CHECK(factory.ParsePathText(t, std::uint32_t(t.data.size()), info, true));
        CHECK(info.UseElevation());
        const tPath* a = info.GetPath(MakePathKey(1, 0, 2, 0), nullptr);
        CHECK(a && a->points.size() == 3 && Near(a->length, 16.0f) && Near(a->points[1].x, 8.0f) && Near(a->points[1].z, -8.0f));
        const tPath* b = info.GetPath(MakePathKey(2, 0xff, 3, 1), nullptr);
        CHECK(b && b->points.size() == 1 && b->flags == 1 && b->length == 0.0f);
        // right-hand traffic: kind 1 -> flag bit 29
        cS3DVector3 stop;
        CHECK(info.GetStopPoint(stop, MakePathKey(1, 0, 2, 0) | 0x20000000u) && Near(stop.x, 3) && Near(stop.z, -4) && Near(stop.y, 5));
        // type 2 paths got twins from MakeBidirectionalPaths: the any-edge one swapped (3 -> 255 is stored as entry 3, exit 255)
        CHECK(info.GetPath(MakePathKey(2, 3, 0xff, 1), nullptr) != nullptr);
        CHECK(info.Paths().size() == 4);
    }
    {
        cSC4PathInfo info;
        Text t;
        t.data = text;
        CHECK(factory.ParsePathText(t, std::uint32_t(t.data.size()), info, false));
        cS3DVector3 stop;
        CHECK(info.GetStopPoint(stop, MakePathKey(1, 0, 2, 0) | 0x10000000u));   // left-hand: bit 28
    }
    // --- rejects ---
    auto parse = [&](const std::string& s) {
        cSC4PathInfo info;
        Text t;
        t.data = s;
        return factory.ParsePathText(t, std::uint32_t(s.size()), info, true);
    };
    CHECK(!parse("NOTPATHS\n1.0\n0\n0\n"));
    CHECK(!parse("SC4PATHS\n1.0\n2000\n0\n"));                         // too many paths
    CHECK(!parse("SC4PATHS\n1.0\n1\n0\n9\n0\n0\n0\n1\n0,0,0\n"));      // type 9
    CHECK(!parse("SC4PATHS\n1.0\n1\n0\n1\n0\n7\n0\n1\n0,0,0\n"));      // entry edge 7
    CHECK(!parse("SC4PATHS\n1.0\n1\n0\n1\n0\n0\n0\n0\n"));             // zero points
    CHECK(parse("SC4PATHS\n1.0\n0\n0\n"));                             // empty is fine
    CHECK(parse("-- header\nSC4PATHS\n-- v\n1.0\n0\n1\n"));
    if (failures) { std::printf("sc4_pathinfofactory: %d failure(s)\n", failures); return 1; }
    std::printf("sc4_pathinfofactory OK\n");
    return 0;
}
