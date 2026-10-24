// cS3DModelProcessor — unit test of the recovered class against the 0x100108e06 / 0x100165910..0x10016667e
// disassembly: the layout defaults, SetDestFormat, the SetModel conversion of every mesh vertex array into the
// destination format (with the opaque white colour and the copied texture coordinates), the borrowed-array restore on
// reset and destruction, the rejection paths (no mesh view, empty mesh list, a source array in the wrong format, an
// animated instance with a real frame animation), the per-vertex normal computation (accumulated double triangle area
// and its normalisation), the lighting pass through cISC4LightingManager vslot 0xd8 (count / strides / colour offset /
// rotation and the rotation flip) with the "lit" material flag, the descending depth order of SortMeshes (including
// equal and NaN depths), the assignment operator, and the ReleaseModel quirk (model and mesh released, the arrays not
// restored, the +0x78 flag suppressing the destructor's second release).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "render/SimGL.h"
#include "sc4/LightingManager.h"
#include "s3d/ModelInstanceAnimatedSprite.h"
#include "s3d/ModelProcessor.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_NEAR(a, b) do { const double va = (a), vb = (b); if (!(std::fabs(va - vb) <= 1e-9)) { \
    std::printf("FAIL line %d: %s == %g, expected %g\n", __LINE__, #a, va, vb); ++failures; } } while (0)

namespace {

// ---- fakes -------------------------------------------------------------------------------------------------

// cISC4LightingManager (recovered: src/sc4/LightingManager.h). Only vslot 0xd8 GetVertexColors is used; the fake
// records the arguments so the test can check the strides and the buffers the processor passes.
struct FakeLighting final : sc4::cISC4LightingManager {
    int refs = 1;
    int calls = 0;
    int count = 0, normalStride = 0, colourStride = 0;
    std::vector<cS3DVector3> normals;
    std::uint8_t colours[4] = {};
    nS3D::Matrix3 rotation;
    const std::uint8_t* coloursPointer = nullptr;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return std::uint32_t(++refs); }
    std::uint32_t Release() override { return std::uint32_t(--refs); }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    float GetTimeOfDay() const override { return 0.0f; }
    bool SetTimeOfDay(float) override { return false; }
    void SetLightingTint(const cS3DVector3&, float) override {}
    void ResetLightingTint() override {}
    bool IsNight() const override { return false; }
    void GetGlobalLight(cS3DVector3* out) const override { if (out) *out = cS3DVector3{1, 1, 1}; }
    bool GetModelLight(s3d::cIS3DModelInstance*, cS3DVector3*) override { return false; }
    void GetTerrainLighting(const cS3DVector3&, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override {
        r = g = b = 0;
    }
    cS3DVector3 GetShadowDirection() const override { return cS3DVector3{}; }
    void GetShadowParams(float& strength, float& threshold, cS3DVector3& colour) override {
        strength = threshold = 0.0f;
        colour = cS3DVector3{};
    }
    float GetShadowStrengthFromLocation(const cS3DVector2&) override { return 0.0f; }
    void GetDecalShadowParams(sc4::cISCPropertyHolder*, std::uint32_t& textureID, float& strength, float& threshold,
                              float& size, cS3DVector2& offset, cS3DVector3& colour) override {
        textureID = 0;
        strength = threshold = size = 0.0f;
        offset = cS3DVector2{};
        colour = cS3DVector3{};
    }
    bool CalcShadowProjection(int, const cS3DVector3*, const cS3DVector2*, const cS3DTransform&, cS3DMatrix4*) override {
        return false;
    }
    bool CalcShadowTerrainRect(int, const cS3DVector3*, const cS3DBoundingBox&, cRZRect*) override { return false; }
    cS3DVector3 GetColor() override { return cS3DVector3{}; }
    cS3DVector3 GetColor(const cS3DVector3&) override { return cS3DVector3{}; }
    cS3DVector3 GetColor(const cS3DVector3&, const cS3DVector3&) override { return cS3DVector3{}; }
    void GetColor(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { r = g = b = 0; }
    void GetColor(const cS3DVector3&, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { r = g = b = 0; }
    void GetColor(const cS3DVector3&, const cS3DVector3&, std::uint8_t& r, std::uint8_t& g,
                  std::uint8_t& b) override {
        r = g = b = 0;
    }
    void GetTerrainColor(const cS3DVector3&, const cS3DVector3&, std::uint8_t& r, std::uint8_t& g,
                         std::uint8_t& b) override {
        r = g = b = 0;
    }
    void GetTerrainColorFloor(const cS3DVector3&, const cS3DVector3&, std::uint8_t& r, std::uint8_t& g,
                              std::uint8_t& b) override {
        r = g = b = 0;
    }
    void GetVertexColors(int normalCount, const cS3DVector3* normalData, int normalStrideIn,
                         std::uint8_t* colourData, int colourStrideIn, const nS3D::Matrix3& rotationIn) override {
        ++calls;
        count = normalCount;
        normalStride = normalStrideIn;
        colourStride = colourStrideIn;
        normals.assign(normalData, normalData + normalCount);
        coloursPointer = colourData;
        rotation = rotationIn;
        for (int i = 0; i < 4; ++i) colours[i] = colourData[i];   // the first record's colour
    }
    void GetTerrainVertexColors(int, int, int, const cS3DVector3*, int, std::uint8_t*, int) override {}
};

// The instance the processor takes: an animated instance (so both QueryInterface answers can be exercised) with an
// optional mesh view.
struct FakeModel final : s3d::cIS3DModelInstanceAnimated {
    int refs = 1;
    int queries = 0, transforms = 0;
    bool answersAnimated = false;
    int frameCount = 1;
    s3d::cS3DMeshAndMaterial* mesh = nullptr;
    cS3DTransform transform;
    cS3DBoundingBox bounds{{-1, -1, -1}, {1, 1, 1}};
    s3d::SC4DrawAttributes attributes;
    int properties = 2;

    bool QueryInterface(std::uint32_t iid, void** out) override {
        ++queries;
        if (iid == s3d::kGZIID_cIS3DModelInstance || iid == 1) {
            *out = static_cast<s3d::cIS3DModelInstance*>(this);
            AddRef();
            return true;
        }
        if (iid == s3d::kGZIID_cIS3DModelInstanceAnimated && answersAnimated) {
            *out = static_cast<s3d::cIS3DModelInstanceAnimated*>(this);
            AddRef();
            return true;
        }
        if (iid == s3d::kGZIID_cIS3DMeshAndMaterial && mesh) {
            *out = mesh;
            mesh->AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return std::uint32_t(++refs); }
    std::uint32_t Release() override { return std::uint32_t(--refs); }
    cIGZUnknown* GetOwner() override { return nullptr; }
    void SetOwner(cIGZUnknown*) override {}
    void* GetCustomData() override { return nullptr; }
    void SetCustomData(void*) override {}
    void GetBounds(cS3DBoundingBox& out) override { out = bounds; }
    void GetWorldBounds(cS3DBoundingBox& out) override { out = bounds; }
    void GetTransform(cS3DTransform& out) override { ++transforms; out = transform; }
    void SetTransform(const cS3DTransform& value) override { transform = value; }
    bool Pick(cSC4PickContext*) override { return false; }
    int Draw(SC4DrawContext*, bool) override { return 1; }
    int GetProperties() override { return properties; }
    void SetProperties(int value) override { properties = value; }
    s3d::SC4DrawAttributes GetDrawAttributes() const override { return attributes; }
    void SetDrawAttributes(const s3d::SC4DrawAttributes& value) override { attributes = value; }
    void GetSortValue(std::uint32_t& out) const override { out = 0; }
    s3d::cIS3DResModel* GetResModel() const override { return nullptr; }

    s3d::cIS3DModelInstance* AsModelInstance() override { return this; }
    int SetPosition(const cS3DVector3&) override { return 0; }
    int SetOrientation(const cS3DVector3&) override { return 0; }
    int SetOrientation(const cS3DVector3&, const cS3DVector3&) override { return 0; }
    void ModelPointToWorldPoint(cS3DVector3&) override {}
    void ModelPointToWorldPoint(float*) override {}
    void ModelVectorToWorldVector(cS3DVector3&) override {}
    void ModelVectorToWorldVector(float*) override {}
    int SetZoomAndRotation(int, int) override { return 0; }
    int SetFrameset(int) override { return 0; }
    int SetFrame(int) override { return 0; }
    int GetFrame() const override { return 0; }
    int GetFrameCount() const override { return frameCount; }
    int IncrementFrame() override { return 0; }
    int SetAutoFrameRate(float) override { return 0; }
    int DoAutoFrameUpdate(float) override { return 0; }
    void SetPlayMode(int) override {}
    const void* GetAnimInfo() const override { return nullptr; }
    int SetOpacity(bool, float) override { return 0; }
    int SetHighlight(int) override { return 0; }
    int SetSelfIlluminated(bool) override { return 0; }
};

// One mesh record to build: vertices in the accepted format 2 (xyz + uv, stride 0x14), a triangle list and bounds.
struct MeshSpec {
    std::vector<float> positions;        // 3 floats per vertex
    std::vector<float> uvs;              // 2 floats per vertex
    std::vector<std::uint16_t> indices;  // empty: the primitive addresses the vertices directly
    cS3DVector3 boundsMin{-1, -1, -1};
    cS3DVector3 boundsMax{1, 1, 1};
    // a real model resource stores the *packed* format id (the value the processor compares against)
    std::uint32_t format = std::uint32_t(nRZSimGL::PackStandardVertexFormat(int(s3d::kModelProcessorAcceptedFormat)));
};

// The real mesh list, with its reference count visible to the test (the member is protected in the port).
struct TestMesh final : s3d::cS3DMeshAndMaterial {
    int RefCount() const { return int(refCount_); }
};

// A mesh list (cS3DMeshAndMaterial) plus the storage the raw mesh records point at, and the vertex arrays the fixture
// releases when the test is done (the port's mesh list does not own them).
struct MeshFixture {
    s3d::cS3DMeshAndMaterial* mesh = nullptr;
    std::vector<std::unique_ptr<std::vector<std::uint16_t>>> indexBuffers;
    std::vector<std::unique_ptr<const std::uint16_t*>> indexGroups;
    std::vector<std::unique_ptr<std::vector<s3d::MeshPrimitive>>> primitiveLists;
    std::vector<std::unique_ptr<s3d::ModelProcessorDrawList>> drawLists;
    std::vector<s3d::cS3DVertexArray*> originals;

    explicit MeshFixture(const std::vector<MeshSpec>& specs) {
        auto* created = new TestMesh();
        mesh = created;
        mesh->AddRef();
        mesh->Meshes().resize(specs.size());
        mesh->Materials().resize(specs.empty() ? 1 : specs.size());
        for (std::size_t i = 0; i < specs.size(); ++i) AddRecord(unsigned(i), specs[i]);
    }
    ~MeshFixture() {
        for (s3d::cS3DVertexArray* array : originals)
            if (array->refCount > 0) array->Release();
        mesh->Release();
    }
    MeshFixture(const MeshFixture&) = delete;
    MeshFixture& operator=(const MeshFixture&) = delete;

    void AddRecord(unsigned index, const MeshSpec& spec) {
        const int vertexCount = int(spec.positions.size() / 3);
        auto indices = std::make_unique<std::vector<std::uint16_t>>(spec.indices);
        auto primitives = std::make_unique<std::vector<s3d::MeshPrimitive>>();
        s3d::MeshPrimitive primitive;
        primitive.type = s3d::kPrimTriangles;
        primitive.first = 0;
        primitive.count = indices->empty() ? vertexCount : int(indices->size());
        primitives->push_back(primitive);
        auto draws = std::make_unique<s3d::ModelProcessorDrawList>();
        draws->begin = primitives->data();
        draws->end = primitives->data() + primitives->size();

        auto* array = new s3d::cS3DVertexArray();
        array->format = spec.format;
        array->stride = 0x14;
        array->count = std::uint16_t(vertexCount);
        array->data.assign(std::size_t(array->count) * array->stride, 0);
        for (int v = 0; v < vertexCount; ++v) {
            std::memcpy(array->data.data() + std::size_t(v) * array->stride, &spec.positions[std::size_t(v) * 3], 12);
            std::memcpy(array->data.data() + std::size_t(v) * array->stride + 12, &spec.uvs[std::size_t(v) * 2], 8);
        }
        array->boundsMin[0] = spec.boundsMin.x;
        array->boundsMin[1] = spec.boundsMin.y;
        array->boundsMin[2] = spec.boundsMin.z;
        array->boundsMax[0] = spec.boundsMax.x;
        array->boundsMax[1] = spec.boundsMax.y;
        array->boundsMax[2] = spec.boundsMax.z;

        s3d::ModelProcessorMesh& record = s3d::MeshRecordAt(*mesh, index);
        s3d::AssignMeshVertexArray(record.vertexArray, array);   // the mesh record owns the first reference
        record.indexGroup = nullptr;
        if (!indices->empty()) {
            indexBuffers.push_back(std::move(indices));
            indexGroups.push_back(std::make_unique<const std::uint16_t*>(indexBuffers.back()->data()));
            record.indexGroup = indexGroups.back().get();
        }
        record.draws = draws.get();
        primitiveLists.push_back(std::move(primitives));
        drawLists.push_back(std::move(draws));
        originals.push_back(array);
    }
};

MeshSpec TriangleSpec() {   // one triangle in the z = 0 plane with a sizeable box
    MeshSpec spec;
    spec.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    spec.uvs = {0, 0, 1, 0, 0, 1};
    spec.indices = {0, 1, 2};
    spec.boundsMin = cS3DVector3{-1, -2, -3};
    spec.boundsMax = cS3DVector3{4, 5, 6};
    return spec;
}

// ---- tests -------------------------------------------------------------------------------------------------

void TestDefaults() {
    CHECK(sizeof(s3d::cS3DModelProcessor) == 0x80);
    CHECK(sizeof(s3d::cS3DVertexArray) == 0x40);
    CHECK(sizeof(s3d::ModelProcessorMesh) == 0x18);

    s3d::cS3DModelProcessor processor;
    CHECK(processor.Model() == nullptr);
    CHECK(processor.MeshAndMaterial() == nullptr);
    CHECK(processor.DestFormat() == 10);
    CHECK(!processor.IsReleased());
    CHECK(processor.VertexArrays().empty());
    CHECK(processor.ByteArrays().empty());
    CHECK(processor.Normals().empty());
    CHECK(processor.SortedMeshIndices().empty());
    processor.SetDestFormat(11);
    CHECK(processor.DestFormat() == 11);

    s3d::cS3DVertexArray array;
    CHECK(array.format == 2 && array.stride == 0x14 && array.count == 0 && array.refCount == 0);
    CHECK(array.boundsMin[0] == 1.0f && array.boundsMin[1] == 1.0f && array.boundsMin[2] == 1.0f);
    CHECK(array.boundsMax[0] == 0.0f && array.boundsMax[1] == 0.0f && array.boundsMax[2] == 0.0f);
    CHECK(array.AddRef() == 1);
    CHECK(array.AddRef() == 2);
    CHECK(array.Release() == 1);   // a stack object: keep the count above zero so no deleting Release runs
    array.refCount = 0;
}

void TestSetModelConvertsMeshes() {
    MeshFixture fixture({TriangleSpec()});
    s3d::cS3DVertexArray* original = fixture.originals[0];
    CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1 && original->refCount == 1);

    FakeModel model;
    model.mesh = fixture.mesh;
    {
        s3d::cS3DModelProcessor processor;
        CHECK(processor.SetModel(&model));
        CHECK(processor.Model() == &model);
        CHECK(processor.MeshAndMaterial() == fixture.mesh);
        CHECK(model.refs == 2);              // the processor holds one reference
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 2);  // the fixture and the processor
        CHECK(processor.VertexArrays().size() == 1);
        CHECK(processor.VertexArrays()[0].get() == original);
        CHECK(original->refCount == 1);      // the processor's vector (the mesh record now holds the copy)

        // the mesh record switched to the converted array
        s3d::ModelProcessorMesh& record = s3d::MeshRecordAt(*fixture.mesh, 0);
        CHECK(record.vertexArray != original);
        const s3d::cS3DVertexArray* converted = record.vertexArray;
        CHECK(converted->format == 10);
        CHECK(converted->stride == 0x18);
        CHECK(converted->count == 3);
        CHECK(converted->refCount == 1);
        CHECK(converted->boundsMin[0] == -1.0f && converted->boundsMin[2] == -3.0f);
        CHECK(converted->boundsMax[0] == 4.0f && converted->boundsMax[2] == 6.0f);
        CHECK(converted->data.size() == 3 * 0x18);

        struct Expected { float x, y, z, u, v; };
        const Expected expected[3] = {{0, 0, 0, 0, 0}, {1, 0, 0, 1, 0}, {0, 1, 0, 0, 1}};
        for (int v = 0; v < 3; ++v) {
            const std::uint8_t* recordBytes = converted->data.data() + std::size_t(v) * 0x18;
            float xyz[3];
            std::memcpy(xyz, recordBytes, sizeof xyz);
            std::uint32_t colour = 0;
            std::memcpy(&colour, recordBytes + 12, sizeof colour);
            float uv[2];
            std::memcpy(uv, recordBytes + 16, sizeof uv);
            CHECK(xyz[0] == expected[v].x && xyz[1] == expected[v].y && xyz[2] == expected[v].z);
            CHECK(colour == 0xffffffffu);
            CHECK(uv[0] == expected[v].u && uv[1] == expected[v].v);
        }

        // no normals or sort order yet
        CHECK(processor.Normals().empty());
        CHECK(processor.SortedMeshIndices().empty());

        // resetting gives the original array back and drops both references
        CHECK(!processor.SetModel(nullptr));
        CHECK(processor.Model() == nullptr);
        CHECK(processor.MeshAndMaterial() == nullptr);
        CHECK(model.refs == 1);
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1);
        CHECK(s3d::MeshRecordAt(*fixture.mesh, 0).vertexArray == original);
        CHECK(original->refCount == 1);
        CHECK(processor.VertexArrays().empty());
    }
    // destroying the processor after a reset releases nothing it no longer holds
    CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1);
    CHECK(original->refCount == 1);
    CHECK(s3d::MeshRecordAt(*fixture.mesh, 0).vertexArray == original);
}

void TestDestructorRestoresArrays() {
    MeshFixture fixture({TriangleSpec()});
    s3d::cS3DVertexArray* original = fixture.originals[0];
    FakeModel model;
    model.mesh = fixture.mesh;
    {
        s3d::cS3DModelProcessor processor;
        CHECK(processor.SetModel(&model));
        CHECK(s3d::MeshRecordAt(*fixture.mesh, 0).vertexArray != original);
        CHECK(model.refs == 2 && static_cast<TestMesh*>(fixture.mesh)->RefCount() == 2);
    }
    // the destructor returned the borrowed array and released the model and the mesh
    CHECK(model.refs == 1);
    CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1);
    CHECK(s3d::MeshRecordAt(*fixture.mesh, 0).vertexArray == original);
    CHECK(original->refCount == 1);
}

