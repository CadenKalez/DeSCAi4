#include <cstdio>
#include <cstring>

#include "rz/cRZString.h"
#include "s3d/MaterialManager.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
using namespace s3d;

int main() {
    static_assert(sizeof(cS3DMaterial) == 0x50, "record size");
    static_assert(offsetof(cS3DMaterial, layers) == 0x10, "layers at +0x10");
    cS3DMaterialManager manager(0x1234, 5);
    cS3DResMaterial res{};
    res.flags = 0x0021; res.field4 = 4; res.field5 = 5; res.field6 = 6; res.field7 = 7; res.field9 = 9; res.layerCount = 2;
    res.layers[0].textureId = 111; std::memcpy(res.layers[0].samplerBytes, "\x01\x02\x03\x04", 4);
    res.layers[1].textureId = 222; std::memcpy(res.layers[1].samplerBytes, "\x05\x06\x07\x08", 4);
    cS3DMaterial* m = nullptr;
    CHECK(manager.CreateMaterial(res, &m) && m);
    CHECK(m->state == 1 && m->refCount == 1 && m->flags == (0x0021 | 0x180));
    CHECK(m->b7 == 4 && m->b6 == 5 && m->b9 == 6 && m->b10 == 7 && m->b8 == 9 && m->layerCount == 2);
    CHECK(m->layers[0].textureId == 111 && m->layers[0].sampler[0] == 1 && m->layers[0].sampler[3] == 4 && m->layers[0].used == 1);
    CHECK(m->layers[1].textureId == 222 && m->layers[1].sampler[2] == 7 && m->layers[1].pad15 == 0 && m->layers[1].scale == 1.0f);
    delete m;
    res.layerCount = 1;
    CHECK(manager.CreateMaterial(res, &m) && m->layerCount == 1 && m->layers[0].used == 1 && m->layers[1].used == 0xff);
    CHECK(m->layers[1].sampler[0] == 3 && m->layers[1].sampler[2] == 1 && m->layers[1].textureId == 0);   // defaults kept
    delete m;
    res.layerCount = 0;
    CHECK(manager.CreateMaterial(res, &m) && m->layers[0].used == 0xff && m->layers[1].used == 0xff);
    delete m;
    res.layerCount = 5;   // clamped to the two layers a record holds
    CHECK(manager.CreateMaterial(res, &m) && m->layerCount == 5 && m->layers[1].textureId == 222);
    delete m;

    void* variant = nullptr;
    CHECK(!manager.CreateMaterialInstance(1u, &variant) && !manager.CreateMaterialInstance("x", &variant));
    CHECK(!manager.CreateMaterialVariant(nullptr, 0, nullptr));
    cRZString stats;
    manager.GetStatsString(stats);
    CHECK(std::string(stats.ToChar()) == "Nothing to report");
    void* iface = nullptr;
    CHECK(manager.QueryInterface(kGZIID_cIS3DMaterialManager, &iface) && iface == &manager);
    CHECK(manager.QueryInterface(kGZIID_cIGZSystemService, &iface) && iface == static_cast<cIGZSystemService*>(&manager));
    CHECK(!manager.QueryInterface(0x1111, &iface));
    CHECK(manager.GetServiceID() == 0x1234 && manager.GetServicePriority() == 5);

    auto* tex = new cS3DFSHTexture;
    tex->AddRef();
    void* p = nullptr;
    CHECK(tex->QueryInterface(kGZIID_cIS3DFSHTexture, &p) && p == tex);
    CHECK(tex->QueryInterface(kGZIID_cIGZPersistResource, &p) && p == static_cast<cIGZPersistResource*>(tex));
    CHECK(!tex->QueryInterface(1, &p) && p == nullptr);
    CHECK(tex->GetBufInfo() != nullptr && !tex->GetBufInfo()->ready);
    tex->SetPixelBuffer(new std::uint8_t[16]);
    CHECK(tex->GetBufInfo()->fields[4] != 0);
    CHECK(tex->GetRefCount() == 3);   // the AddRef plus the two successful queries
    tex->Release(); tex->Release(); tex->Release();   // the last one deletes
    std::printf(g_fail ? "FAILED %d\n" : "s3d_materialmanager OK\n", g_fail);
    return g_fail ? 1 : 0;
}
