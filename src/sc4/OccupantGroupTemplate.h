// cSC4OccupantGroupTemplate and cSC4OccupantGroupOccupantFilter — occupant filters by occupant group (8 + 11 functions:
// group filter 0x1000a8cf8..0x1000a938e, group template 0x1001ddea6..0x1001de4b0).
//
// cSC4OccupantGroupOccupantFilter (+0x18 vector<u32> group ids, base cSC4OccupantFilter): includes an occupant that belongs to any listed
// group (occupant vslot 0xb0 IsOccupantGroup); id 0 is never added.
//
// cSC4OccupantGroupTemplate — one occupant group as the automata scripts describe it: a group id, an optional parent template (groups
// form a hierarchy), a class id, a spawn chance and the controller templates attached to the group. Layout: +0x18 parent, +0x20 group id,
// +0x24 class id, +0x28 cRZVariant (the id chain, built lazily), +0x40 chance (1.0), +0x48 list<tControllerTemplate*>, +0x60
// map<string,u32> animation offsets, +0x88 list<resource key>, +0xa0 two u16 (0xfffe), +0xa8 list<u32>, +0xc0 list<cRZString>, +0xe0/+0xe8
// two heap arrays, +0xf8 map<u8, tKeyEffect> (player drive key effects). It also serves as the IOccupantGroupTemplate of the controller
// manager (Matches = IsOccupantIncluded, SpawnChance = chance).
// IsOccupantIncluded: the occupant lists its groups in property 0xaa1dd396; the template matches when its own id is among them and, for a
// chance below 1, a random draw (cRZRandom seeded from the clock) is <= the chance.
// Quirks kept: the filter constructor with a single id pushes that id only when non-zero; GetGroupIDVariant only fills the variant while it
// is empty and returns it even when no id was found; a template without group id never includes anything.
#pragma once
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"
#include <memory>

#include "sc4/AutomataControllerManager.h"
#include "sc4/AutomataDriveParams.h"
#include "sc4/OccupantManager.h"

namespace sc4 {

class IOccupantGroupHost {
public:
    virtual ~IOccupantGroupHost() = default;
    virtual double RandomDouble() = 0;   // cRZRandom(0xffffffff).RandomDoubleUniform()
};
IOccupantGroupHost* OccupantGroupHost();
void SetOccupantGroupHost(IOccupantGroupHost* host);

class cSC4OccupantGroupOccupantFilter : public cSC4OccupantFilter {
public:
    explicit cSC4OccupantGroupOccupantFilter(std::uint32_t groupId);             // @ 0x1000a8cf8
    explicit cSC4OccupantGroupOccupantFilter(const std::vector<std::uint32_t>& groups);   // @ 0x1000a8d9e (0x1000a8e42 twin)
    ~cSC4OccupantGroupOccupantFilter() override = default;                        // @ 0x1000a932c (deleting 0x1000a938e)
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;                    // @ 0x1000a8e4c
    void AddGroupId(std::uint32_t groupId);                                       // @ 0x1000a8e94
    void SetGroupIdVector(const std::vector<std::uint32_t>& groups);              // @ 0x1000a8eca
    const std::vector<std::uint32_t>& GroupIds() const { return groups_; }

private:
    std::vector<std::uint32_t> groups_;   // +0x18
};

// tKeyEffect: what a player-drive key does (three script strings and a value).
struct tKeyEffect {
    cRZString fx, bone, controller;       // +0x08, +0x30, +0x58 (script keys "fx", "bone", "controller")
    std::uint32_t value = 0;              // +0x78 (flags: 1 hold, 2 one_shot, 4 hard_transition, 8 override, 0x10 has fx, 0x20 has controller)
};

class cSC4OccupantGroupTemplate : public cSC4OccupantFilter, public IOccupantGroupTemplate {
public:
    cSC4OccupantGroupTemplate() = default;                                        // @ 0x1001de4b0
    ~cSC4OccupantGroupTemplate() override;                                        // @ 0x1001ddea6 (0x1001ddfc4 / 0x1001ddfce)

    bool IsOccupantIncluded(cISC4Occupant* occupant) override;                    // @ 0x1001de152
    bool IsOccupantGroup(std::uint32_t group) const;                              // @ 0x1001ddffc (walks the parent chain)
    bool GetAnimOffset(const cRZString& name, std::uint32_t& offset) const;       // @ 0x1001de014
    std::uint32_t GetClassID() const;                                             // @ 0x1001de042 (first non-zero class id up the chain)
    cRZVariant& GetGroupIDVariant();                                              // @ 0x1001de05a
    bool GetKeyEffect(std::uint8_t command, tKeyEffect& out) const;               // @ 0x1001de222
    int GetGroupHierarchyDepth() const;                                           // @ 0x1001de29a

    // IOccupantGroupTemplate (controller manager seam)
    bool Matches(cISC4Occupant* occupant) override { return IsOccupantIncluded(occupant); }
    float SpawnChance() override { return chance_; }
    const std::list<tControllerTemplate*>& ControllerTemplates() override { return controllerTemplates_; }

    // script-side data
    cSC4OccupantGroupTemplate* parent = nullptr;                // +0x18
    std::uint32_t groupId = 0;                                  // +0x20
    std::uint32_t classId = 0;                                  // +0x24
    float chance_ = 1.0f;                                       // +0x40
    std::list<tControllerTemplate*> controllerTemplates_;       // +0x48
    std::unordered_map<std::string, std::uint32_t> animOffsets; // +0x60
    std::list<cGZPersistResourceKey> resourceKeys;              // +0x88 ("models")
    std::int16_t selfRef = -2, parentRef = -2;                  // +0xa0, +0xa2: Lua registry references while the groups are being linked (-2 = none)
    std::list<std::uint32_t> trailerGroupIds;                   // +0xa8 (resolved from trailerNames by LinkOccupantGroups)
    std::list<cRZString> trailerNames;                          // +0xc0 ("trailers")
    float kphMin = 0.0f, kphMax = 0.0f;                         // +0xd8, +0xdc ("kph")
    std::unique_ptr<tAutomataDriveParams> driveParams;          // +0xe0 (airplane / helicopter / WatercraftDriveParams)
    std::unique_ptr<tAutomataPlayerDriveParams> playerDriveParams;   // +0xe8 ("playerdrive")
    std::uint32_t csiImage = 0, sourceBuilding = 0;             // +0xf0, +0xf4 ("csi_image", "source_building", hex strings)
    std::unordered_map<std::uint8_t, tKeyEffect> keyEffects;    // +0xf8

private:
    cRZVariant groupVariant_;    // +0x28
};

}  // namespace sc4