void TestSetModelReplacesModel() {
    MeshFixture first({TriangleSpec()});
    MeshSpec other = TriangleSpec();
    other.positions = {0, 0, 0, 2, 0, 0, 0, 2, 0};
    MeshFixture second({other});
    s3d::cS3DVertexArray* firstOriginal = first.originals[0];
    s3d::cS3DVertexArray* secondOriginal = second.originals[0];
    FakeModel a;
    a.mesh = first.mesh;
    FakeModel b;
    b.mesh = second.mesh;
    {
        s3d::cS3DModelProcessor processor;
        CHECK(processor.SetModel(&a));
        CHECK(s3d::MeshRecordAt(*first.mesh, 0).vertexArray != firstOriginal);
        // replacing the model returns the first mesh's array and converts the second one
        CHECK(processor.SetModel(&b));
        CHECK(processor.Model() == &b);
        CHECK(processor.MeshAndMaterial() == second.mesh);
        CHECK(a.refs == 1);
        CHECK(b.refs == 2);
        CHECK(static_cast<TestMesh*>(first.mesh)->RefCount() == 1);
        CHECK(static_cast<TestMesh*>(second.mesh)->RefCount() == 2);
        CHECK(s3d::MeshRecordAt(*first.mesh, 0).vertexArray == firstOriginal);
        CHECK(firstOriginal->refCount == 1);
        CHECK(processor.VertexArrays().size() == 1);
        CHECK(processor.VertexArrays()[0].get() == secondOriginal);
        CHECK(s3d::MeshRecordAt(*second.mesh, 0).vertexArray->count == 3);
        const float* convertedPositions =
            reinterpret_cast<const float*>(s3d::MeshRecordAt(*second.mesh, 0).vertexArray->data.data());
        CHECK(convertedPositions[6] == 2.0f);   // the second record's x (six floats per record)
        CHECK(processor.Normals().empty() && processor.SortedMeshIndices().empty());
    }
    CHECK(a.refs == 1 && b.refs == 1);
    CHECK(static_cast<TestMesh*>(first.mesh)->RefCount() == 1);
    CHECK(static_cast<TestMesh*>(second.mesh)->RefCount() == 1);
    CHECK(s3d::MeshRecordAt(*second.mesh, 0).vertexArray == secondOriginal);
}

