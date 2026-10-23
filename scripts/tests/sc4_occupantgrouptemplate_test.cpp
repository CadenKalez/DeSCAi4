#include <cstdio>

#include "sc4/OccupantGroupTemplate.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Host final : IOccupantGroupHost {
    double value = 0.5;
    int draws = 0;
    double RandomDouble() override { ++draws; return value; }
};
cSC4Occupant* NewOccupant(std::initializer_list<std::uint32_t> propertyGroups) {
    auto* o = new cSC4Occupant();
    o->AddRef();
    if (propertyGroups.size()) {
        std::vector<std::uint32_t> ids(propertyGroups);
        cRZVariant v;
        v.RefUint32(ids.data(), static_cast<std::uint32_t>(ids.size()));
        o->AsPropertyHolder()->AddProperty(kPropertyOccupantGroups, &v, true);
    }
    return o;
}
}  // namespace

int main() {
    Host host;
    SetOccupantGroupHost(&host);

    // ---- group filter
    {
        auto* member = new cSC4Occupant(); member->AddRef(); member->AddOccupantGroup(0x1234);
        auto* other = new cSC4Occupant(); other->AddRef(); other->AddOccupantGroup(0x99);
        cSC4OccupantGroupOccupantFilter f(0x1234u);
        CHECK(f.GroupIds().size() == 1 && f.IsOccupantIncluded(member) && !f.IsOccupantIncluded(other) && !f.IsOccupantIncluded(nullptr));
        f.AddGroupId(0); f.AddGroupId(0x99);
        CHECK(f.GroupIds().size() == 2 && f.IsOccupantIncluded(other));
        cSC4OccupantGroupOccupantFilter none(0u);
        CHECK(none.GroupIds().empty() && !none.IsOccupantIncluded(member));
        f.SetGroupIdVector({7});
        f.SetGroupIdVector(f.GroupIds());   // self assignment
        CHECK(f.GroupIds().size() == 1 && !f.IsOccupantIncluded(member));
        cSC4OccupantGroupOccupantFilter fromVector(std::vector<std::uint32_t>{0x1234, 5});
        CHECK(fromVector.IsOccupantIncluded(member) && fromVector.GetGZCLSID() == kGZCLSID_cSC4OccupantFilter);
        member->Release(); other->Release();
    }

    // ---- template hierarchy
    cSC4OccupantGroupTemplate root, mid, leaf;
    root.groupId = 0x10; root.classId = 0;
    mid.parent = &root; mid.groupId = 0x20; mid.classId = 0xAB;
    leaf.parent = &mid; leaf.groupId = 0x30;
    CHECK(leaf.IsOccupantGroup(0x30) && leaf.IsOccupantGroup(0x20) && leaf.IsOccupantGroup(0x10) && !leaf.IsOccupantGroup(0x99) && !root.IsOccupantGroup(0x20));
    CHECK(leaf.GetClassID() == 0xAB && root.GetClassID() == 0 && mid.GetClassID() == 0xAB);
    CHECK(leaf.GetGroupHierarchyDepth() == 3 && root.GetGroupHierarchyDepth() == 1);
    {
        cRZVariant& ids = leaf.GetGroupIDVariant();
        CHECK(ids.GetCount() == 3 && ids.RefUint32()[0] == 0x30 && ids.RefUint32()[1] == 0x20 && ids.RefUint32()[2] == 0x10);
        mid.groupId = 0;   // only filled while empty: the cached chain stays
        CHECK(leaf.GetGroupIDVariant().GetCount() == 3);
        mid.groupId = 0x20;
        cSC4OccupantGroupTemplate lonely;
        CHECK(lonely.GetGroupIDVariant().GetCount() == 0);   // no ids: returned empty
    }
    leaf.animOffsets["walk"] = 12;
    std::uint32_t offset = 0;
    CHECK(leaf.GetAnimOffset(cRZString("walk"), offset) && offset == 12 && !leaf.GetAnimOffset(cRZString("run"), offset));
    tKeyEffect effect;
    leaf.keyEffects[3].fx.AssignFromCString("boost");
    leaf.keyEffects[3].bone.AssignFromCString("zoom");
    leaf.keyEffects[3].controller.AssignFromCString("flame");
    leaf.keyEffects[3].value = 42;
    CHECK(leaf.GetKeyEffect(3, effect) && std::string(effect.fx.ToChar()) == "boost" && std::string(effect.bone.ToChar()) == "zoom" &&
          std::string(effect.controller.ToChar()) == "flame" && effect.value == 42 && !leaf.GetKeyEffect(4, effect));

    // ---- inclusion by the occupant's group property
    {
        auto* in = NewOccupant({0x05, 0x30});
        auto* out = NewOccupant({0x05});
        auto* bare = NewOccupant({});
        CHECK(leaf.IsOccupantIncluded(in) && leaf.Matches(in) && !leaf.IsOccupantIncluded(out) && !leaf.IsOccupantIncluded(bare));
        CHECK(host.draws == 0);
        leaf.chance_ = 0.4f;
        host.value = 0.39;
        CHECK(leaf.IsOccupantIncluded(in) && host.draws == 1);
        host.value = 0.9;
        CHECK(!leaf.IsOccupantIncluded(in) && leaf.SpawnChance() == 0.4f);
        leaf.chance_ = 1.0f;
        cSC4OccupantGroupTemplate noId;
        CHECK(!noId.IsOccupantIncluded(in));
        // controller templates through the manager seam
        tControllerTemplate t;
        leaf.controllerTemplates_.push_back(&t);
        IOccupantGroupTemplate* seam = &leaf;
        CHECK(seam->ControllerTemplates().size() == 1 && seam->ControllerTemplates().front() == &t);
        in->Release(); out->Release(); bare->Release();
    }
    std::printf(failures ? "FAILED\n" : "sc4_occupantgrouptemplate OK\n");
    return failures ? 1 : 0;
}
