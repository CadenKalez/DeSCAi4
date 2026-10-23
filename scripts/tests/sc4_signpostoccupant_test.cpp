// cSC4SignpostOccupant: the record, the geometry RebuildModel builds, the two draw passes' vertex streams, the
// picking boxes, the flag / permission gating, the messages (refresh, visibility, blink tick), the transform
// composition and the version-2 record — driven through the class's seams (host, glyph textures, texture bindings,
// the occupant manager) and a recording gd driver for the draw context.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <string>
#include <vector>

#include "render/gd/GDriverBase.h"
#include "rz/PortMemoryStreams.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/DrawContext.h"
#include "sc4/RenderProperties.h"
#include "sc4/SignpostOccupant.h"
#include "sc4/ViewUtilities.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) <= 1e-5f)

using namespace sc4;

namespace {

// ---- fakes --------------------------------------------------------------------------------------------------
struct FakeBinding final : ISignpostTextureBinding {
    std::uint32_t id = 0;
    int releases = 0;
    std::uint32_t TextureId() const override { return id; }
    void Release() override { ++releases; }
};

struct FakeGlyph final : ISignpostGlyphTextureInfo {
    int refs = 1;
    int glyphWidth = 10, glyphHeight = 20;
    float rect[4] = {0.0f, 0.0f, 0.5f, 1.0f};
    FakeBinding binding;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    void GetGlyph(cIGZString&) override {}
    void GetTextureSize(int* widthHeight) override { widthHeight[0] = 64; widthHeight[1] = 64; }
    void GetGlyphSize(int* widthHeight) override { widthHeight[0] = glyphWidth; widthHeight[1] = glyphHeight; }
    void GetGlyphTextureCoordinates(float* out) override { std::memcpy(out, rect, sizeof rect); }
    ISignpostTextureBinding* GetTextureBinding() override { return &binding; }
};

struct FakeViewSet final : ISignpostViewSet {
    int refs = 1;
    bool contains = false;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool ContainsOccupant(cISC4Occupant*) override { return contains; }
};

struct FakeMessages final : cIGZMessageServer2 {
    std::vector<std::pair<cIGZMessageTarget2*, std::uint32_t>> added, removed;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 2; }
    std::uint32_t Release() override { return 1; }
    bool MessageSend(cIGZMessage2*) override { return true; }
    bool MessagePost(cIGZMessage2*, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        added.emplace_back(target, type);
        return true;
    }
    bool RemoveNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        removed.emplace_back(target, type);
        return true;
    }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 1; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