void TestSetModelFailures() {
    // no mesh view at all: QueryInterface for the mesh interface fails
    {
        FakeModel model;
        s3d::cS3DModelProcessor processor;
        CHECK(!processor.SetModel(&model));
        CHECK(model.refs == 1);
        CHECK(processor.Model() == nullptr && processor.MeshAndMaterial() == nullptr);
    }
    // an empty mesh list
    {
        MeshFixture fixture({});
        FakeModel model;
        model.mesh = fixture.mesh;
        s3d::cS3DModelProcessor processor;
        CHECK(!processor.SetModel(&model));
        CHECK(model.refs == 1);
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1);
        CHECK(processor.Model() == nullptr && processor.MeshAndMaterial() == nullptr);
    }
    // a vertex array that is not in the accepted format drops the model and the mesh list
    {
        MeshSpec spec = TriangleSpec();
        spec.format = 10;
        MeshFixture fixture({spec});
        FakeModel model;
        model.mesh = fixture.mesh;
        s3d::cS3DModelProcessor processor;
        CHECK(!processor.SetModel(&model));
        CHECK(model.refs == 1);
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1);
        CHECK(processor.Model() == nullptr && processor.MeshAndMaterial() == nullptr);
        CHECK(s3d::MeshRecordAt(*fixture.mesh, 0).vertexArray == fixture.originals[0]);
    }
    // an animated instance with two or more frames is rejected before its mesh is queried
    {
        MeshFixture fixture({TriangleSpec()});
        FakeModel model;
        model.mesh = fixture.mesh;
        model.answersAnimated = true;
        model.frameCount = 3;
        s3d::cS3DModelProcessor processor;
        CHECK(!processor.SetModel(&model));
        CHECK(model.refs == 1);
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1);
        CHECK(processor.Model() == nullptr && processor.MeshAndMaterial() == nullptr);
    }
    // ... a single frame is a static model and passes
    {
        MeshFixture fixture({TriangleSpec()});
        FakeModel model;
        model.mesh = fixture.mesh;
        model.answersAnimated = true;
        model.frameCount = 1;
        s3d::cS3DModelProcessor processor;
        CHECK(processor.SetModel(&model));
        CHECK(processor.Model() == &model);
        CHECK(processor.MeshAndMaterial() == fixture.mesh);
        CHECK(processor.VertexArrays().size() == 1);
    }
}

