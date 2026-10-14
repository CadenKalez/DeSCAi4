#include <cmath>
#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "sc4/cSC4SimGrid.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

struct TestCity final : sc4::ISimGridCity {
    std::int32_t CitySizeXInCells() const override { return 64; }
    std::int32_t CitySizeZInCells() const override { return 128; }
    float CellSizeXMeters() const override { return 16.0f; }
    float CellSizeZMeters() const override { return 16.0f; }
};

int main() {
    TestCity city;
    sc4::SetSimGridCity(&city);

    auto* grid = new sc4::cSC4SimGrid<float>();
    grid->AddRef();
    CHECK(grid->GetGZCLSID() == 0x49b9e60a);
    grid->SetTractSize(4);   // 4-cell tracts
    CHECK(grid->GetTractShift() == 2 && grid->GetTractCountX() == 16 && grid->GetTractCountZ() == 32);
    CHECK(grid->GetTractWidthX() == 64.0f && grid->GetOneOverTractWidthX() == 1.0f / 64.0f);
    CHECK(grid->TractIsInBounds(15, 31) && !grid->TractIsInBounds(16, 0) && !grid->TractIsInBounds(-1, 0));

    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 32; ++z) grid->SetTractValue(x, z, static_cast<float>(x * 100 + z));
    CHECK(grid->GetCellValue(9, 13) == 2 * 100 + 3);            // cell (9,13) -> tract (2,3)
    CHECK(grid->GetAverageValueInCellRect(8, 12, 4, 4) == 203);  // inside one tract: its value
    // across 2x1 tracts: values 203 and 303 -> mean of the samples
    float a = grid->GetAverageValueInCellRect(6, 12, 4, 1);     // cells x=6,7 -> tract 1; x=8,9 -> tract 2
    CHECK(std::fabs(a - ((103.0f * 2 + 203.0f * 2) / 4)) < 1e-4f);
    // float samples are truncated to integers before summing
    grid->SetTractValue(0, 0, 0.9f);
    grid->SetTractValue(0, 1, 0.9f);
    CHECK(grid->GetAverageValueInTractRect(0, 0, 1, 2) == 0.0f);

    std::int32_t tx = 0, tz = 0;
    grid->PositionToTract(130.0f, 70.0f, tx, tz);
    CHECK(tx == 2 && tz == 1);
    float px = 0, pz = 0;
    grid->TractCenterToPosition(2, 1, px, pz);
    CHECK(px == 160.0f && pz == 96.0f);
    grid->TractCornerToPosition(2, 1, px, pz);
    CHECK(px == 128.0f && pz == 64.0f);

    // resizing to the same size keeps the data; a new size adds zeroed tracts
    grid->SetTractSize(4);
    CHECK(grid->GetTractValue(3, 4) == 304.0f);
    grid->SetTractSize(8);
    CHECK(grid->GetTractShift() == 3 && grid->GetTractCountX() == 8 && grid->GetTractValue(3, 4) == 304.0f);
    grid->SetTractSize(3);   // non power of two: shift is floor(log2)
    CHECK(grid->GetTractShift() == 1);
    grid->SetTractSize(4);

    // serialization round trip
    grid->SetInstanceID(0xabcd);
    grid->SetTractValues(7.5f);
    grid->Init();
    PortMemoryOStream out;
    CHECK(grid->Write(out));
    auto* copy = new sc4::cSC4SimGrid<float>();
    copy->AddRef();
    PortMemoryIStream in(out.bytes().data(), out.bytes().size());
    CHECK(copy->Read(in));
    CHECK(copy->GetInstanceID() == 0xabcd && copy->GetTractCountX() == 16 && copy->GetTractValue(15, 31) == 7.5f &&
          copy->GetGZCLSID() == 0x49b9e60a && copy->GetOneOverTractWidthZ() == grid->GetOneOverTractWidthZ());
    // a wrong version is rejected
    std::uint8_t bad[4] = {2, 0, 0, 0};
    PortMemoryIStream badIn(bad, sizeof bad);
    CHECK(!copy->Read(badIn));

    // QueryInterface hands out the right subobjects
    void* raw = nullptr;
    CHECK(grid->QueryInterface(sc4::kGZIID_cISC4SimGrid, &raw) && raw == static_cast<void*>(static_cast<sc4::cISC4SimGridBase*>(grid)));
    cIGZUnknown* u = static_cast<cIGZUnknown*>(static_cast<sc4::cISC4SimGridBase*>(grid));
    u->Release();
    CHECK(grid->QueryInterface(sc4::kGZIID_cIGZSerializableSimGrid, &raw) && raw == static_cast<void*>(static_cast<cIGZSerializable*>(grid)));
    static_cast<cIGZSerializable*>(grid)->Release();
    CHECK(!grid->QueryInterface(0x1234, &raw));

    // integer grids: exact averages, correct class ids and element widths
    auto* bytes = new sc4::cSC4SimGrid<std::uint8_t>();
    bytes->AddRef();
    bytes->SetTractSize(2);
    bytes->SetTractValues(10);
    bytes->SetTractValue(0, 0, 20);
    CHECK(bytes->GetAverageValueInTractRect(0, 0, 2, 1) == 15.0f && bytes->GetGZCLSID() == 0x49b9e602);
    PortMemoryOStream out2;
    bytes->Write(out2);
    CHECK(out2.bytes().size() == 2 + 1 + 4 + 4 + 16 + 16 + static_cast<std::size_t>(bytes->GetTractCountX()) * bytes->GetTractCountZ());
    auto* s64 = new sc4::cSC4SimGrid<std::int64_t>();
    CHECK(s64->GetGZCLSID() == 0x49b9e609);
    delete s64;
    grid->Release(); copy->Release(); bytes->Release();

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("sc4_simgrid ok");
    return 0;
}
