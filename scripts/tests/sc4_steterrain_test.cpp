// cSTETerrain part 2: COM identity, the message posts, the display/accumulation state, the vertex
// editing helpers, the mark/rect/span helpers, the temporary water-level copies, the brush tuning
// queries, the lighting row walk, the edge-altitude subfiles and the stream Read/Write wrapper.
// The engine services (message server, lighting manager, the view) are fakes here.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZPersistDBSegment.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "sc4/DBSegment.h"
#include "sc4/cSTETerrain.h"

using namespace sc4;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

namespace {

// ---------------------------------------------------------------------- fake message server
struct PostedMessage {
    std::uint32_t type = 0;
    std::uint64_t data[4] = {0, 0, 0, 0};
};

class FakeMessageServer final : public cIGZMessageServer2 {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { return --refs_; }
    bool MessageSend(cIGZMessage2* message) override { Record(message); return true; }
    bool MessagePost(cIGZMessage2* message, bool) override { Record(message); return true; }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2* message, cIGZMessageTarget2* target) override {
        Record(message);
        lastTarget = target;
        return true;
    }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return static_cast<std::uint32_t>(posts.size()); }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return refs_; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }

    void Record(cIGZMessage2* message) {
        PostedMessage post;
        post.type = message->GetType();
        void* raw = nullptr;
        if (message->QueryInterface(kGZIID_cIGZMessage2Standard, &raw) && raw) {
            auto* standard = static_cast<cIGZMessage2Standard*>(raw);
            post.data[0] = standard->GetData1();
            post.data[1] = standard->GetData2();
            post.data[2] = standard->GetData3();
            post.data[3] = standard->GetData4();
            standard->Release();
        }
        posts.push_back(post);
    }

    std::vector<PostedMessage> posts;
    cIGZMessageTarget2* lastTarget = nullptr;
    std::uint32_t refs_ = 0;
};

// ---------------------------------------------------------------------------- fake terrain host
class FakeHost final : public ITerrainHost {
public:
    struct LightRow {
        std::int32_t z, x, endX, normalStride, colourStride;
        float* normals;
        void* colours;
    };

    cIGZMessageServer2* Messages() override { return &messages; }
    // The model's `new cRZMessage2Standard`; the real class also implements every payload setter.
    cIGZMessage2Standard* NewMessage() override {
        auto* message = new cRZMessage2Standard();
        message->AddRef();
        return message;
    }
    bool WriteGZSerializable(cIGZOStream& stream, const cIGZUnknown* object) override {
        return sc4::SC4WriteGZSerializable(stream, object);
    }
    bool ReadGZSerializable(cIGZIStream& stream, std::uint32_t iid, void** out) override {
        return sc4::SC4ReadGZSerializable(stream, iid, out, false);
    }
    double RandomDoubleRangeUniform(double lo, double hi) override {
        // a deterministic stand-in for the class's seeded sRand
        randomState = randomState * 1664525u + 1013904223u;
        const double unit = static_cast<double>(randomState >> 8) / 16777216.0;
        return lo + (hi - lo) * unit;
    }
    void ApplyTerrainLightingRow(std::int32_t z, std::int32_t x, std::int32_t endX, float* normals,
                                 std::int32_t normalStride, void* colours, std::int32_t colourStride) override {
        rows.push_back({z, x, endX, normalStride, colourStride, normals, colours});
    }

    FakeMessageServer messages;
    std::vector<LightRow> rows;
    std::uint32_t randomState = 12345;
};

class FakeView final : public ITerrainView {
public:
    cIGZSerializable* Serializable() override { return nullptr; }
};

class FakeMessageTarget final : public cIGZMessageTarget2 {
public:
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 0; }
    bool DoMessage(cIGZMessage2*) override { return true; }
};

// The records the edge subfiles live in are opened on a real package file.
cGZDBSegmentPackedFile* Open(const std::string& path, bool write) {
    auto* segment = new cGZDBSegmentPackedFile();
    segment->AddRef();
    segment->Init();
    cRZString p(path.c_str());
    segment->SetPath(p);
    CHECK(segment->Open(true, write));
    return segment;
}