void TestCreateVertexNormals() {
    // Two triangles sharing the origin: their double-area normals are (0,-1,0) with length 1 and (-3,0,0) with
    // length 3, so the origin accumulates (-3,-1,0) over a weight of 4 and normalises to (-0.75,-0.25,0).
    MeshSpec spec;
    spec.positions = {0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 0};
    spec.uvs = {0, 0, 1, 0, 0, 1, 0, 1};
    spec.indices = {0, 1, 2, 0, 2, 3};
    MeshFixture fixture({spec});
    FakeModel model;
    model.mesh = fixture.mesh;
    s3d::cS3DModelProcessor processor;
    CHECK(processor.SetModel(&model));
    processor.CreateVertexNormals();
    CHECK(processor.Normals().size() == 1);
    const std::vector<cS3DVector3>& normals = processor.Normals()[0];
    CHECK(normals.size() == 4);
    CHECK_NEAR(normals[0].x, -0.75);
    CHECK_NEAR(normals[0].y, -0.25);
    CHECK_NEAR(normals[0].z, 0.0);
    CHECK_NEAR(normals[1].x, 0.0);
    CHECK_NEAR(normals[1].y, -1.0);
    CHECK_NEAR(normals[1].z, 0.0);
    CHECK_NEAR(normals[2].x, -0.75);
    CHECK_NEAR(normals[2].y, -0.25);
    CHECK_NEAR(normals[2].z, 0.0);
    CHECK_NEAR(normals[3].x, -1.0);
    CHECK_NEAR(normals[3].y, 0.0);
    CHECK_NEAR(normals[3].z, 0.0);

    // the same triangle list addressed directly (no index buffer)
    MeshSpec direct;
    direct.positions = {0, 0, 0, 1, 0, 0, 0, 0, 1};
    direct.uvs = {0, 0, 1, 0, 0, 1};
    MeshFixture directFixture({direct});
    FakeModel directModel;
    directModel.mesh = directFixture.mesh;
    s3d::cS3DModelProcessor directProcessor;
    CHECK(directProcessor.SetModel(&directModel));
    directProcessor.CreateVertexNormals();
    CHECK(directProcessor.Normals().size() == 1);
    CHECK(directProcessor.Normals()[0].size() == 3);
    for (const cS3DVector3& normal : directProcessor.Normals()[0]) {
        CHECK_NEAR(normal.x, 0.0);
        CHECK_NEAR(normal.y, -1.0);
        CHECK_NEAR(normal.z, 0.0);
    }

    // a vertex no triangle references keeps the zero normal, and a mesh with no vertices yields empty normals
    MeshSpec stray;
    stray.positions = {0, 0, 0, 1, 0, 0, 0, 0, 1, 9, 9, 9};
    stray.uvs = {0, 0, 1, 0, 0, 1, 1, 1};
    stray.indices = {0, 1, 2};
    MeshFixture strayFixture({stray});
    FakeModel strayModel;
    strayModel.mesh = strayFixture.mesh;
    s3d::cS3DModelProcessor strayProcessor;
    CHECK(strayProcessor.SetModel(&strayModel));
    strayProcessor.CreateVertexNormals();
    CHECK(strayProcessor.Normals()[0].size() == 4);
    CHECK(strayProcessor.Normals()[0][3].x == 0.0f && strayProcessor.Normals()[0][3].y == 0.0f &&
          strayProcessor.Normals()[0][3].z == 0.0f);

    // two meshes: one normals entry per mesh, sized by that mesh's vertex count
    MeshSpec second = direct;
    MeshFixture twoFixtures({spec, second});
    FakeModel twoModel;
    twoModel.mesh = twoFixtures.mesh;
    s3d::cS3DModelProcessor twoProcessor;
    CHECK(twoProcessor.SetModel(&twoModel));
    twoProcessor.CreateVertexNormals();
    CHECK(twoProcessor.Normals().size() == 2);
    CHECK(twoProcessor.Normals()[0].size() == 4);
    CHECK(twoProcessor.Normals()[1].size() == 3);
    CHECK_NEAR(twoProcessor.Normals()[0][0].x, -0.75);
    CHECK_NEAR(twoProcessor.Normals()[1][0].y, -1.0);

    // no model at all: nothing to do
    s3d::cS3DModelProcessor empty;
    empty.CreateVertexNormals();
    CHECK(empty.Normals().empty());
}

