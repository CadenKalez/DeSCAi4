#include <cstdio>
#include <string>
#include <vector>

#include "sc4/PlaceableObject.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Model final : cIS3DModelInstance {
    int refs = 0; void* owner = reinterpret_cast<void*>(1);
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void SetOwner(void* o) override { owner = o; }
};
struct Host final : IPlaceableHost {
    std::vector<std::string> log;
    bool AddModelToRenderer(cIS3DModelInstance* m) override { log.push_back("add" + std::to_string(reinterpret_cast<std::uintptr_t>(m) & 0)); return true; }
    void RemoveModelFromRenderer(cIS3DModelInstance*) override { log.push_back("rm"); }
    void RemoveOverlay(std::uint32_t id) override { log.push_back("rmov" + std::to_string(id)); }
    void SetOverlayVisible(std::uint32_t id, bool v) override { log.push_back("ov" + std::to_string(id) + (v ? "+" : "-")); }
};
}  // namespace

int main() {
    Host host; SetPlaceableHost(&host);
    auto* obj = new cSC43DPlaceableObject();
    obj->AddRef();
    void* out = nullptr;
    CHECK(obj->QueryInterface(kGZIID_cISC43DPlaceableObject, &out) && out == static_cast<cISC43DPlaceableObject*>(obj));
    obj->Release();
    CHECK(!obj->QueryInterface(0x1234, &out));

    Model a, b, c;
    CHECK(obj->GetModelCount() == 0 && !obj->IsVisible());
    obj->AddModel(&a); obj->AddModel(&b); obj->AddModel(&c);
    obj->ResizeModelInstanceArray(8);                                          // keeps the models
    CHECK(obj->GetModelCount() == 3 && a.refs == 1 && b.refs == 1 && host.log.empty());
    cIS3DModelInstance* got[2]; std::uint32_t n = 2;
    CHECK(obj->GetModels(got, n) == 2 && n == 2 && got[0] == &a && got[1] == &b);
    n = 10; cIS3DModelInstance* all[4];
    CHECK(obj->GetModels(all, n) == 3 && all[2] == &c);

    obj->SetVisibility(true);                          // models enter the renderer
    CHECK(obj->IsVisible() && host.log.size() == 3 && host.log[0] == "add0");
    obj->SetVisibility(true);
    CHECK(host.log.size() == 3);
    host.log.clear();
    CHECK(obj->RemoveModel(&a) && a.refs == 0 && obj->GetModelCount() == 2);
    n = 4; obj->GetModels(all, n);
    CHECK(all[0] == &c && all[1] == &b);                // the last model took the first slot
    CHECK(host.log.size() == 1 && host.log[0] == "rm");
    CHECK(obj->RemoveModel(&b) && obj->GetModelCount() == 1);
    CHECK(!obj->RemoveModel(&a));

    host.log.clear();
    obj->AddOverlay(7); obj->AddOverlay(9);
    CHECK(host.log.size() == 2 && host.log[0] == "ov7+");
    host.log.clear();
    obj->SetOverlaysVisible(false);
    CHECK(host.log.size() == 2 && host.log[0] == "ov7-" && host.log[1] == "ov9-");
    host.log.clear();
    obj->SetOverlaysVisible(false);
    CHECK(host.log.empty());
    obj->SetVisibility(false);
    CHECK(!obj->IsVisible() && host.log.size() == 3);   // two overlay updates then the model removal

    obj->SetZoomLevelsCreatedFor(0x1f); obj->SetRotationsCreatedFor(0xf);
    CHECK(obj->GetZoomLevelsCreatedFor() == 0x1f && obj->GetRotationsCreatedFor() == 0xf && !obj->IsVisible());
    host.log.clear();
    obj->RemoveAll();
    CHECK(obj->GetModelCount() == 0 && c.refs == 0 && c.owner == nullptr);
    CHECK(host.log.size() == 2 && host.log[0] == "rmov7" && obj->GetZoomLevelsCreatedFor() == 0 && obj->GetRotationsCreatedFor() == 0);

    Model d; obj->AddModel(&d);
    CHECK(obj->Release() == 0 && d.refs == 0);          // the final release lets go of the models
    std::printf(failures ? "FAILED\n" : "sc4_placeableobject ok\n");
    return failures != 0;
}
