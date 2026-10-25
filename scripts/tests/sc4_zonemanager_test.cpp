#include <cstdio>
#include <string>
#include <unistd.h>

#include "rz/cRZString.h"
#include "sc4/DBSegment.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4/cSC4ZoneManager.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {
class FakeCity : public ISimGridCity {
public:
    std::int32_t CitySizeXInCells() const override { return 64; }
    std::int32_t CitySizeZInCells() const override { return 64; }
    float CellSizeXMeters() const override { return 16.0f; }
    float CellSizeZMeters() const override { return 16.0f; }
};
class Host : public IZoneManagerHost, public ISerializableFactory {
public:
    int CityCellsX() override { return 64; }
    int CityCellsZ() override { return 64; }
    cISCPropertyHolder* GetTunables(const cGZPersistResourceKey&) override { return &tunables; }
    void OnZoneDisplayAlphaChanged() override { ++alphaCalls; }
    cIGZSerializable* CreateSerializable(std::uint32_t classId) override {
        if (classId == 0x49b9e603) {
            auto* g = new cSC4SimGrid<std::int8_t>();
            g->AddRef();
            return static_cast<cIGZSerializable*>(g);
        }
        if (classId == kZoneManagerClassId) {
            auto* zm = new cSC4ZoneManager(this);
            zm->AddRef();
            return static_cast<cIGZSerializable*>(zm);
        }
        return nullptr;
    }
    cSCPropertyHolder tunables;
    int alphaCalls = 0;
};
}  // namespace