// A tiny occupant manager (the same shape sc4_propmanager_test uses).
class MiniManager final : public cISC4OccupantManager {
public:
    int moves = 0;
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool SetCitySize(float, float) override { return true; }
    void GetOccupantManagerCellSizes(float&, float&) const override {}
    void GetWorldCellCount(int&, int&) const override {}
    void GetOccupantManagerCellCount(int&, int&) const override {}
    void GetOccupantManagerCellBounds(int&, int&, int&, int&) const override {}
    void WorldCellToOccupantManagerCell(int, int, int&, int&) const override {}
    void OccupantManagerCellToWorldCell(int, int, int&, int&) const override {}
    void WorldCellToStandardCityCell(int, int, int&, int&) const override {}
    void StandardCityCellToWorldCell(int, int, int&, int&) const override {}
    void WorldCellToPosition(int, int, float&, float&) const override {}
    void PositionToWorldCell(float, float, int&, int&) const override {}
    void OccupantManagerCellToPosition(int, int, float&, float&) const override {}
    void PositionToOccupantManagerCell(float, float, int&, int&) const override {}
    void StandardCityCellToPosition(int, int, float&, float&) const override {}
    void PositionToStandardCityCell(float, float, int&, int&) const override {}
    bool InsertOccupant(cISC4Occupant* o, std::uint32_t) override { o->AddRef(); items.push_back(o); return true; }
    bool RemoveOccupant(cISC4Occupant* o, bool, std::uint32_t) override {
        for (auto it = items.begin(); it != items.end(); ++it)
            if (*it == o) {
                items.erase(it);
                o->Release();
                return true;
            }
        return false;
    }
    bool RemoveOccupants(int, bool, std::uint32_t) override { return false; }
    bool RemoveOccupants(cISC4OccupantFilter*, bool, std::uint32_t) override { return false; }
    bool MoveOccupant(cISC4Occupant* o, bool) override {
        ++moves;
        moved.push_back(o);
        return true;
    }
    bool IsCellEmpty(int, int) const override { return true; }
    int GetBoundingCells(int, int, int (*)[2], int&) override { return 0; }
    bool FindOccupant(cISC4Occupant*, bool, int*, int*) override { return false; }
    bool GetFirstOccupantByPosition(cISC4Occupant*&, float, float, std::uint32_t) override { return false; }
    bool GetFirstOccupantByPosition(cISC4Occupant*&, float, float, cISC4OccupantFilter*) override { return false; }
    bool GetFirstOccupant(cISC4Occupant*&, int, int, std::uint32_t) override { return false; }
    bool GetFirstOccupant(cISC4Occupant*&, int, int, cISC4OccupantFilter*) override { return false; }
    bool GetFirstOccupantByStandardCityCell(cISC4Occupant*&, int, int, std::uint32_t) override { return false; }
    bool GetFirstOccupantByStandardCityCell(cISC4Occupant*&, int, int, cISC4OccupantFilter*) override { return false; }
    bool GetFirstOccupantByStandardCityCells(cISC4Occupant*&, const int*, const int*, std::uint32_t) override { return false; }
    bool GetFirstOccupantByStandardCityCells(cISC4Occupant*&, const int*, const int*, cISC4OccupantFilter*) override { return false; }
    bool GetOccupantsByBBox(OccupantList&, const float*, const float*, std::uint32_t, std::uint32_t) override { return false; }
    bool GetOccupantsByBBox(OccupantList&, const float*, const float*, cISC4OccupantFilter*, std::uint32_t) override { return false; }
    bool GetOccupantsByOccupantManagerCells(OccupantList&, const int*, const int*, std::uint32_t, std::uint32_t) override { return false; }
    bool GetOccupantsByOccupantManagerCells(OccupantList&, const int*, const int*, cISC4OccupantFilter*, std::uint32_t) override { return false; }
    bool GetOccupantsByStandardCityCells(OccupantList&, const int*, const int*, std::uint32_t, std::uint32_t) override { return false; }
    bool GetOccupantsByStandardCityCells(OccupantList&, const int*, const int*, cISC4OccupantFilter*, std::uint32_t) override { return false; }
    bool IterateOccupantsByBBox(OccupantCallback, void*, const float*, const float*, std::uint32_t) override { return false; }
    bool IterateOccupantsByBBox(OccupantCallback, void*, const float*, const float*, cISC4OccupantFilter*) override { return false; }
    bool IterateOccupants(OccupantCallback, void*, const int*, const int*, std::uint32_t) override { return false; }
    bool IterateOccupants(OccupantCallback, void*, const int*, const int*, cISC4OccupantFilter*) override { return false; }
    bool IterateOccupantsByStandardCityCell(OccupantCallback, void*, const int*, const int*, std::uint32_t) override { return false; }
    bool IterateOccupantsByStandardCityCell(OccupantCallback, void*, const int*, const int*, cISC4OccupantFilter*) override { return false; }
    void ReleaseOccupantList(OccupantList& list) override {
        for (auto* o : list) o->Release();
        list.clear();
    }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }

    OccupantList items, moved;
};

struct Host final : ISignpostHost {
    FakeMessages messages;
    MiniManager manager;
    std::vector<FakeGlyph*> glyphs;          // what StringToGlyphTextureArray hands out
    std::vector<FakeBinding*> bindings;      // every binding CreateTextureBinding made
    std::vector<std::uint32_t> bindingIds;
    std::vector<s3d::cIS3DModelInstance*> installed;
    std::vector<std::pair<s3d::cIS3DModelInstance*, int>> changes;
    int moves = 0;
    bool allow = true;
    int zoom = 2;
    float worldPerViewportUnit = 0.25f;
    float viewDirX = 0.0f, viewDirZ = -1.0f;
    int glyphCalls = 0;
    std::uint32_t lastFontId = 0;
    std::vector<std::pair<cIGZMessageTarget2*, int>> ticks;