void TestLightModel() {
    MeshFixture fixture({TriangleSpec()});
    FakeModel model;
    model.mesh = fixture.mesh;
    model.transform.rotation[0][0] = 0.0f;
    model.transform.rotation[0][1] = 1.0f;
    model.transform.rotation[1][0] = -1.0f;
    model.transform.rotation[1][1] = 0.0f;
    FakeLighting lighting;
    {
        s3d::cS3DModelProcessor processor;
        CHECK(processor.SetModel(&model));
        processor.LightModel(&lighting, false);

        // the normals were created on demand and passed with their stride
        CHECK(processor.Normals().size() == 1 && processor.Normals()[0].size() == 3);
        CHECK(lighting.calls == 1);
        CHECK(lighting.count == 3);
        CHECK(lighting.normalStride == 12);
        CHECK(lighting.colourStride == 24);
        CHECK(lighting.normals.size() == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(lighting.normals[i].x == processor.Normals()[0][i].x);
            CHECK(lighting.normals[i].y == processor.Normals()[0][i].y);
            CHECK(lighting.normals[i].z == processor.Normals()[0][i].z);
        }

        // the colours go into byte 0xc of the converted array's records
        const s3d::cS3DVertexArray* converted = s3d::MeshRecordAt(*fixture.mesh, 0).vertexArray;
        CHECK(lighting.coloursPointer == converted->data.data() + 12);

        // the model rotation is passed through, and the flip negates it
        CHECK(lighting.rotation.m[1] == 1.0f && lighting.rotation.m[3] == -1.0f);
        CHECK(lighting.rotation.m[0] == 0.0f && lighting.rotation.m[4] == 0.0f && lighting.rotation.m[8] == 1.0f);
        processor.LightModel(&lighting, true);
        CHECK(lighting.calls == 2);
        CHECK(lighting.rotation.m[1] == -1.0f && lighting.rotation.m[3] == 1.0f);
        CHECK(lighting.rotation.m[8] == -1.0f);

        // the first material is marked "lit" (0x1001660a2)
        CHECK((fixture.mesh->Material(0)[4] & 0x08) == 0x08);
        CHECK(model.transforms >= 1);
    }

    // one lighting call per mesh, each with its own colour buffer
    MeshSpec second = TriangleSpec();
    second.positions = {2, 0, 0, 3, 0, 0, 2, 1, 0};
    MeshFixture multi({TriangleSpec(), second});
    FakeModel multiModel;
    multiModel.mesh = multi.mesh;
    FakeLighting multiLighting;
    s3d::cS3DModelProcessor multiProcessor;
    CHECK(multiProcessor.SetModel(&multiModel));
    multiProcessor.LightModel(&multiLighting, false);
    CHECK(multiLighting.calls == 2);
    CHECK(multiProcessor.Normals().size() == 2);
    CHECK(multiProcessor.Normals()[1].size() == 3);
    CHECK(multi.mesh->Material(0)[4] == 0x08);
}