int main() {
    FakeCity city;
    SetSimGridCity(&city);
    Host host;
    SetSerializableFactory(&host);
    cSCPropertyHelp::SetPropertyValue(&host.tunables, 0xe94825b2, std::vector<std::uint32_t>{});
    std::vector<std::uint32_t> mins(16), maxs(16);
    for (int i = 0; i < 16; ++i) { mins[i] = 2 + i; maxs[i] = 20 + i; }
    // the tunable arrays are signed ints in the game; the test stores them as uint32 blobs and the
    // manager reads them through the Sint32 view of the same data
    cSCPropertyHelp::SetPropertyValue(&host.tunables, 0xe94825b2, mins);
    cSCPropertyHelp::SetPropertyValue(&host.tunables, 0xe94825b3, maxs);
    cSCPropertyHelp::SetPropertyValue(&host.tunables, 0xe94825b7, std::uint8_t{180});

    cSC4ZoneManager zm(&host);
    CHECK(zm.Init());
    CHECK(zm.GetZoneGrid() && zm.GetZoneGrid()->GetTractCountX() == 64);
    CHECK(zm.GetZoneCount(0) == 64 * 64 && zm.GetZoneCount(1) == 0);
    CHECK(zm.GetUndevelopedTileCount() == 64 * 64 && zm.GetUndevelopedTileCount(5) == 64 * 64);
    CHECK(zm.GetZoneType(3, 4) == 0);
    CHECK(zm.GetMinZoneSize(3) == 5 && zm.GetMaxZoneSize(0) == 20);
    CHECK(zm.GetZoneDisplayAlpha() == 180 && zm.GetDefaultZoneDisplayAlpha() == 180);
    zm.SetZoneDisplayAlpha(100);
    zm.SetZoneDisplayAlpha(100);
    CHECK(zm.GetZoneDisplayAlpha() == 100 && host.alphaCalls == 1);
    CHECK(cSC4ZoneManager::IsRCIZone(1) && cSC4ZoneManager::IsRCIZone(9) && !cSC4ZoneManager::IsRCIZone(10) &&
          !cSC4ZoneManager::IsRCIZone(0) && !cSC4ZoneManager::IsRCIZone(17));
    cGZPersistResourceKey key;
    CHECK(zm.GetTextureForZone(3, key) && key.type == 0x7ab50e44 && key.group == 0x1abe787d);
    CHECK(!zm.GetTextureForZone(16, key));
    zm.GetZoneGrid()->SetTractValue(10, 11, 5);
    CHECK(zm.GetZoneType(10, 11) == 5);
    zm.AdjustZoneCounts(0, 5, 1);
    zm.RecordDevelopmentFailure(5, 3);
    zm.RecordDevelopmentFailure(5, 3);
    CHECK(zm.GetDevelopmentFailureCount(5, 3) == 2 && zm.GetDevelopmentFailureCount(5, 4) == 0 &&
          zm.GetDevelopmentFailureCount(16, 0) == 0);

    // save/load through a database segment
    const std::string path = "/tmp/sc4_zm_test_" + std::to_string(getpid()) + ".sc4";
    const cGZPersistResourceKey rec{0x77, 0x88, 0x99};
    {
        cSC4DBSegment* seg = new cSC4DBSegment();
        seg->AddRef();
        seg->Init();
        cRZString p(path.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(false, true));
        cISC4DBSegmentOStream* os = nullptr;
        CHECK(seg->OpenOStream(rec, &os, true));
        CHECK(zm.Write(*os));
        seg->CloseOStream(os);
        os->Release();
        CHECK(seg->Close());
        seg->Release();
    }
    {
        cSC4DBSegment* seg = new cSC4DBSegment();
        seg->AddRef();
        seg->Init();
        cRZString p(path.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(true, false));
        CHECK(seg->LoadClassObjects(0x48));
        cISC4DBSegmentIStream* is = nullptr;
        CHECK(seg->OpenIStream(rec, &is));
        cSC4ZoneManager loaded(&host);
        CHECK(loaded.Read(*is));
        CHECK(loaded.GetZoneGrid() && loaded.GetZoneType(10, 11) == 5);
        CHECK(loaded.GetZoneCount(5) == 1 && loaded.GetZoneCount(0) == 64 * 64 - 1);
        CHECK(loaded.GetDevelopmentFailureCount(5, 3) == 2);
        CHECK(loaded.GetZoneDisplayAlpha() == 100);
        CHECK(loaded.GetMinZoneSize(2) == 4);
        CHECK(loaded.Init());                       // keeps the loaded counters
        CHECK(loaded.GetZoneCount(5) == 1);
        seg->CloseIStream(is);
        is->Release();
        seg->Close();
        seg->Release();
    }
    unlink(path.c_str());

    // SC4ReadGZSerializable round-trips the manager through its class factory (its cIGZSerializable identity):
    // the writer asks for the serializable (0xe4fda3d4, +0x10) and the reader creates a fresh manager from the
    // class id and asks for the cISC4ZoneManager (0x67652433, +0), then downcasts through that interface.
    {
        const std::string path2 = "/tmp/sc4_zm_srlz_" + std::to_string(getpid()) + ".sc4";
        const cGZPersistResourceKey rec2{0x77, 0x88, 0x9a};
        auto* written = new cSC4ZoneManager(&host);
        written->AddRef();
        CHECK(written->Init());
        written->SetZoneDisplayAlpha(77);
        written->GetZoneGrid()->SetTractValue(3, 3, 7);
        {
            cSC4DBSegment* seg = new cSC4DBSegment();
            seg->AddRef();
            seg->Init();
            cRZString p(path2.c_str());
            seg->SetPath(p);
            CHECK(seg->Open(false, true));
            cISC4DBSegmentOStream* os = nullptr;
            CHECK(seg->OpenOStream(rec2, &os, true));
            CHECK(SC4WriteGZSerializable(*os, static_cast<cIGZUnknown*>(static_cast<cISC4ZoneManager*>(written))));
            seg->CloseOStream(os);
            os->Release();
            CHECK(seg->Close());
            seg->Release();
        }
        {
            cSC4DBSegment* seg = new cSC4DBSegment();
            seg->AddRef();
            seg->Init();
            cRZString p(path2.c_str());
            seg->SetPath(p);
            CHECK(seg->Open(true, false));
            CHECK(seg->LoadClassObjects(0x48));
            cISC4DBSegmentIStream* is = nullptr;
            CHECK(seg->OpenIStream(rec2, &is));
            void* raw = nullptr;
            CHECK(SC4ReadGZSerializable(*is, kGZIID_cISC4ZoneManager, &raw, false));
            CHECK(raw != nullptr);
            auto* loaded = static_cast<cSC4ZoneManager*>(static_cast<cISC4ZoneManager*>(raw));
            CHECK(loaded && loaded->GetGZCLSID() == kZoneManagerClassId);
            CHECK(loaded->GetZoneDisplayAlpha() == 77 && loaded->GetZoneType(3, 3) == 7);
            static_cast<cISC4ZoneManager*>(raw)->Release();
            seg->CloseIStream(is);
            is->Release();
            CHECK(seg->Close());
            seg->Release();
        }
        written->Release();
        unlink(path2.c_str());
    }

    CHECK(zm.Shutdown());
    std::printf(failures ? "FAILED (%d)\n" : "PASS sc4_zonemanager\n", failures);
    return failures != 0;
}
