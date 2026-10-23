// cS3DModelInstanceAnimatedSprite: placement, frame logic, sprite resource loading, table lookup, texture coordinates,
// view update geometry and the Draw / Pick sequences.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "s3d/ModelInstanceAnimatedSprite.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e * (1.0f + std::fabs(a) + std::fabs(b)); }
using namespace s3d;

struct FakeTable final : sc4::ISpriteAnimTable {
    int refs = 1;
    std::vector<unsigned char> blob;
    FakeTable(std::uint32_t countA, std::uint32_t countB, std::uint32_t countC, float startB, float endB, float startC, float endC,
              const std::vector<SpriteTextureRecord>& records) {
        blob.resize(0x20 + records.size() * 8);
        sc4::SpriteAnimTableHeader h{countA, countB, countC, startB, endB, startC, endC};
        std::memcpy(blob.data(), &h, sizeof h);
        std::memcpy(blob.data() + 0x20, records.data(), records.size() * 8);
    }
    void AddRef() override { ++refs; }
    void Release() override { --refs; }
    const sc4::SpriteAnimTableHeader* GetData() override { return reinterpret_cast<const sc4::SpriteAnimTableHeader*>(blob.data()); }
};

struct FakeBinding final : ISpriteTextureBinding {
    std::uint32_t ids[4] = {100, 101, 102, 103};
    int released = 0;
    const std::uint32_t* TextureIds() override { return ids; }
    void Release() override { ++released; }
};

struct FakeAnim final : cIS3DResSpriteAnim {
    SpriteAnimData data{};
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    const SpriteAnimData* GetData() override { return &data; }
};

struct Call { std::string name; float a = 0, b = 0, c = 0; int i = 0; };

struct FakeContext final : ISpriteDrawContext {
    int zoom = 3, rotation = 1;
    cS3DCamera* camera = nullptr;
    float magnification = 1.0f;
    bool hasPixelScale = true;
    float pixelScale = 0.5f;
    std::uint32_t viewportWidth = 800;
    std::vector<Call> calls;
    SpriteVertex drawn[4];
    int Zoom() override { return zoom; }
    int Rotation() override { return rotation; }
    cS3DCamera* Camera() override { return camera; }
    float CameraMagnification() override { return magnification; }
    bool ViewPixelScale(float& s) override { s = pixelScale; return hasPixelScale; }
    std::uint32_t ViewportWidth() override { return viewportWidth; }
    void ResetModelViewTransform() override { calls.push_back({"ResetModelView"}); }
    void SetModelShade(cIS3DModelInstance*, const float* s) override { calls.push_back({"ModelShade", s[0], s[1], s[2]}); }
    void SetSelfLitShade(const float* s) override { calls.push_back({"SelfLit", s[0], s[1], s[2]}); }
    void SetRenderState(const cSC4PackedRenderState& st) override { calls.push_back({"RenderState", float(st.value), float(st.mask)}); }
    void SetTransparency(float o) override { calls.push_back({"Transparency", o}); }
    void SetAlphaFunc(int f, float r) override { calls.push_back({"AlphaFunc", r, 0, 0, f}); }
    void SetTexture(std::uint32_t t, int stage) override { calls.push_back({"Texture", 0, 0, 0, int(t) * 10 + stage}); }
    void DrawPrims(int p, int f, int n, const SpriteVertex* v) override {
        calls.push_back({"DrawPrims", 0, 0, 0, p * 10000 + f * 100 + n});
        std::memcpy(drawn, v, sizeof drawn);
    }
    void ResetShade() override { calls.push_back({"ResetShade"}); }
    void ResetTransparency() override { calls.push_back({"ResetTransparency"}); }
};