void Close(cGZDBSegmentPackedFile* segment) {
    segment->Close();
    segment->Shutdown();
    segment->Release();
}

}  // namespace

int main() {
    FakeHost host;
    SetTerrainHost(&host);
    CHECK(TerrainHost() == &host);

    // ------------------------------------------------------------------ COM identity + thunks
    {
        auto* object = new cSTETerrain();
        CHECK(object->GetGZCLSID() == 0xe98f9525 && object->GetGZCLSIDThunk() == 0xe98f9525);
        void* out = nullptr;
        CHECK(object->QueryInterface(1, &out) && out == object);
        CHECK(object->QueryInterface(0xab953253, &out) && out == object);
        CHECK(object->QueryInterface(0xc70bd37e, &out) && out == object);
        CHECK(object->QueryInterface(0xe4fda3d4, &out) && out == object);
        CHECK(object->QueryInterface(0x452294aa, &out) && out == object);
        CHECK(!object->QueryInterface(0x12345678, &out));
        // the five successful QueryInterface calls each added a reference
        CHECK(object->AddRef() == 6);
        CHECK(object->AddRefThunk_13fc() == 7 && object->AddRefThunk_140a() == 8);
        CHECK(object->Release() == 7 && object->Release() == 6 && object->Release() == 5);
        CHECK(object->ReleaseThunk_e0da() == 4);
        CHECK(object->Release() == 3 && object->Release() == 2 && object->Release() == 1);
        object->ReleaseThunk_e0fe();                        // last reference: destroys
    }

    // ------------------------------------------------------------------------- the grid fixture
    cSTETerrain t;
    TerrainTuning tuning;
    tuning.seaLevel = 20.0f;
    t.Init(8, 6, tuning);
    CHECK(t.CellCountX() == 8 && t.CellCountZ() == 6);
    CHECK(t.VertexCountX() == 9 && t.VertexCountZ() == 7 && t.VertexCount() == 63);
    for (std::int32_t z = 0; z < 7; ++z)
        for (std::int32_t x = 0; x < 9; ++x) t.Vertices()[t.GetVertIndex(x, z)].altitude = 10.0f * x + 2.0f * z;
    t.RecomputeWaterLevel({0, 0, 8, 6});

    // --------------------------------------------------------------------- display + messages
    CHECK(t.GetWaterSurfaceDisplayState());
    host.messages.posts.clear();
    t.SetWaterSurfaceDisplayState(true);                   // unchanged: nothing is posted
    CHECK(host.messages.posts.empty());
    t.SetWaterSurfaceDisplayState(false);
    CHECK(!t.GetWaterSurfaceDisplayState());
    CHECK(host.messages.posts.size() == 1 && host.messages.posts[0].type == 0x6ab97795 &&
          host.messages.posts[0].data[0] == 0);
    t.SetWaterSurfaceDisplayState(true);
    CHECK(host.messages.posts.size() == 2 && host.messages.posts[1].data[0] == 1);

    CHECK(!t.GetPlumbingInfoDisplayState());
    t.SetPlumbingInfoDisplayState(true);
    CHECK(t.GetPlumbingInfoDisplayState());
    t.SetPlumbingInfoDisplayState(false);

    host.messages.posts.clear();
    t.BeginLongTerrainOperation();
    t.EndLongTerrainOperation();
    CHECK(host.messages.posts.size() == 2 && host.messages.posts[0].type == 0xea3b0af1 &&
          host.messages.posts[1].type == 0x6a3b0afd);

    t.PostMessageA(0xc922eb91, 7, 8, 9, 10);
    CHECK(host.messages.posts.size() == 3 && host.messages.posts[2].type == 0xc922eb91);
    CHECK(host.messages.posts[2].data[0] == 7 && host.messages.posts[2].data[1] == 8 &&
          host.messages.posts[2].data[2] == 9 && host.messages.posts[2].data[3] == 10);

    FakeMessageTarget target;
    t.PostMessageToTarget(0x2a612643, &target);
    CHECK(host.messages.posts.size() == 4 && host.messages.posts[3].type == 0x2a612643);
    CHECK(host.messages.lastTarget == &target);
    // a missing server posts nothing
    SetTerrainHost(nullptr);
    t.BeginLongTerrainOperation();
    CHECK(host.messages.posts.size() == 4);
    SetTerrainHost(&host);

    // -------------------------------------------------------------------- accumulate flag
    CHECK(!t.IsAccumulatingTextureUpdates());
    t.StartAccumulatingTextureUpdates();
    CHECK(t.IsAccumulatingTextureUpdates());
    t.StartAccumulatingTextureUpdates();                   // idempotent
    CHECK(t.IsAccumulatingTextureUpdates());
    t.StopAccumulatingTextureUpdates();
    CHECK(!t.IsAccumulatingTextureUpdates());
    t.StopAccumulatingTextureUpdates();                    // idempotent

    // ---------------------------------------------------------------------------- view
    CHECK(t.GetView() == nullptr);
    FakeView view;
    t.SetView(&view);
    CHECK(t.GetView() == &view);
    t.SetView(nullptr);

    // ---------------------------------------------------------------------- limits and counts
    CHECK(cSTETerrain::MinAllowedHeight() == 5.0f && cSTETerrain::MaxAllowedHeight() == 200.0f);
    CHECK(cSTETerrain::GetDefaultTerrainRaiseLowerAmount() == 20.0f);
    CHECK(t.NumberOfWaterVertices() == 0);                 // +0x108, filled by the water update
    CHECK(t.NumberOfWaterVertices({0, 0, 2, 2}) == 6);     // alt < 20 m: 0,10,20? 20 is not below the sea
    CHECK(t.GetAltitudeCheckSum({0, 0, 8, 6}) == 2898.0f);
    CHECK(t.GetAltitudeCheckSum({0, 0, 9, 6}) == 0.0f);    // x1 out of bounds
    CHECK(t.GetAltitudeCheckSum({0, 4, 2, 3}) == 0.0f);    // z1 < z0
    CHECK(t.AreSeaVertices({0, 0, 1, 1}));
    CHECK(!t.AreSeaVertices({0, 0, 2, 2}));
    CHECK(!t.AreSeaVertices({0, 0, 9, 6}));                // bounds
    CHECK(t.AreSeaVertices({0, 4, 2, 3}));                 // empty range: "all sea"

    // ------------------------------------------------------------------------ vertex editing
    // LowerVertex clamps at the 5 m floor and rewrites the surface-water depth.
    t.Vertices()[t.GetVertIndex(4, 4)].altitude = 100.0f;
    t.WaterDepth()[t.GetVertIndex(4, 4)] = 0.0f;
    t.LowerVertex(static_cast<std::uint32_t>(t.GetVertIndex(4, 4)), 10.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 90.0f && t.GetWaterDepthAtVertex(4, 4) == -70.0f);   // negative (quirk)
    t.LowerVertex(static_cast<std::uint32_t>(t.GetVertIndex(4, 4)), 1000.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 5.0f && t.GetWaterDepthAtVertex(4, 4) == 15.0f);
    // RaiseVertex clamps at 200 m.
    t.RaiseVertex(static_cast<std::uint32_t>(t.GetVertIndex(4, 4)), 1000.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 200.0f && t.GetWaterDepthAtVertex(4, 4) == -180.0f);
    // A locked vertex ignores both.
    t.Vertices()[t.GetVertIndex(4, 4)].flags |= 0x4000;
    t.RaiseVertex(static_cast<std::uint32_t>(t.GetVertIndex(4, 4)), 5.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 200.0f);
    t.Vertices()[t.GetVertIndex(4, 4)].flags &= static_cast<std::uint16_t>(~0x4000);
    // SetAltitudeAtVertex picks the direction and the amount.
    t.SetAltitudeAtVertex(4, 4, 30.0f);
    CHECK(t.GetAltitudeAtVertex(4, 4) == 30.0f && t.GetWaterDepthAtVertex(4, 4) == -10.0f);
    t.SetAltitudeAtVertex(4, 4, 30.0f);                    // no change, no message
    CHECK(t.GetAltitudeAtVertex(4, 4) == 30.0f);

    // ------------------------------------------------------------------ flags and mark helpers
    t.Vertices()[t.GetVertIndex(1, 1)].flags = 0xffff;
    t.Vertices()[t.GetVertIndex(2, 2)].flags = 0xffff;
    t.ClearVertexPropertyFlags(static_cast<std::uint16_t>(0x0003));
    CHECK((t.Vertices()[t.GetVertIndex(1, 1)].flags & 0x0003) == 0);
    t.Vertices()[t.GetVertIndex(1, 1)].flags = 0xffff;
    t.ClearVertexPropertyFlags({1, 1, 1, 1}, static_cast<std::uint16_t>(0x0018));
    CHECK((t.Vertices()[t.GetVertIndex(1, 1)].flags & 0x0018) == 0);
    CHECK((t.Vertices()[t.GetVertIndex(2, 2)].flags & 0x0018) == 0x0018);

    // The cliff flip flag also clears the internal triangulation flip.
    {
        const std::size_t i = static_cast<std::size_t>(t.GetVertIndex(3, 1));
        t.Vertices()[i].flags = static_cast<std::uint16_t>(t.Vertices()[i].flags | 0x0800);
        t.FlipInternalTriangulation(3, 1, true);
        CHECK(t.IsTriangulationFlipped(3, 1));
        t.ClearVertexCliffFlipTriangulationFlags({3, 1, 3, 1});
        CHECK((t.Vertices()[i].flags & 0x0800) == 0);
        CHECK((t.Vertices()[i].flags & 0x1000) == 0);
        CHECK(!t.IsTriangulationFlipped(3, 1));
    }

    // Marks 1 and 3 mark "land above the sea level" (strictly above it).
    {
        const std::int32_t seaLevelIndex = t.GetVertIndex(2, 0);   // altitude exactly 20 m
        const std::int32_t landIndex = t.GetVertIndex(3, 0);       // 30 m
        CHECK(!t.IsLandAndNotMark1(seaLevelIndex) && t.IsLandAndNotMark1(landIndex));
        CHECK(t.IsLandAndNotMark3(landIndex));
        t.Vertices()[static_cast<std::size_t>(landIndex)].flags |= 0x0001;
        CHECK(t.IsVertexWithMark1(landIndex) && !t.IsLandAndNotMark1(landIndex));
        t.Vertices()[static_cast<std::size_t>(landIndex)].flags |= 0x0004;
        CHECK(t.IsVertexWithMark3(landIndex) && !t.IsLandAndNotMark3(landIndex));
        CHECK(!t.IsVertexWithMark1(seaLevelIndex) && !t.IsVertexWithMark3(seaLevelIndex));
        t.Vertices()[static_cast<std::size_t>(landIndex)].flags &= static_cast<std::uint16_t>(~0x0005);
    }

    // ------------------------------------------------------------------------- rect helpers
    {
        TerrainRect r{1, 1, 6, 4};
        t.PadRectangle(r, 2);
        CHECK(r.left == 0 && r.top == 0 && r.right == 8 && r.bottom == 6);
        TerrainRect q{1, 1, 2, 2};
        t.PadRectangle(q, 100);
        CHECK(q.left == 0 && q.top == 0 && q.right == 8 && q.bottom == 6);
        TerrainRect v{};
        t.CreateVertRectAroundVertex(v, 5, 3, 2);
        CHECK(v.left == 3 && v.top == 1 && v.right == 7 && v.bottom == 5);
        t.CreateVertRectAroundVertex(v, 0, 0, 9);          // clamped to the grid
        CHECK(v.left == 0 && v.top == 0 && v.right == 8 && v.bottom == 6);
    }

    // ------------------------------------------------------------- temporary water-level copy
    {
        TerrainRect r{0, 0, 3, 3};
        t.SaveWaterLevelsInTempArray(r);
        const float before = t.WaterDepth()[t.GetVertIndex(2, 2)];
        t.WaterDepth()[t.GetVertIndex(2, 2)] = 123.0f;
        t.RestoreWaterLevelsFromTempArray(r);
        CHECK(t.WaterDepth()[t.GetVertIndex(2, 2)] == before);
        t.WaterDepth()[t.GetVertIndex(7, 6)] = 55.0f;       // outside the rect: untouched
        t.RestoreWaterLevelsFromTempArray(r);
        CHECK(t.WaterDepth()[t.GetVertIndex(7, 6)] == 55.0f);
        t.WaterDepth()[t.GetVertIndex(7, 6)] = 0.0f;
    }

    // ------------------------------------------------------------------------ sediment channel
    {
        t.Sediment().assign(t.VertexCount(), 0.0f);
        t.Sediment()[static_cast<std::size_t>(t.GetVertIndex(1, 2))] = 7.5f;   // cell-centred rounding
        CHECK(t.GetSedimentValue(24.0f, 40.0f) == 7.5f);
        CHECK(t.GetSedimentValue(24.0f, 39.9f) == 7.5f);
        CHECK(t.GetSedimentValue(-1.0f, 0.0f) == 0.0f);    // out of bounds
        CHECK(t.GetSedimentValue(8.0f, 8.0f) == 0.0f);
    }

    // ---------------------------------------------------------------- lowest water neighbour
    {
        cSTETerrain small;
        small.Init(2, 2, tuning);
        small.Vertices()[4].altitude = 100.0f;             // centre
        for (int i = 0; i < 9; ++i)
            if (i != 4) small.Vertices()[static_cast<std::size_t>(i)].altitude = 100.0f;
        small.RecomputeWaterLevel({0, 0, 2, 2});
        CHECK(small.GetNeighborWithLowestWaterLevel(4) == -1);   // flat: no lower neighbour
        small.Vertices()[3].altitude = 90.0f;              // direction 0 (index - 1): 10 m lower
        CHECK(small.GetNeighborWithLowestWaterLevel(4) == 0);
        small.Vertices()[0].altitude = 80.0f;              // direction 1 (index - 4 = 1 - vertsX): 20 m lower
        CHECK(small.GetNeighborWithLowestWaterLevel(4) == 1);
        small.Vertices()[0].altitude = 89.0f;              // 11 < 10 * sqrt(2): the straight one wins
        CHECK(small.GetNeighborWithLowestWaterLevel(4) == 0);
    }

    // ---------------------------------------------------------------------- brush tuning
    {
        auto& brushes = cSTETerrain::BrushDescriptions();
        brushes.clear();
        cSTETerrain brush;
        brush.SetZoomLevel(2);
        CHECK(brush.BrushRadius(3) == 32.0f && brush.GetCostPerBrushApplication(3) == 32);
        TerrainBrushUIElements ui{};
        CHECK(!brush.GetUIElementsForBrush(3, ui));
        CHECK(brushes.empty());

        TerrainBrushDescription description;
        description.radius[2] = 48.0f;
        description.radius[3] = 64.0f;
        description.cost[2] = 12.9f;
        for (int i = 0; i < 10; ++i) description.ui.values[i] = static_cast<std::uint32_t>(100 + i);
        brushes[3] = description;
        CHECK(brush.BrushRadius(3) == 48.0f);
        CHECK(brush.GetCostPerBrushApplication(3) == 12);           // truncated to a whole number
        CHECK(brush.GetUIElementsForBrush(3, ui) && ui.values[0] == 100 && ui.values[9] == 109);
        CHECK(brush.BrushRadius(4) == 32.0f);                       // unknown brush: the defaults
        cSTETerrain::ReleaseBrushDescriptions();
        CHECK(brushes.empty());
        CHECK(!brush.GetUIElementsForBrush(3, ui));
    }
    {
        cSTETerrain brush;
        std::uint32_t exemplar = 0;
        CHECK(brush.GetBrushExemplarID(0, exemplar) && exemplar == 0xea312ef9u);
        CHECK(brush.GetBrushExemplarID(6, exemplar) && exemplar == 0x2a31304eu);
        CHECK(!brush.GetBrushExemplarID(7, exemplar) && exemplar == 0u);
        // eight phases in [0, 2*pi)
        brush.InitBrushParameters();
        for (int i = 0; i < 8; ++i) CHECK(brush.BrushPhase(i) >= 0.0f && brush.BrushPhase(i) < 6.2831856f);
    }

    // --------------------------------------------------------------------------- lighting
    {
        host.rows.clear();
        t.DoLighting({2, 1, 5, 3});
        CHECK(host.rows.size() == 3);
        for (std::size_t i = 0; i < host.rows.size(); ++i) {
            const auto& row = host.rows[i];
            const std::size_t index = static_cast<std::size_t>(t.GetVertIndex(2, 1 + static_cast<int>(i)));
            CHECK(row.z == 1 + static_cast<std::int32_t>(i) && row.x == 2);
            CHECK(row.endX == (1 - 2) + 5);
            CHECK(row.normalStride == 0x24 && row.colourStride == 0x24);
            CHECK(row.normals == &t.Vertices()[index].normal[0]);
            CHECK(row.colours == t.Vertices()[index].surface);
        }
    }

    // ---------------------------------------------------------------- surface texture layers
    {
        const std::size_t i = static_cast<std::size_t>(t.GetVertIndex(2, 2));
        for (int k = 0; k < 0x12; ++k) t.Vertices()[i].surface[k] = static_cast<std::uint8_t>(k + 1);
        t.ClearSurfaceTextures({2, 2, 2, 2});
        CHECK(t.Vertices()[i].surface[6] == 0 && t.Vertices()[i].surface[0xa] == 0 &&
              t.Vertices()[i].surface[0xe] == 0);
        CHECK(t.Vertices()[i].surface[8] == 9 && t.Vertices()[i].surface[0xc] == 13);   // weights kept
    }

    // ---------------------------------------------------------------- marked-region bounds
    {
        for (std::size_t i = 0; i < t.Vertices().size(); ++i) t.Vertices()[i].flags = 0;
        t.Vertices()[static_cast<std::size_t>(t.GetVertIndex(2, 1))].flags = 0x0004;
        t.Vertices()[static_cast<std::size_t>(t.GetVertIndex(5, 4))].flags = 0x0004;
        TerrainRect bounds{};
        t.ComputeBoundsOfMarkedRegion(bounds, 0x0004);
        CHECK(bounds.left == 2 && bounds.top == 1 && bounds.right == 5 && bounds.bottom == 4);
        t.ComputeBoundsOfMarkedRegion(bounds, 0x0100);     // nothing marked
        CHECK(bounds.left == 0x7fffffff && bounds.right == -0x7fffffff - 1);
    }

    // -------------------------------------------------------- rotated rectangle + cell spans
    {
        TerrainPoint quad[4];
        t.ComputeRectAroundPt(quad, 32.0f, 24.0f, 64.0f, 48.0f, 0.0f);
        CHECK(Near(quad[0].x, 0.0f) && Near(quad[0].z, 0.0f));
        CHECK(Near(quad[1].x, 0.0f) && Near(quad[1].z, 48.0f));
        CHECK(Near(quad[2].x, 64.0f) && Near(quad[2].z, 48.0f));
        CHECK(Near(quad[3].x, 64.0f) && Near(quad[3].z, 0.0f));
        t.ComputeRectAroundPt(quad, 0.0f, 0.0f, 32.0f, 32.0f, 1.5707964f);   // 90 degrees
        CHECK(Near(quad[0].x, 16.0f) && Near(quad[0].z, -16.0f));
        CHECK(Near(quad[2].x, -16.0f) && Near(quad[2].z, 16.0f));

        t.ComputeRectAroundPt(quad, 32.0f, 24.0f, 64.0f, 48.0f, 0.0f);
        int spanMin[260];
        int spanMax[260];
        int firstRow = 0, lastRow = 0;
        t.ComputeSpansForRectangle(quad, spanMin, spanMax, firstRow, lastRow);
        CHECK(firstRow == 0 && lastRow == 3);
        // Rows 0..2 hold the quad's two vertical edges; each span is padded one cell on the right.
        for (int row = 0; row <= 2; ++row) CHECK(spanMin[row] == 0 && spanMax[row] == 5);
        // (sic) row 3 is the far boundary: no edge is half-open across it, so the minimum stays at
        // INT_MAX and the empty maximum becomes cellsX.
        CHECK(spanMin[3] == 0x7fffffff && spanMax[3] == 8);
        // every covered vertex is sea (the fixture's plane is below 20 m only up to x = 1):
        CHECK(t.ComputeBooleanFnForRect(quad, &cSTETerrain::IsSeaVertex, 0.0f));
        CHECK(!t.ComputeBooleanFnForRect(quad, &cSTETerrain::IsSeaVertex, 1.0f));
        CHECK(t.ComputeBooleanFnForRect(quad, &cSTETerrain::IsLandVertex, 0.5f));
    }

    // ------------------------------------------------------------------ edge altitude subfiles
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "sc4_steterrain_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "city.dat").string();
    {
        auto* segment = Open(path, true);
        // Save writes the four edge subfiles itself (0x10073f17a loops over them).
        CHECK(t.Save(segment));
        for (int edge = 0; edge < 4; ++edge) CHECK(t.SaveEdgeAltitudes(edge, segment));
        // SaveObliterated clears every flag except 0x18 (its mask argument is inverted by the
        // original: flags &= ~0xffe7 == flags & 0x18) before it saves.
        const std::size_t obliterated = static_cast<std::size_t>(t.GetVertIndex(1, 1));
        t.Vertices()[obliterated].flags = 0xffff;
        t.SaveObliterated(segment);
        CHECK(t.Vertices()[obliterated].flags == 0x0018);
        Close(segment);

        segment = Open(path, false);
        std::vector<float> edge;
        CHECK(t.LoadEdgeAltitudes(0, segment, edge) && edge.size() == 7);   // west column, x = 0
        CHECK(edge[0] == 0.0f && edge[1] == 2.0f && edge[6] == 12.0f);
        CHECK(t.LoadEdgeAltitudes(1, segment, edge) && edge.size() == 9);   // north row, z = 0
        CHECK(edge[0] == 0.0f && edge[8] == 80.0f);
        CHECK(t.LoadEdgeAltitudes(2, segment, edge) && edge.size() == 7);   // east column, x = 8
        CHECK(edge[0] == 80.0f && edge[6] == 92.0f);
        CHECK(t.LoadEdgeAltitudes(3, segment, edge) && edge.size() == 9);   // south row, z = 6
        CHECK(edge[0] == 12.0f && edge[8] == 92.0f);
        cSTETerrain loaded;
        loaded.LoadObliterated(segment);
        CHECK(loaded.LastLoadSucceeded() && loaded.VertexCountX() == 9 && loaded.VertexCountZ() == 7);
        Close(segment);
    }

    // ---------------------------------------------------------------------- stream Read/Write
    const std::string streamPath = (dir / "stream.dat").string();
    {
        auto* segment = new cSC4DBSegment();
        segment->AddRef();
        segment->Init();
        cRZString p(streamPath.c_str());
        segment->SetPath(p);
        CHECK(segment->Open(true, true));
        cISC4DBSegmentOStream* out = nullptr;
        CHECK(segment->OpenOStream({0xaaaabbbb, 0x11112222, 1}, &out, true) && out != nullptr);
        t.Write(*out);
        CHECK(segment->CloseOStream(out));
        out->Release();
        CHECK(segment->Close());
        segment->Release();                                 // the destructor calls Close() again

        auto* reopen = new cSC4DBSegment();
        reopen->AddRef();
        reopen->Init();
        cRZString p2(streamPath.c_str());
        reopen->SetPath(p2);
        CHECK(reopen->Open(true, false));
        cISC4DBSegmentIStream* in = nullptr;
        CHECK(reopen->OpenIStream({0xaaaabbbb, 0x11112222, 1}, &in) && in != nullptr);
        cSTETerrain back;
        if (in) {
            CHECK(back.Read(*in));
            CHECK(reopen->CloseIStream(in));
            in->Release();
        }
        CHECK(reopen->Close());
        reopen->Release();
        CHECK(back.LastLoadSucceeded());
        CHECK(back.VertexCountX() == 9 && back.VertexCountZ() == 7);
        CHECK(back.GetAltitudeAtVertex(8, 6) == t.GetAltitudeAtVertex(8, 6));
    }
    std::filesystem::remove_all(dir);
    SetTerrainHost(nullptr);

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::puts("sc4_steterrain ok");
    return 0;
}