void TestSortMeshes() {
    // three meshes with centres (0,0,0), (5,0,0), (-5,0,0)
    MeshSpec a = TriangleSpec();
    MeshSpec b = TriangleSpec();
    MeshSpec c = TriangleSpec();
    a.boundsMin = cS3DVector3{-1, -1, -1};
    a.boundsMax = cS3DVector3{1, 1, 1};
    b.boundsMin = cS3DVector3{4, -1, -1};
    b.boundsMax = cS3DVector3{6, 1, 1};
    c.boundsMin = cS3DVector3{-6, -1, -1};
    c.boundsMax = cS3DVector3{-4, 1, 1};
    MeshFixture fixture({a, b, c});
    FakeModel model;
    model.mesh = fixture.mesh;
    s3d::cS3DModelProcessor processor;
    CHECK(processor.SetModel(&model));

    // +x: b (5) then a (0) then c (-5)
    processor.SortMeshes(cS3DVector3{1.0f, 0.0f, 0.0f});
    CHECK(processor.SortedMeshIndices().size() == 3);
    CHECK(processor.SortedMeshIndices()[0] == 1);
    CHECK(processor.SortedMeshIndices()[1] == 0);
    CHECK(processor.SortedMeshIndices()[2] == 2);

    // -x reverses it
    processor.SortMeshes(cS3DVector3{-1.0f, 0.0f, 0.0f});
    CHECK(processor.SortedMeshIndices()[0] == 2);
    CHECK(processor.SortedMeshIndices()[1] == 0);
    CHECK(processor.SortedMeshIndices()[2] == 1);

    // equal depths are inserted before the equal entry, so the list ends up reversed
    processor.SortMeshes(cS3DVector3{0.0f, 1.0f, 0.0f});
    CHECK(processor.SortedMeshIndices() == (std::vector<int>{2, 1, 0}));

    // a NaN depth is never "greater" and takes the same first position
    processor.SortMeshes(cS3DVector3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
    CHECK(processor.SortedMeshIndices() == (std::vector<int>{2, 1, 0}));

    // no mesh: nothing happens, the list stays as it was
    s3d::cS3DModelProcessor empty;
    empty.SortMeshes(cS3DVector3{1.0f, 0.0f, 0.0f});
    CHECK(empty.SortedMeshIndices().empty());
}

void TestAssignment() {
    MeshFixture fixture({TriangleSpec()});
    FakeModel model;
    model.mesh = fixture.mesh;
    {
        s3d::cS3DModelProcessor source;
        CHECK(source.SetModel(&model));
        source.SetDestFormat(11);
        source.CreateVertexNormals();
        source.SortMeshes(cS3DVector3{0.0f, 0.0f, 1.0f});
        CHECK(source.Normals().size() == 1 && source.SortedMeshIndices().size() == 1);

        s3d::cS3DModelProcessor target;
        target = source;
        CHECK(target.Model() == &model);
        CHECK(target.MeshAndMaterial() == fixture.mesh);
        CHECK(target.DestFormat() == 11);
        CHECK(model.refs == 3);              // the test, the source and the target
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 3);  // the fixture and both processors
        CHECK(target.VertexArrays().size() == 1);
        CHECK(target.VertexArrays()[0].get() == source.VertexArrays()[0].get());
        CHECK(target.Normals().size() == 1 && target.Normals()[0].size() == 3);
        CHECK(target.SortedMeshIndices() == source.SortedMeshIndices());
        CHECK(target.IsReleased() == source.IsReleased());

        // self-assignment keeps everything intact
        s3d::cS3DModelProcessor& alias = target;
        target = alias;
        CHECK(target.Model() == &model);
        CHECK(model.refs == 3);
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 3);
        CHECK(target.Normals().size() == 1);

        // assigning a default processor over it releases the references
        s3d::cS3DModelProcessor empty;
        target = empty;
        CHECK(target.Model() == nullptr && target.MeshAndMaterial() == nullptr);
        CHECK(model.refs == 2);
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 2);
        CHECK(target.DestFormat() == 10);
        CHECK(target.VertexArrays().empty() && target.Normals().empty() && target.SortedMeshIndices().empty());

        // the copy constructor mirrors the assignment
        s3d::cS3DModelProcessor copied(source);
        CHECK(copied.Model() == &model && copied.MeshAndMaterial() == fixture.mesh);
        CHECK(copied.DestFormat() == 11 && copied.Normals().size() == 1);
        CHECK(model.refs == 3 && static_cast<TestMesh*>(fixture.mesh)->RefCount() == 3);
    }
    CHECK(model.refs == 1);
    CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1);
}