    ~Host() override {
        for (auto* binding : bindings) delete binding;
    }
    cIGZMessageServer2* Messages() override { return &messages; }
    void AddTickNotification(cIGZMessageTarget2* target, int channel) override { ticks.emplace_back(target, channel); }
    void RemoveTickNotification(cIGZMessageTarget2* target, int channel) override { ticks.emplace_back(target, -channel); }
    bool PropDeveloperAllowsSigns() override { return allow; }
    cISC4OccupantManager* OccupantManager() override { return &manager; }
    bool RendererHasModel(s3d::cIS3DModelInstance* model) override {
        for (auto* installedModel : installed)
            if (installedModel == model) return true;
        return false;
    }
    void RendererInsertModel(s3d::cIS3DModelInstance* model) override { installed.push_back(model); }
    void RendererRemoveModel(s3d::cIS3DModelInstance* model) override {
        for (auto it = installed.begin(); it != installed.end(); ++it)
            if (*it == model) {
                installed.erase(it);
                return;
            }
    }
    void RendererMoveModel(s3d::cIS3DModelInstance*) override { ++moves; }
    void RendererChangeModel(s3d::cIS3DModelInstance* model, int reason) override { changes.emplace_back(model, reason); }
    void StringToGlyphTextureArray(std::uint32_t fontId, cIGZString&,
                                   std::vector<cRZAutoRefCount<ISignpostGlyphTextureInfo>>& out) override {
        ++glyphCalls;
        lastFontId = fontId;
        out.clear();   // the real manager clears the caller's list first
        for (FakeGlyph* glyph : glyphs) out.push_back(cRZAutoRefCount<ISignpostGlyphTextureInfo>(glyph));
    }
    ISignpostTextureBinding* CreateTextureBinding(std::uint32_t instance) override {
        auto* binding = new FakeBinding();
        binding->id = 0x9000u + instance;
        bindings.push_back(binding);
        bindingIds.push_back(instance);
        return binding;
    }
    int ViewZoom() override { return zoom; }
    void CameraViewDirection(float& x, float& z) override {
        x = viewDirX;
        z = viewDirZ;
    }
    float ViewportUnitsToWorldUnits(float units) override { return units * worldPerViewportUnit; }
};

// The recording driver the draw context talks to.
struct Driver final : gd::cGDriverBase {
    gd::sGDMode mode{};
    std::vector<std::uint32_t> textures;
    std::vector<std::pair<int, int>> draws;      // primitive, count
    std::vector<std::pair<int, int>> formats;    // format, stride
    int matrices = 0;
    bool GetVideoModeInfo(gd::sGDMode& out) override {
        out = mode;
        return true;
    }
    void DrawArrays(gd::eGDPrimType primitive, int, int count) override {
        draws.push_back({static_cast<int>(primitive), count});
    }
    void InterleavedArrays(gd::eGDVertexFormat format, int stride, const void*) override {
        formats.push_back({static_cast<int>(format), stride});
    }
    void SetTexture(std::uint32_t texture, std::uint32_t) override { textures.push_back(texture); }
    void LoadMatrix(const float*) override { ++matrices; }
};

}  // namespace