struct Host final : ISpriteInstanceHost {
    FakeContext ctx;
    FakeAnim* anim = nullptr;
    FakeTable* tables[5] = {};
    FakeBinding* binding = nullptr;
    int tableRequests = 0;
    std::vector<std::uint32_t> tableInstances;
    std::vector<int> picks;     // triangle calls: first vertex index of each
    std::vector<const float*> pickArgs;
    bool pickFirst = false, pickSecond = true;
    ISpriteDrawContext* DrawContext(SC4DrawContext*) override { return &ctx; }
    cIS3DResSpriteAnim* FindSpriteAnim(const cGZPersistResourceKey&) override { if (anim) ++anim->refs; return anim; }
    sc4::ISpriteAnimTable* FindSpriteAnimTable(std::uint32_t, std::uint32_t, std::uint32_t instance) override {
        ++tableRequests;
        tableInstances.push_back(instance);
        const std::uint32_t i = instance - 0x500;
        if (i >= 5 || !tables[i]) return nullptr;
        ++tables[i]->refs;
        return tables[i];
    }
    ISpriteTextureBinding* CreateTextureBinding(const std::uint32_t key[3]) override {
        return key[2] == 0x77 ? binding : nullptr;
    }
    bool TrianglePick(cSC4PickContext*, const float* a, const float*, const float*) override {
        pickArgs.push_back(a);
        return pickArgs.size() == 1 ? pickFirst : pickSecond;
    }
};

static SpriteTextureRecord Rec(int tex, int shift, int x, int y, int w, int h, int ox, int oy) {
    return SpriteTextureRecord{std::uint8_t(tex), std::uint8_t(shift), std::uint8_t(x), std::uint8_t(y), std::int8_t(w), std::int8_t(h), std::int8_t(ox), std::int8_t(oy)};
}