void TestReleaseModel() {
    MeshFixture fixture({TriangleSpec()});
    s3d::cS3DVertexArray* original = fixture.originals[0];
    FakeModel model;
    model.mesh = fixture.mesh;
    FakeModel other;   // declared before the processor: it must outlive the processor that references it
    other.mesh = fixture.mesh;
    {
        s3d::cS3DModelProcessor processor;
        CHECK(processor.SetModel(&model));
        CHECK(processor.VertexArrays().size() == 1);
        CHECK(model.refs == 2 && static_cast<TestMesh*>(fixture.mesh)->RefCount() == 2);

        processor.ReleaseModel();
        CHECK(processor.Model() == nullptr);
        CHECK(processor.IsReleased());
        CHECK(model.refs == 1);             // the processor released the model
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1); // ... and the mesh (the fixture keeps it alive for the destructor)
        CHECK(processor.MeshAndMaterial() == fixture.mesh);   // QUIRK: the pointer is not cleared
        CHECK(processor.VertexArrays().size() == 1);          // QUIRK: the borrowed arrays are not returned

        // SetModel after a release skips the second mesh release (the flag) and works again
        CHECK(processor.SetModel(&other));
        CHECK(!processor.IsReleased());
        CHECK(processor.Model() == &other);
        CHECK(other.refs == 2);
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 2);
        CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 2);
        CHECK(s3d::MeshRecordAt(*fixture.mesh, 0).vertexArray != original);
    }
    // both models are gone, the mesh list is back to the fixture's reference and the original array is in place
    CHECK(model.refs == 1);
    CHECK(static_cast<TestMesh*>(fixture.mesh)->RefCount() == 1);
    CHECK(s3d::MeshRecordAt(*fixture.mesh, 0).vertexArray == original);
    CHECK(original->refCount == 1);
}

}  // namespace

int main() {
    TestDefaults();
    TestSetModelConvertsMeshes();
    TestDestructorRestoresArrays();
    TestSetModelReplacesModel();
    TestSetModelFailures();
    TestCreateVertexNormals();
    TestLightModel();
    TestSortMeshes();
    TestAssignment();
    TestReleaseModel();
    std::printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