int main() {
    Host host;
    SetSignpostHost(&host);

    // ---- construction and the interface identity ----------------------------------------------------------
    auto* sign = new cSC4SignpostOccupant();
    sign->AddRef();
    CHECK(!sign->IsInitialized());
    CHECK(sign->EditMode() == false && sign->Text().Strlen() == 0);
    CHECK(sign->GetGZCLSID() == kGZCLSID_cSC4SignpostOccupant);
    std::uint32_t sortValue = 0xffffffffu;
    sign->GetSortValue(sortValue);
    CHECK(sortValue == 0);
    {
        cS3DTransform transform;
        sign->GetTransform(transform);
        CHECK(transform.changeCounter == 0 && transform.flags == 0);
        CHECK(transform.rotation[0][0] == 1.0f && transform.rotation[1][1] == 1.0f && transform.rotation[2][2] == 1.0f);
        CHECK(transform.translation[0] == 0.0f && transform.scale == 1.0f);
        cS3DBoundingBox box;
        sign->GetBounds(box);
        CHECK(box.min[0] == 1.0f && box.min[1] == 1.0f && box.min[2] == 1.0f);   // the "empty" form the ctor stores
        CHECK(box.max[0] == 0.0f && box.max[1] == 0.0f && box.max[2] == 0.0f);
        sign->GetWorldBounds(box);
        CHECK(box.min[0] == 1.0f && box.max[0] == 0.0f);
    }
    CHECK(sign->GetOwner() == static_cast<cIGZUnknown*>(sign->AsOccupant()));
    {
        void* out = nullptr;
        CHECK(sign->QueryInterface(kGZIID_cISC4SignpostOccupant, &out));
        CHECK(out == static_cast<cISC4SignpostOccupant*>(sign));
        sign->Release();   // QueryInterface's reference
        out = nullptr;
        CHECK(sign->QueryInterface(kGZIID_cISC4Occupant, &out));
        CHECK(out == sign->AsOccupant());
        sign->Release();
    }

    // ---- Init: prop type, flag, texture bindings, notifications, render state ------------------------------
    CHECK(sign->Init());
    CHECK(sign->IsInitialized());
    CHECK(sign->GetPropType() == kSignpostPropType);
    CHECK(sign->IsFlagSet(kSignpostFlagInitialized));
    CHECK(host.bindingIds.size() == 10);
    CHECK(host.bindingIds[0] == 0x8b4a6560u && host.bindingIds[6] == 0x8b4a6564u && host.bindingIds[9] == 0x8b4a6567u);
    CHECK(host.messages.added.size() == 2);
    CHECK(host.messages.added[0].second == kSignpostMessageRefresh);
    CHECK(host.messages.added[1].second == kSignpostMessageVisibility);
    {
        cSC4PackedRenderState expected;
        expected.PackRenderState(true, true, true, false, true, 1, false, 3, 4, 5, -1, -1.0f, 1, -1);
        CHECK(sign->RenderState().value == expected.value);
        CHECK(sign->RenderState().mask == expected.mask);
    }
    // the empty sign: 1 unit wide, 24 high, margins 8 / post 64 viewport units, scale 1.0 at zoom 2
    {
        cS3DBoundingBox box;
        sign->GetBounds(box);
        CHECK_NEAR(box.min[0], -2.125f);     // -(0.25 / 2 + 2)
        CHECK_NEAR(box.max[0], 2.125f);
        CHECK_NEAR(box.min[1], 0.0f);
        CHECK_NEAR(box.max[1], 28.0f);       // 3 * 2 + 16 + 6
        CHECK_NEAR(box.min[2], 0.0f);
        CHECK_NEAR(box.max[2], 0.0f);
        sign->GetWorldBounds(box);
        CHECK_NEAR(box.min[0], -2.125f);
        CHECK_NEAR(box.max[1], 28.0f);
        CHECK_NEAR(sign->PickBox(1)[0], -1.0f);   // the post is one margin wide
        CHECK_NEAR(sign->PickBox(1)[3], 1.0f);
        CHECK_NEAR(sign->PickBox(1)[4], 18.0f);   // frame bottom = 28 - 2 - 6 - 2
        cS3DTransform transform;
        sign->GetTransform(transform);
        CHECK(transform.flags == 4);         // the camera-facing rotation was composed
        CHECK(transform.changeCounter == 1);
        CHECK_NEAR(transform.rotation[0][0], 1.0f);
        CHECK_NEAR(transform.rotation[2][2], 1.0f);
    }

    // ---- SetText: glyph quads, sizes and the "same text is a no-op" rule ---------------------------------
    FakeGlyph glyphA, glyphB;
    glyphA.glyphWidth = 10;
    glyphB.glyphWidth = 6;
    glyphB.rect[0] = 0.5f;
    glyphB.rect[2] = 0.625f;
    host.glyphs = {&glyphA, &glyphB};
    const int rebuildsBefore = sign->GlyphTextures().size();
    (void)rebuildsBefore;
    cRZString text("AB");
    sign->SetText(text);
    CHECK(sign->Text().Strlen() == 2 && std::strcmp(sign->Text().ToChar(), "AB") == 0);
    CHECK(host.glyphCalls == 1 && host.lastFontId == kSignpostFontId);
    CHECK(sign->GlyphTextures().size() == 2);
    CHECK(sign->GlyphQuads().size() == 8);
    CHECK_NEAR(sign->GlyphQuads()[0].x, -2.0f);      // left board edge: -(16 * 0.25) / 2
    CHECK_NEAR(sign->GlyphQuads()[2].x, 0.5f);       // + 10 * 0.25
    CHECK_NEAR(sign->GlyphQuads()[0].y, 25.0f);      // board top   = 3 * 2 + 16 + 5 - 2
    CHECK_NEAR(sign->GlyphQuads()[1].y, 20.0f);      // board bottom = board top - 5
    CHECK_NEAR(sign->GlyphQuads()[0].u, 0.0f);
    CHECK_NEAR(sign->GlyphQuads()[1].v, 1.0f);
    CHECK_NEAR(sign->GlyphQuads()[2].u, 0.5f);       // glyph A's right texture coordinate
    CHECK_NEAR(sign->GlyphQuads()[4].x, 0.5f);       // glyph B starts where A ended
    CHECK_NEAR(sign->GlyphQuads()[6].x, 2.0f);       // 0.5 + 6 * 0.25
    CHECK_NEAR(sign->GlyphQuads()[4].u, 0.5f);       // glyph B's left texture coordinate
    CHECK_NEAR(sign->GlyphQuads()[6].u, 0.625f);
    {
        cS3DBoundingBox box;
        sign->GetBounds(box);
        CHECK_NEAR(box.min[0], -4.0f);               // -(4 / 2 + 2)
        CHECK_NEAR(box.max[0], 4.0f);
        CHECK_NEAR(box.max[1], 27.0f);               // 3 * 2 + 16 + 5
        CHECK_NEAR(box.max[1] - box.min[1] - 3 * 2.0f - 16.0f, 5.0f);   // board height = 20 * 0.25
    }
    // the ten static pieces: the frame, the two mirrored side pieces and the post
    {
        const SignVertexT2F* pieces = sign->PieceQuads();
        const auto at = [pieces](int piece, int vertex) -> const SignVertexT2F& {
            return pieces[piece * 4 + vertex];
        };
        CHECK_NEAR(at(0, 0).x, -4.0f);               // left top corner piece starts at the left outer edge
        CHECK_NEAR(at(0, 0).y, 27.0f);               // ... at the sign's top
        CHECK_NEAR(at(0, 1).y, 25.0f);               // ... down to the board top
        CHECK_NEAR(at(0, 2).x, -2.0f);               // ... to the board's left edge
        CHECK_NEAR(at(4, 0).x, -2.0f);               // the strip above the board
        CHECK_NEAR(at(4, 2).x, 2.0f);
        CHECK_NEAR(at(4, 0).u, 0.0f);
        CHECK_NEAR(at(6, 0).u, 1.0f);                // the left side piece has its u mirrored
        CHECK_NEAR(at(6, 1).u, 0.0f);
        CHECK_NEAR(at(7, 0).u, 1.0f);                // ... and so does the right one
        CHECK_NEAR(at(8, 0).x, -1.0f);               // post upper piece
        CHECK_NEAR(at(8, 0).y, 18.0f);               // frame bottom = 25 - 5 - 2
        CHECK_NEAR(at(8, 1).y, 2.0f);
        CHECK_NEAR(at(9, 0).y, 2.0f);                // post base
        CHECK_NEAR(at(9, 1).y, 0.0f);
    }
    // the board's background quad carries the model properties' colour (bit 0 clear -> 0xffbeac90)
    CHECK(sign->BackgroundQuad()[0].color == kSignpostBoardColors[0]);
    CHECK_NEAR(sign->BackgroundQuad()[0].x, -2.0f);
    CHECK_NEAR(sign->BackgroundQuad()[2].x, 2.0f);
    // the same text again does not touch the glyph list
    sign->SetText(text);
    CHECK(host.glyphCalls == 1);
    // an empty text always rebuilds, even when it already is empty
    const cS3DTransform beforeEmpty = sign->Transform();
    cRZString empty("");
    sign->SetText(empty);
    CHECK(sign->GlyphTextures().empty() && sign->GlyphQuads().empty());
    // every rebuild after the first one resets the composed rotation (one change) and composes it again (another)
    const std::uint8_t afterFirstEmpty = sign->Transform().changeCounter;
    CHECK(afterFirstEmpty == static_cast<std::uint8_t>(beforeEmpty.changeCounter + 2));
    sign->SetText(empty);
    CHECK(sign->Transform().changeCounter == static_cast<std::uint8_t>(afterFirstEmpty + 2));
    // back to "AB" for the draw test
    sign->SetText(text);
    CHECK(sign->GlyphQuads().size() == 8);

    // ---- the blink: the tick message only flips the cursor after 500 ms ----------------------------------
    {
        cRZMessage2Standard message;
        message.SetType(kSignpostMessageTick);
        message.SetData2(499000);                 // 499 whole seconds: one below the threshold
        CHECK(sign->DoMessage(&message));
        CHECK(sign->CursorVisible() == false);
        message.SetData2(1000);                   // + 1 -> 500: the cursor flips on
        CHECK(sign->DoMessage(&message));
        CHECK(sign->CursorVisible() == true);
        message.SetData2(600000);                 // 600 -> flips off again and the accumulator restarts
        sign->DoMessage(&message);
        CHECK(sign->CursorVisible() == false);
        message.SetData2(1000);                   // 1: no flip yet
        sign->DoMessage(&message);
        CHECK(sign->CursorVisible() == false);
        // the non-tick messages do not touch the cursor
        message.SetType(kSignpostMessageRefresh);
        sign->DoMessage(&message);
        CHECK(sign->CursorVisible() == false);
    }

    // ---- SetEditMode: dynamic board, tick registration, cursor quad ---------------------------------------
    {
        CHECK(host.changes.empty());
        sign->SetEditMode(true);
        CHECK(sign->EditMode());
        CHECK(sign->CursorVisible());
        CHECK(host.ticks.size() == 1 && host.ticks[0].second == kSignpostTickChannel);
        CHECK(sign->GetProperties() == 3);
        CHECK(sign->BackgroundQuad()[0].color == kSignpostBoardColors[1]);
        const SignVertexC4B* cursor = sign->CursorQuad();
        CHECK_NEAR(cursor[0].x, 1.5f + 0.5f);        // the glyphs stay laid out from the wider board's left edge
        CHECK_NEAR(cursor[2].x, 2.0f + 0.5f);
        CHECK_NEAR(cursor[0].y, 25.0f);              // board top (edit mode widens the board by 4 units)
        CHECK(cursor[0].color == 0xff6a554e);
        // the wider board: width (16 + 4) * 0.25
        CHECK_NEAR(sign->GlyphQuads()[0].y, 25.0f);
        cS3DBoundingBox box;
        sign->GetBounds(box);
        CHECK_NEAR(box.min[0], -4.5f);               // -(5 / 2 + 2)
        sign->SetEditMode(false);
        CHECK(!sign->EditMode());
        CHECK(host.ticks.size() == 2 && host.ticks[1].second == -kSignpostTickChannel);
        CHECK(sign->GetProperties() == 2);
        CHECK(sign->BackgroundQuad()[0].color == kSignpostBoardColors[0]);
    }

    // ---- flags: flag 2 needs the prop developer, and adding/removing drives the renderer ------------------
    {
        sign->SetFlag(kSignpostFlagShown);
        CHECK(sign->IsFlagSet(kSignpostFlagShown));
        CHECK(host.installed.size() == 1);
        sign->SetDynamic(true);                       // properties bit 0 -> 1, shown -> ChangeModel(5)
        CHECK(host.changes.size() == 1 && host.changes[0].second == 5);
        sign->SetDynamic(true);                       // unchanged: no call at all
        CHECK(host.changes.size() == 1);
        sign->ClearFlag(kSignpostFlagShown);
        CHECK(!sign->IsFlagSet(kSignpostFlagShown));
        CHECK(host.installed.empty());
        host.allow = false;
        sign->SetFlag(kSignpostFlagShown);            // refused: no renderer call
        CHECK(!sign->IsFlagSet(kSignpostFlagShown));
        CHECK(host.installed.empty());
        host.allow = true;
        sign->SetFlag(kSignpostFlagShown);
        CHECK(host.installed.size() == 1);
        // the visibility message contains/does not contain the occupant
        FakeViewSet view;
        ISignpostViewSet* viewPointer = &view;
        cRZMessage2Standard message;
        message.SetType(kSignpostMessageVisibility);
        view.contains = true;
        message.SetVoid2(&viewPointer);
        sign->DoMessage(&message);
        CHECK(sign->IsFlagSet(kSignpostFlagShown));
        view.contains = false;
        sign->DoMessage(&message);
        CHECK(!sign->IsFlagSet(kSignpostFlagShown));
        CHECK(host.installed.empty());
        // SetDynamic on an unshown sign does not reach the renderer
        sign->SetDynamic(false);
        CHECK(host.changes.size() == 1);
    }

    // ---- SetPosition: the boxes and the transform move, the renderer is told ------------------------------
    {
        host.changes.clear();
        cS3DBoundingBox before;
        sign->GetWorldBounds(before);
        sign->SetFlag(kSignpostFlagShown);            // ChangeModel(4) then needs the renderer
        host.changes.clear();
        const float position[3] = {10.0f, 2.0f, 20.0f};
        sign->SetPosition(position);
        const cS3DTransform& transform = sign->Transform();
        CHECK_NEAR(transform.translation[0], 10.0f);
        CHECK_NEAR(transform.translation[1], 2.0f);
        CHECK_NEAR(transform.translation[2], 20.0f);
        CHECK((transform.flags & 1) != 0);
        cS3DBoundingBox after;
        sign->GetWorldBounds(after);
        CHECK_NEAR(after.min[0], before.min[0] + 10.0f);
        CHECK_NEAR(after.min[1], before.min[1] + 2.0f);
        CHECK_NEAR(after.min[2], before.min[2] + 20.0f);
        CHECK_NEAR(after.max[2], before.max[2] + 20.0f);
        CHECK_NEAR(sign->PickBox(0)[0], after.min[0]);
        CHECK_NEAR(sign->PickBox(1)[3], 11.0f);       // the post is still one unit either side of x = 10
        CHECK(host.changes.size() == 1 && host.changes[0].second == 4);
        float propPosition[3] = {0, 0, 0};
        sign->GetPosition(propPosition);
        CHECK_NEAR(propPosition[0], 10.0f);
        CHECK_NEAR(propPosition[2], 20.0f);
    }

    // ---- Pick: hits the sign box, misses away from it -----------------------------------------------------
    {
        cSC4PickContext context;
        context.origin = cS3DVector3{10.0f, 100.0f, 20.0f};
        context.direction = cS3DVector3{0.0f, -1.0f, 0.0f};
        context.maxDistance = 1000.0f;
        CHECK(sign->Pick(&context));
        CHECK_NEAR(context.maxDistance, 100.0f - (sign->PickBox(0)[4]));
        cSC4PickContext miss;
        miss.origin = cS3DVector3{0.0f, 100.0f, 0.0f};
        miss.direction = cS3DVector3{0.0f, -1.0f, 0.0f};
        miss.maxDistance = 1000.0f;
        CHECK(!sign->Pick(&miss));
        CHECK_NEAR(miss.maxDistance, 1000.0f);
        // a hit beyond the running maximum is rejected
        cSC4PickContext tooClose;
        tooClose.origin = cS3DVector3{10.0f, 100.0f, 20.0f};
        tooClose.direction = cS3DVector3{0.0f, -1.0f, 0.0f};
        tooClose.maxDistance = 10.0f;
        CHECK(!sign->Pick(&tooClose));
        CHECK_NEAR(tooClose.maxDistance, 10.0f);
    }

    // ---- the occupant manager is told the bounding box moved ----------------------------------------------
    {
        host.manager.moves = 0;
        sign->SetFlag(1);
        cRZString other("XY");
        sign->SetText(other);                          // RebuildModel -> MoveOccupant
        CHECK(host.manager.moves == 1);
        CHECK(host.manager.moved.size() == 1 && host.manager.moved.front() == sign->AsOccupant());
        sign->ClearFlag(1);
    }

    // ---- Draw: the vertex streams and the draw order ------------------------------------------------------
    {
        Driver driver;
        driver.mode.flags[1] = 3;
        cSC4RenderProperties properties;
        SC4DrawContext context;
        context.SetDriver(&driver);
        context.SetRenderProperties(&properties);
        context.Init();
        CHECK(context.IsInitialized());
        driver.draws.clear();
        driver.formats.clear();
        driver.textures.clear();
        CHECK(sign->Draw(&context, false) == 1);
        // the board background, the two glyphs, then the ten frame pieces (no cursor: not editing)
        CHECK(driver.draws.size() == 13);
        CHECK(driver.formats.size() == 13);
        CHECK(driver.formats[0].first == 1);           // sVF_V3F_C4B for the board
        CHECK(driver.formats[1].first == 2);           // sVF_V3F_T2F for the glyphs and the pieces
        CHECK(driver.textures.size() == 12);           // two glyph textures + ten frame textures
        CHECK(driver.textures[0] == glyphA.binding.id);
        CHECK(driver.textures[1] == glyphB.binding.id);
        for (int i = 0; i < 10; ++i) CHECK(driver.textures[2 + i] == host.bindings[i]->id);
        for (const auto& draw : driver.draws) {
            CHECK(draw.first == 6);                    // triangle strip
            CHECK(draw.second == 4);                   // one quad
        }
        // editing with the cursor visible adds one coloured quad between the glyphs and the pieces
        sign->SetEditMode(true);
        driver.draws.clear();
        driver.formats.clear();
        driver.textures.clear();
        CHECK(sign->Draw(&context, false) == 1);
        CHECK(driver.draws.size() == 14);
        CHECK(driver.formats[3].first == 1);           // the cursor after the two glyph quads
        CHECK(driver.textures.size() == 12);
        sign->SetEditMode(false);
        // an uninitialised occupant draws nothing
        auto* fresh = new cSC4SignpostOccupant();
        fresh->AddRef();
        driver.draws.clear();
        CHECK(fresh->Draw(&context, false) == 1);
        CHECK(driver.draws.empty());
        fresh->Release();
    }

    // ---- the version-2 record ----------------------------------------------------------------------------
    {
        PortMemoryOStream stream;
        CHECK(sign->Write(stream));
        const auto& bytes = stream.bytes();
        CHECK(bytes.size() > 2 && bytes[0] == 2 && bytes[1] == 0);
        auto* copy = new cSC4SignpostOccupant();
        copy->AddRef();
        PortMemoryIStream input(bytes.data(), bytes.size());
        CHECK(copy->Read(input));
        cRZString textOut;
        copy->GetText(textOut);
        CHECK(std::strcmp(textOut.ToChar(), "XY") == 0);
        cS3DTransform transform;
        copy->GetTransform(transform);
        CHECK_NEAR(transform.translation[0], 10.0f);
        CHECK_NEAR(transform.translation[1], 2.0f);
        CHECK_NEAR(transform.translation[2], 20.0f);
        CHECK((transform.flags & 1) != 0);
        CHECK(copy->GetPropType() == kSignpostPropType);
        copy->Release();
    }
    // a version-1 record carries an extra u32 before the position
    {
        PortMemoryOStream stream;
        stream.SetUint16(1);
        cSC4PropOccupant prop;                        // the sign's prop half, written by the prop's own record
        PortMemoryOStream propOnly;
        CHECK(prop.Write(propOnly));
        stream.SetVoid(propOnly.bytes().data(), static_cast<std::uint32_t>(propOnly.bytes().size()));
        cRZString oldText("old");
        stream.SetGZStr(oldText);
        stream.SetUint32(0xdeadbeefu);                // the version-1 extra word, dropped by Read
        stream.SetFloat32(1.0f);
        stream.SetFloat32(2.0f);
        stream.SetFloat32(3.0f);
        auto* old = new cSC4SignpostOccupant();
        old->AddRef();
        PortMemoryIStream input(stream.bytes().data(), stream.bytes().size());
        CHECK(old->Read(input));
        cRZString textOut;
        old->GetText(textOut);
        CHECK(std::strcmp(textOut.ToChar(), "old") == 0);
        cS3DTransform transform;
        old->GetTransform(transform);
        CHECK_NEAR(transform.translation[0], 1.0f);
        CHECK_NEAR(transform.translation[1], 2.0f);
        CHECK_NEAR(transform.translation[2], 3.0f);
        old->Release();
    }

    // ---- Shutdown ----------------------------------------------------------------------------------------
    {
        CHECK(sign->Shutdown());
        CHECK(!sign->IsInitialized());
        CHECK(host.messages.removed.size() == 2);
        CHECK(host.messages.removed[0].second == kSignpostMessageRefresh);
        CHECK(host.messages.removed[1].second == kSignpostMessageVisibility);
        for (FakeBinding* binding : host.bindings) CHECK(binding->releases == 1);
        CHECK(sign->GlyphTextures().empty());
    }

    sign->Release();
    SetSignpostHost(nullptr);

    if (failures == 0) std::printf("sc4_signpostoccupant: all checks passed\n");
    else std::printf("sc4_signpostoccupant: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