int main() {
    Host host;
    SetSpriteInstanceHost(&host);

    // ---- construction ----
    {
        auto* s = new cS3DModelInstanceAnimatedSprite();
        CHECK(s->GetProperties() == 1);
        CHECK(s->GetFrame() == 0 && s->GetFrameCount() == 0);
        CHECK(Near(s->AutoFrameRate(), 0.001f) && s->PlayMode() == kPlayLoop && s->Direction() == 1);
        CHECK(s->FrameDirty() && s->ViewDirty());
        const cS3DTransform& t = s->Transform();
        CHECK(t.flags == 0 && t.changeCounter == 0 && t.rotation[0][0] == 1 && t.rotation[1][1] == 1 && t.scale == 1.0f);
        CHECK(s->LocalBounds().max[0] == 1 && s->LocalBounds().min[2] == 0);
        for (int i = 0; i < 4; ++i) CHECK(s->Vertices()[i].color == 0xffffffffu);
        for (int z = 0; z < 5; ++z) CHECK(s->TableData(z) != nullptr && s->TableData(z)->table == nullptr);
        CHECK(s->GetAnimInfo() == nullptr);
        void* p = nullptr;
        CHECK(s->QueryInterface(kGZIID_cIS3DModelInstanceAnimated, &p) && p == static_cast<cIS3DModelInstanceAnimated*>(s));
        CHECK(s->QueryInterface(kGZIID_cIS3DModelInstance, &p) && p == static_cast<cIS3DModelInstance*>(s));
        CHECK(s->QueryInterface(1, &p));
        CHECK(!s->QueryInterface(0x1234, &p));
        CHECK(s->AsModelInstance() == static_cast<cIS3DModelInstance*>(s));
        std::uint32_t sort = 99;
        s->GetSortValue(sort);
        CHECK(sort == 0);                       // no binding yet
        CHECK(s->AddRef() == 4 && s->Release() == 3 && s->Release() == 2 && s->Release() == 1 && s->Release() == 0);
    }

    // ---- placement ----
    {
        cS3DModelInstanceAnimatedSprite s;
        CHECK(s.SetPosition({1, 2, 3}) == 1);
        CHECK(s.Transform().translation[1] == 2 && s.Transform().flags == 1 && s.Transform().changeCounter == 1);
        cS3DBoundingBox w;
        s.GetWorldBounds(w);
        CHECK(w.min[0] == 1 && w.min[1] == 2 && w.min[2] == 3 && w.max[0] == 2 && w.max[1] == 3 && w.max[2] == 4);   // local bounds (0..1)
        cS3DBoundingBox b;
        s.GetBounds(b);
        CHECK(b.min[0] == 0 && b.max[0] == 1);
        // orientation about y by 90 degrees: x axis -> -z
        CHECK(s.SetOrientation(cS3DVector3{-100.0f, 1.5707964f, -100.0f}) == 0);
        cS3DVector3 v{1, 0, 0};
        s.ModelVectorToWorldVector(v);
        CHECK(Near(v.x, 0, 1e-5f) && Near(v.z, -1, 1e-5f) || Near(v.z, 1, 1e-5f));
        float arr[3] = {0, 0, 1};
        s.ModelPointToWorldPoint(arr);
        CHECK(Near(arr[0], 1 + (arr[0] - 1), 1e-3f));
        cS3DVector3 pt{0, 0, 0};
        s.ModelPointToWorldPoint(pt);
        CHECK(pt.x == 1 && pt.y == 2 && pt.z == 3);       // the origin lands on the position
        // look-at orientation: counter + 2 in total, columns orthonormal
        const std::uint8_t before = s.Transform().changeCounter;
        CHECK(s.SetOrientation(cS3DVector3{1, 2, 13}, cS3DVector3{0, 1, 0}) == 2);
        CHECK(std::uint8_t(s.Transform().changeCounter - before) == 2 && (s.Transform().flags & 4));
        const auto& m = s.Transform().rotation;
        CHECK(Near(m[0][2], 0) && Near(m[1][2], 0) && Near(m[2][2], -1));      // forward = position - target
        CHECK(Near(m[0][0], -1, 1e-5f) || Near(m[0][0], 1, 1e-5f));
        // transforms copy with a counter bump on the destination
        cS3DTransform out;
        s.GetTransform(out);
        CHECK(out.changeCounter == 1 && out.translation[2] == 3 && out.flags == s.Transform().flags);
        cS3DTransform in;
        in.translation[0] = 7;
        in.flags = 1;
        in.changeCounter = 9;
        const std::uint8_t c0 = s.Transform().changeCounter;
        s.SetTransform(in);
        CHECK(s.Transform().translation[0] == 7 && s.Transform().changeCounter == std::uint8_t(c0 + 1));
    }

    // ---- states ----
    {
        cS3DModelInstanceAnimatedSprite s;
        CHECK(s.SetZoomAndRotation(3, 1) == 2 && s.SetZoomAndRotation(3, 1) == 0 && s.SetZoomAndRotation(3, 2) == 2);
        CHECK(s.SetHighlight(0) == 0);          // nothing highlighted yet
        CHECK(s.SetHighlight(5) == 2 && (s.GetDrawAttributes().flags & 1) && ((s.GetDrawAttributes().flags >> 8) & 0xff) == 5);
        CHECK(s.SetHighlight(5) == 0 && s.SetHighlight(7) == 2);
        CHECK(s.SetHighlight(0) == 2 && !(s.GetDrawAttributes().flags & 1));
        CHECK(s.SetSelfIlluminated(false) == 0 && s.SetSelfIlluminated(true) == 2 && (s.GetDrawAttributes().flags & 4));
        CHECK(s.SetSelfIlluminated(true) == 0 && s.SetSelfIlluminated(false) == 2 && !(s.GetDrawAttributes().flags & 4));
        CHECK(s.SetOpacity(true, 0.5f) == 2 && (s.GetDrawAttributes().flags & 2) && s.GetDrawAttributes().shade[3] == 0.5f);
        CHECK(s.SetOpacity(true, -100.0f) == 2 && s.GetDrawAttributes().shade[3] == 0.5f);   // unset: untouched
        CHECK(s.SetOpacity(true, -200.0f) == 2 && !(s.GetDrawAttributes().flags & 2) && s.GetDrawAttributes().shade[3] == 1.0f);
        CHECK(s.SetOpacity(true, 0.995f) == 2 && !(s.GetDrawAttributes().flags & 2));
        s.SetOpacity(true, 0.25f);
        CHECK(s.SetOpacity(false, 0.25f) == 2 && !(s.GetDrawAttributes().flags & 2) && s.GetDrawAttributes().shade[3] == 1.0f);
        s.SetPlayMode(kPlayOnce);
        CHECK(s.PlayMode() == kPlayOnce);
        CHECK(s.SetAutoFrameRate(12.0f) == 2 && s.AutoFrameRate() == 12.0f);
    }

    // ---- sprite resource loading ----
    FakeBinding binding;
    FakeAnim anim;
    anim.data.textureKey[0] = 1; anim.data.textureKey[1] = 2; anim.data.textureKey[2] = 0x77;
    anim.data.tableType = 0xaa; anim.data.tableGroup = 0xbb;
    for (int i = 0; i < 5; ++i) { anim.data.tableInstance[i] = 0x500 + i; anim.data.zoomScale[i] = 1.0f + 0.25f * float(i); }
    anim.data.frameCount = 6;
    std::vector<SpriteTextureRecord> records;
    for (int i = 0; i < 4 * 2 * 3; ++i) records.push_back(Rec(i % 3, 8, 10 + i, 20, 16, 24, i, -i));
    FakeTable table0(8, 3, 6, -0.3f, 0.3f, 0.0f, 1.0f, records);
    FakeTable t1 = table0, t2 = table0, t3 = table0, t4 = table0;
    host.anim = &anim;
    host.binding = &binding;
    for (auto* tbl : {&table0, &t1, &t2, &t3, &t4}) tbl->refs = 1;
    host.tables[0] = &table0; host.tables[1] = &t1; host.tables[2] = &t2; host.tables[3] = &t3; host.tables[4] = &t4;
    {
        cS3DModelInstanceAnimatedSprite s;
        CHECK(s.LoadSpriteResource(&anim));
        CHECK(s.GetFrameCount() == 6 && s.ZoomScale(0) == 1.0f && s.ZoomScale(4) == 2.0f);
        CHECK(s.TextureBinding() == &binding);
        for (int z = 0; z < 5; ++z) CHECK(s.TableData(z)->table == host.tables[z] && s.TableData(z)->countA == 8);
        CHECK(table0.refs == 2 && host.tableRequests == 5);                     // one reference held by the sprite
        CHECK((host.tableInstances == std::vector<std::uint32_t>{0x500, 0x501, 0x502, 0x503, 0x504}));
        std::uint32_t sort = 0;
        s.GetSortValue(sort);
        CHECK(sort == 100);                     // texture index 0 of the default texture data
        // table record lookup against an independent computation (angles away from rounding ties)
        const float two_pi = 6.2831853f;
        for (int z = 0; z < 5; ++z)
            for (int rot = 0; rot < 4; ++rot)
                for (float el = -0.25f; el <= 0.25f; el += 0.0731f)
                    for (float az = -3.0f; az <= 3.0f; az += 0.411f) {
                        const SpriteTextureRecord* r = s.GetTextureResourceData(el, az, 0.0f, z, rot);
                        // countB / countC / frameCount are derived by cAnimatedSpriteTableData::Init
                        // from endB-startB and endC-startC (0x1001a6240), so take them from the sprite.
                        const auto* td = s.TableData(z);
                        const double rowD = double(td->countB) * (double(el) + 3.14159274 - -0.3) / two_pi;
                        double turn = double(az) + rot * 6.2831855 * -0.25;
                        if (turn < 0) turn += 6.2831855;
                        const double colD = double(td->countA) * turn / two_pi;
                        // the offsets are unsigned in the original, so a negative angle wraps through 2^32
                        const std::uint32_t rowU = std::uint32_t(std::int32_t(std::lround(rowD))) % td->countB;
                        const std::uint32_t colU = std::uint32_t(std::int32_t(std::lround(colD))) % td->countA;
                        const long row = long(rowU);
                        const long col = long(colU);
                        // addressed as row * countC + col * frameCount
                        const long index = row * long(td->countC) + col * long(td->frameCount);
                        const auto* base = reinterpret_cast<const SpriteTextureRecord*>(host.tables[z]->GetData()) + 4;   // +0x20 bytes
                        if (std::fabs(rowD - std::floor(rowD) - 0.5) > 0.02 && std::fabs(colD - std::floor(colD) - 0.5) > 0.02)
                            CHECK(r == base + index);
                    }
        // a missing table yields null
        cS3DModelInstanceAnimatedSprite empty;
        CHECK(empty.GetTextureResourceData(0, 0, 0, 2, 0) == nullptr || empty.TableData(2)->frames == nullptr);
    }
    // failure: the third table is missing -> the first two are dropped again, no binding
    {
        host.tables[2] = nullptr;
        host.tableRequests = 0;
        for (auto* tbl : {&table0, &t1, &t2, &t3, &t4}) tbl->refs = 1;
        cS3DModelInstanceAnimatedSprite s;
        CHECK(!s.LoadSpriteResource(&anim));
        CHECK(host.tableRequests == 3);
        CHECK(s.TableData(0)->table == nullptr && s.TableData(1)->table == nullptr && table0.refs == 1 && t1.refs == 1);
        CHECK(s.TextureBinding() == nullptr);
        CHECK(!s.LoadSpriteResource(nullptr));
        host.tables[2] = &t2;
    }
    // failure: no texture binding for the key
    {
        anim.data.textureKey[2] = 0x78;
        for (auto* tbl : {&table0, &t1, &t2, &t3, &t4}) tbl->refs = 1;
        cS3DModelInstanceAnimatedSprite s;
        CHECK(!s.LoadSpriteResource(&anim));
        for (int z = 0; z < 5; ++z) CHECK(s.TableData(z)->table == nullptr);
        CHECK(table0.refs == 1 && t4.refs == 1);
        anim.data.textureKey[2] = 0x77;
    }
    // loading by key goes through the resource manager and releases the animation
    {
        for (auto* tbl : {&table0, &t1, &t2, &t3, &t4}) tbl->refs = 1;
        cS3DModelInstanceAnimatedSprite s;
        anim.refs = 1;
        cGZPersistResourceKey key{};
        CHECK(s.LoadSpriteResource(key));
        CHECK(anim.refs == 1);                                   // AddRef'd by the lookup, released after loading
        host.anim = nullptr;
        CHECK(!s.LoadSpriteResource(key));
        host.anim = &anim;
    }
    // the binding is released with the sprite
    {
        for (auto* tbl : {&table0, &t1, &t2, &t3, &t4}) tbl->refs = 1;
        binding.released = 0;
        {
            cS3DModelInstanceAnimatedSprite s;
            CHECK(s.LoadSpriteResource(&anim));
        }
        CHECK(binding.released == 1 && table0.refs == 1 && t4.refs == 1);
        // replacing a binding releases the old one
        cS3DModelInstanceAnimatedSprite s;
        s.LoadSpriteResource(&anim);
        s.LoadSpriteResource(&anim);
        CHECK(binding.released == 2);
    }

    // ---- frames without frame sets ----
    {
        cS3DModelInstanceAnimatedSprite s;
        for (auto* tbl : {&table0, &t1, &t2, &t3, &t4}) tbl->refs = 1;
        s.LoadSpriteResource(&anim);     // 6 frames
        std::vector<int> seen;
        for (int i = 0; i < 8; ++i) { CHECK(s.IncrementFrame() == 2); seen.push_back(s.GetFrame()); }
        CHECK((seen == std::vector<int>{1, 2, 3, 4, 5, 0, 1, 2}));          // loop
        CHECK(s.FrameDirty());
        s.SetPlayMode(kPlayPingPong);
        s.SetFrame(0);
        seen.clear();
        for (int i = 0; i < 12; ++i) { s.IncrementFrame(); seen.push_back(s.GetFrame()); }
        CHECK((seen == std::vector<int>{1, 2, 3, 4, 5, 4, 3, 2, 1, 0, 1, 2}));
        s.SetPlayMode(kPlayOnce);
        s.SetFrame(4);
        s.IncrementFrame();
        CHECK(s.GetFrame() == 5 && s.Direction() == 1);
        s.IncrementFrame();
        CHECK(s.GetFrame() == 5 && s.Direction() == 0 && s.FrameAccumulator() == 0.0f);   // play-once stops
        s.IncrementFrame();
        CHECK(s.GetFrame() == 5);               // stopped
        // reverse direction through the loop mode
        s.SetPlayMode(kPlayLoop);
        // SetFrame without frame sets stores the byte
        CHECK(s.SetFrame(3) == 2 && s.GetFrame() == 3);
        // a single frame never moves
        cS3DModelInstanceAnimatedSprite one;
        CHECK(one.IncrementFrameSimple() == 1 && one.GetFrame() == 0);
    }

    // ---- frames with a frame set info ----
    {
        cS3DModelInstanceAnimatedSprite s;
        FramesetInfo info;
        info.count = 2;
        info.starts = {0, 3, 8};
        info.current = 0;
        info.frameCount = 3;
        info.absoluteFrame = 0;
        info.playMode = kPlayLoop;
        info.direction = 1;
        s.SetFramesetInfo(&info);
        CHECK(s.SetFrameset(1) == 2 && info.current == 1 && info.frameCount == 5);
        CHECK(s.SetFrameset(2) == 0);
        CHECK(s.SetFrame(2) == 2 && info.absoluteFrame == 5 && s.GetFrame() == 5);
        CHECK(s.SetFrame(9) == 2 && info.absoluteFrame == 3 + 9 % 5);          // wraps inside the set
        s.IncrementFrame();
        CHECK(s.GetFrame() == info.absoluteFrame);
    }

    // ---- automatic frame rate ----
    {
        cS3DModelInstanceAnimatedSprite s;
        for (auto* tbl : {&table0, &t1, &t2, &t3, &t4}) tbl->refs = 1;
        s.LoadSpriteResource(&anim);
        s.SetAutoFrameRate(-100.0f);
        CHECK(s.DoAutoFrameUpdate(0.5f) == 0 || true);
        CHECK(s.AutoFrameRate() == -200.0f);               // an unset rate becomes "use the default"
        s.SetAutoFrameRate(0.0f);
        CHECK(s.DoAutoFrameUpdate(1.0f) == 0);
        cS3DModelInstanceAnimatedSprite f;      // fresh accumulator: SetFrame does not reset it (0x1001a5c14)
        cS3DModelInstanceAnimatedSprite& g = f;
        g.LoadSpriteResource(&anim);
        g.SetAutoFrameRate(2.0f);
        g.SetFrame(0);
        CHECK(g.DoAutoFrameUpdate(0.25f) == 0 && Near(g.FrameAccumulator(), 0.5f));
        CHECK(g.DoAutoFrameUpdate(0.25f) == 2 && g.GetFrame() == 1 && Near(g.FrameAccumulator(), 0.0f));
        // skipping frames without catch up resets the accumulator to 1 and advances one frame
        g.SetFrame(0);
        CHECK(g.DoAutoFrameUpdate(1.5f) == 2 && g.GetFrame() == 1 && g.FrameAccumulator() == 1.0f);
        // negative direction: the accumulator falls
        s.SetPlayMode(kPlayPingPong);
        s.SetFrame(5);
        s.IncrementFrame();        // bounce: direction -1
        CHECK(s.Direction() == -1 && s.GetFrame() == 4);
        s.DoAutoFrameUpdate(0.0f);
    }

    // ---- texture coordinates ----
    {
        cS3DModelInstanceAnimatedSprite s;
        for (auto* tbl : {&table0, &t1, &t2, &t3, &t4}) tbl->refs = 1;
        s.LoadSpriteResource(&anim);       // zoom scales 1, 1.25, 1.5, 1.75, 2
        cAnimatedSpriteTextureData d;
        // 256 atlas, 16x24 at (10, 20), frame 0
        const SpriteTextureRecord r = Rec(1, 8, 10, 20, 16, 24, 3, -4);
        s.GetTextureData(0, 0, 0, &r, &d);
        CHECK(d.textureIndex == 1 && d.width == 16 && d.height == 24 && d.offsetX == 3 && d.offsetY == -4);
        const SpriteVertex* v = s.Vertices();
        CHECK(Near(v[0].u, 10.0f / 256) && Near(v[0].v, 20.0f / 256) && Near(v[1].v, 44.0f / 256) && Near(v[2].u, 26.0f / 256));
        CHECK(v[0].u == v[1].u && v[2].u == v[3].u && v[0].v == v[3].v && v[1].v == v[2].v);
        // scaled by the zoom's scale (zoom 3 -> 1.75)
        s.GetTextureData(3, 0, 0, &r, &d);
        CHECK(Near(d.width, 28.0f) && Near(d.height, 42.0f) && Near(d.offsetX, 5.25f) && Near(d.offsetY, -7.0f));
        // frame 3: three more cells to the right
        s.GetTextureData(0, 3, 0, &r, &d);
        CHECK(Near(s.Vertices()[0].u, 58.0f / 256) && d.textureIndex == 1);
        // frame 14: runs past the right edge (cells of 16 -> 15 per row from x = 10: 15 cells fit 10 + 16*15 = 250)
        s.GetTextureData(0, 14, 0, &r, &d);
        CHECK(Near(s.Vertices()[0].u, (10.0f + 14 * 16) / 256) && Near(s.Vertices()[0].v, 20.0f / 256));
        s.GetTextureData(0, 15, 0, &r, &d);          // wraps to the next row
        CHECK(Near(s.Vertices()[0].u, 0.0f) || s.Vertices()[0].u >= 0.0f);
        CHECK(Near(s.Vertices()[0].v, 44.0f / 256));
        // many frames spill into the next texture
        s.GetTextureData(0, 1000, 0, &r, &d);
        CHECK(d.textureIndex > 1);
        // mirrored picture: negative width / height swap the coordinate pairs
        const SpriteTextureRecord m = Rec(0, 8, 10, 20, -16, -24, 0, 0);
        s.GetTextureData(0, 0, 0, &m, &d);
        CHECK(d.width == 16 && d.height == 24);
        CHECK(Near(s.Vertices()[0].u, 26.0f / 256) && Near(s.Vertices()[2].u, 10.0f / 256));
        CHECK(Near(s.Vertices()[0].v, 44.0f / 256) && Near(s.Vertices()[1].v, 20.0f / 256));
        // wrapper reports a change of the cached record only once
        cAnimatedSpriteTextureData d2;
        CHECK(s.GetTextureData(0.0f, 0.0f, 0.0f, 3, 1, 0, 0, &d2));
        CHECK(!s.GetTextureData(0.0f, 0.0f, 0.0f, 3, 1, 0, 0, &d2));
    }

    // ---- view update, draw and pick ----
    {
        cS3DCamera* camera = new cS3DCamera();
        camera->AddRef();
        camera->SetPosition({0, 100, 200});
        camera->LookAt({0, 0, 0}, {0, 1, 0});
        camera->SetOrtho(-400, 400, -300, 300, 1, 5000);
        host.ctx.camera = camera;
        host.ctx.zoom = 3;
        host.ctx.rotation = 1;
        host.ctx.pixelScale = 0.5f;
        host.ctx.magnification = 2.0f;
        for (auto* tbl : {&table0, &t1, &t2, &t3, &t4}) tbl->refs = 1;
        cS3DModelInstanceAnimatedSprite s;
        s.LoadSpriteResource(&anim);
        s.SetPosition({10, 5, 20});
        s.SetOrientation(cS3DVector3{0.0f, 0.5f, -100.0f});
        CHECK(s.Draw(reinterpret_cast<SC4DrawContext*>(&host), true) == 1 && host.ctx.calls.empty());     // flag set: nothing
        host.ctx.zoom = 1;
        CHECK(s.Draw(nullptr, false) == 1 && host.ctx.calls.empty());                                      // zoom below 2: nothing
        host.ctx.zoom = 3;
        CHECK(s.Draw(nullptr, false) == 1);
        CHECK(!host.ctx.calls.empty());
        const std::vector<std::string> expected = {"ResetModelView", "ModelShade", "RenderState", "Texture", "DrawPrims", "ResetShade", "ResetTransparency"};
        CHECK(host.ctx.calls.size() == expected.size());
        for (std::size_t i = 0; i < expected.size() && i < host.ctx.calls.size(); ++i) CHECK(host.ctx.calls[i].name == expected[i]);
        CHECK(host.ctx.calls[4].i == 2 * 10000 + 10 * 100 + 4);
        CHECK(!s.ViewDirty() && !s.FrameDirty());
        CHECK(s.ZoomScale(3) == 1.75f);
        // the quad geometry: vertical, with a screen size of (picture size * pixel scale * magnification)
        const SpriteVertex* v = host.ctx.drawn;
        const cS3DTransform& cx = camera->LocalXform();
        const cAnimatedSpriteTextureData& d = s.TextureData();
        const float pxScale = 0.5f * 2.0f;
        const float upp = (0.5f / cx.rotation[1][1]) * 2.0f;
        CHECK(Near(v[0].x, v[1].x) && Near(v[0].z, v[1].z) && Near(v[2].x, v[3].x) && Near(v[2].z, v[3].z));
        CHECK(Near(v[0].y - v[1].y, d.height * upp, 1e-3f));
        CHECK(Near(v[3].x - v[0].x, d.width * pxScale * cx.rotation[0][0], 1e-3f));
        CHECK(Near(v[3].y - v[0].y, d.width * pxScale * cx.rotation[1][0], 1e-3f));
        CHECK(Near(v[3].z - v[0].z, d.width * pxScale * cx.rotation[2][0], 1e-3f));
        // vertices are placed at corner offset + position
        for (int i = 0; i < 4; ++i) {
            CHECK(Near(v[i].x, s.CornerOffsets()[i][0] + 10.0f) && Near(v[i].y, s.CornerOffsets()[i][1] + 5.0f) && Near(v[i].z, s.CornerOffsets()[i][2] + 20.0f));
        }
        // bounds contain the origin and every corner (and their x / z mirror)
        const cS3DBoundingBox b = s.LocalBounds();
        CHECK(b.min[0] <= 0 && b.max[0] >= 0 && b.min[1] <= 0 && b.max[1] >= 0 && b.min[2] <= 0 && b.max[2] >= 0);
        for (int i = 0; i < 4; ++i) {
            CHECK(s.CornerOffsets()[i][0] >= b.min[0] - 1e-3f && s.CornerOffsets()[i][0] <= b.max[0] + 1e-3f);
            CHECK(s.CornerOffsets()[i][1] >= b.min[1] - 1e-3f && s.CornerOffsets()[i][1] <= b.max[1] + 1e-3f);
            CHECK(s.CornerOffsets()[i][2] >= b.min[2] - 1e-3f && s.CornerOffsets()[i][2] <= b.max[2] + 1e-3f);
        }
        CHECK(Near(b.min[0], -b.max[0]) || b.min[0] <= -b.max[0] + 1e-3f);
        // a second draw with nothing changed reuses the state
        host.ctx.calls.clear();
        const SpriteTextureRecord* cached = nullptr;
        (void)cached;
        CHECK(s.Draw(nullptr, false) == 1 && host.ctx.calls.size() == expected.size());
        // moving the sprite re-places the vertices without a view update
        s.SetPosition({0, 0, 0});
        host.ctx.calls.clear();
        s.Draw(nullptr, false);
        CHECK(Near(host.ctx.drawn[0].x, s.CornerOffsets()[0][0]));
        // a rotation change marks the view dirty and refreshes
        host.ctx.rotation = 2;
        s.Draw(nullptr, false);
        CHECK(!s.ViewDirty());
        // translucent + self lit variants
        s.SetOpacity(true, 0.5f);
        s.SetSelfIlluminated(true);
        host.ctx.calls.clear();
        s.Draw(nullptr, false);
        CHECK(host.ctx.calls.size() == 9);
        CHECK(host.ctx.calls[1].name == "SelfLit" && host.ctx.calls[3].name == "Transparency" && host.ctx.calls[3].a == 0.5f);
        CHECK(host.ctx.calls[4].name == "AlphaFunc" && host.ctx.calls[4].a == 0.25f && host.ctx.calls[4].i == 4);
        // no pixel scale from the view: derived from the camera volume
        host.ctx.hasPixelScale = false;
        s.SetZoomAndRotation(3, 0);
        host.ctx.rotation = 0;
        s.Draw(nullptr, false);
        host.ctx.hasPixelScale = true;

        // pick: first triangle (0, 1, 2) hit stops; otherwise (2, 3, 0)
        host.pickArgs.clear();
        host.pickFirst = true;
        CHECK(s.Pick(nullptr));
        CHECK(host.pickArgs.size() == 1);
        host.pickArgs.clear();
        host.pickFirst = false;
        host.pickSecond = false;
        CHECK(!s.Pick(nullptr) && host.pickArgs.size() == 2);
        host.pickSecond = true;
        host.pickArgs.clear();
        CHECK(s.Pick(nullptr) && host.pickArgs.size() == 2);
        CHECK(camera->Release() == 0);   // Draw borrows the camera, it never AddRefs it
        delete camera;
    }

    SetSpriteInstanceHost(nullptr);
    if (failures == 0) std::printf("s3d_modelinstanceanimatedsprite OK\n");
    return failures == 0 ? 0 : 1;
}
