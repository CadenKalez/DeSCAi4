// cSC4Occupant — the base class of everything placed in the city (buildings, props, lots, network
// pieces, flora...). 42 functions at 0x1000bc5bc..0x1000bd546.
//
// Object layout (0x60 bytes): +0x00 cISC4Occupant vptr, +0x08 cIGZSerializable vptr, +0x10
// cSCExemplarPropertyHolder (refcount at +0x18), +0x38 cISCExemplarPropertyHolder vptr, +0x48 type id,
// +0x4c occupant-manager bounding box cells (4 bytes, 0xff = not set), +0x50 removal counters
// (low byte = flag 2 count, high byte = flag 1 count), +0x52 state (bit0 initialized, bit1 loaded
// from a save, high nibble highlight), +0x53 flags (bit 2 visible; saved & 0xdd), +0x58 3D placeable.
#pragma once
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZSerializable.h"
#include "sc4/DBSegment.h"
#include "sc4/Exemplar.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4Occupant = 0x26bb767e;
// The constant is also declared by AutomataManager.h (same value); guard it so a translation unit can include both.
#ifndef SC4_GZIID_CISC4BuildingOccupant_DEFINED
#define SC4_GZIID_CISC4BuildingOccupant_DEFINED
constexpr std::uint32_t kGZIID_cISC4BuildingOccupant = 0x87dda3a9;
#endif
constexpr std::uint32_t kGZCLSID_cSC4Occupant = 0xa9bc9ab6;
constexpr std::uint32_t kOccupantDefaultType = 0x74758925;
#ifndef SC4_kPropertyOccupantGroups_DEFINED
#define SC4_kPropertyOccupantGroups_DEFINED
constexpr std::uint32_t kPropertyOccupantGroups = 0xaa1dd396;
#endif

// messages posted by occupants
constexpr std::uint32_t kMessageOccupantInitialized = 0x99ef1140;
constexpr std::uint32_t kMessageOccupantShutdown = 0x99ef1141;
constexpr std::uint32_t kMessageOccupantInserted = 0x99ef1142;           // by the occupant manager
constexpr std::uint32_t kMessageOccupantRemovedFromManager = 0x99ef1143;
constexpr std::uint32_t kMessageOccupantChanged = 0x99ef1145;
constexpr std::uint32_t kMessageOccupantRemoved = 0x99ef1146;
constexpr std::uint32_t kMessageOccupantHighlight = 0xe6e85114;
constexpr std::uint32_t kMessageOccupantGroupAdded = 0xebff18a3;

// Occupant flag bits
constexpr std::uint32_t kOccupantFlagVisible = 4;

// A renderable model (cIS3DModelInstance): only the owner reset the placeable object performs is needed here.
class cIS3DModelInstance : public cIGZUnknown {
public:
    virtual void Slot3() {}
    virtual void SetOwner(void* owner) = 0;     // [4] (0x20): cleared when the placeable object lets go
};

// cISC43DPlaceableObject (iid 0xc6bb5efa): the set of 3D models (and terrain overlays) one occupant shows.
constexpr std::uint32_t kGZIID_cISC43DPlaceableObject = 0xc6bb5efa;
class cISC43DPlaceableObject : public cIGZUnknown {
public:
    virtual int GetModelCount() = 0;                                              // [3]
    virtual std::uint32_t GetModels(cIS3DModelInstance** out, std::uint32_t& count) = 0;   // [4]
    virtual bool AddModel(cIS3DModelInstance* model) = 0;                         // [5]
    virtual bool RemoveModel(cIS3DModelInstance* model) = 0;                      // [6]
    virtual bool AddOverlay(std::uint32_t overlayId) = 0;                         // [7]
    virtual void SetVisibility(bool visible) = 0;                                 // [8] in the renderer or not
    virtual void SetOverlaysVisible(bool visible) = 0;                            // [9]
    virtual bool IsVisible() const = 0;                                           // [10]
    virtual void RemoveAll() = 0;                                                 // [11]
    virtual std::uint32_t GetZoomLevelsCreatedFor() const = 0;                    // [12]
    virtual void SetZoomLevelsCreatedFor(std::uint32_t zoomLevels) = 0;           // [13]
    virtual std::uint32_t GetRotationsCreatedFor() const = 0;                     // [14]
    virtual void SetRotationsCreatedFor(std::uint32_t rotations) = 0;             // [15]
};
using PlaceableObjectFactory = std::function<cISC43DPlaceableObject*()>;   // returns one reference
void SetPlaceableObjectFactory(PlaceableObjectFactory factory);            // cSC43DPlaceableObject::new

struct SC4CellRect { std::int32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0; };      // SC4Rect<int> (min/max cells)

class cISC4Occupant : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                   // [3]
    virtual bool Shutdown() = 0;                                                               // [4]
    virtual bool IsInitialized() const = 0;                                                    // [5]
    virtual cISCPropertyHolder* AsPropertyHolder() = 0;                                        // [6]
    virtual std::uint32_t GetType() const = 0;                                                 // [7]
    virtual void GetPosition(float* xyz) = 0;                                                  // [8]
    virtual void SetPosition(const float* xyz) = 0;                                            // [9]
    virtual void GetBoundingBox(float* minXyz, float* maxXyz) = 0;                             // [10]
    virtual void GetBoundingCityCells(SC4CellRect& rect) = 0;                                  // [11]
    virtual void SetRemovalFlags(int flags) = 0;                                               // [12]
    virtual void UnsetRemovalFlags(int flags) = 0;                                             // [13]
    virtual bool CanRemove(int flags) = 0;                                                     // [14]
    virtual void PostOccupantMessage(std::uint32_t message, std::uint32_t data) = 0;           // [15]
    virtual std::uint32_t GetHighlight() const = 0;                                            // [16]
    virtual void SetHighlight(int highlight, bool postNow) = 0;                                // [17]
    virtual void SetVisibility(bool visible, bool post) = 0;                                   // [18]
    virtual cISC43DPlaceableObject* GetPlaceableObject() const = 0;                            // [19]
    virtual cISC43DPlaceableObject* GetOrCreatePlaceableObject() = 0;                          // [20]
    virtual void SetPlaceableObject(cISC43DPlaceableObject* object) = 0;                       // [21]
    virtual bool IsOccupantGroup(std::uint32_t group) const = 0;                               // [22]
    virtual void AddOccupantGroup(std::uint32_t group) = 0;                                    // [23]
    virtual bool GetOccupantGroups(std::set<std::uint32_t>& groups) const = 0;                 // [24]
    virtual bool GetOccupantManagerBBox(std::uint8_t* cells) const = 0;                        // [25]
    virtual void SetOccupantManagerBBox(const std::uint8_t* cells) = 0;                        // [26]
    virtual bool GetLotTag(std::uint32_t& tag) const = 0;                                      // [27]
    virtual void SetLotTag(std::uint32_t tag) = 0;                                             // [28]
    virtual void SetFlag(std::uint32_t flag) = 0;                                              // [29]
    virtual void SetAllFlags(std::uint32_t flags) = 0;                                         // [30]
    virtual void ClearFlag(std::uint32_t flag) = 0;                                            // [31]
    virtual bool IsFlagSet(std::uint32_t flag) = 0;                                            // [32]
    virtual std::uint32_t GetFlags() = 0;                                                      // [33]
    virtual ~cISC4Occupant() = default;                                                        // [34],[35]
};

class cSC4Occupant : public cISC4Occupant, public cIGZSerializable, public cSCExemplarPropertyHolder {
public:
    cSC4Occupant() = default;
    ~cSC4Occupant() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cSCExemplarPropertyHolder::AddRef(); }
    std::uint32_t Release() override { return cSCExemplarPropertyHolder::Release(); }

    bool Init() override;
    bool Shutdown() override;
    bool IsInitialized() const override { return (state_ & 1) != 0; }
    cISCPropertyHolder* AsPropertyHolder() override { return static_cast<cSCExemplarPropertyHolder*>(this); }
    std::uint32_t GetType() const override { return type_; }
    void GetPosition(float* xyz) override { xyz[0] = xyz[1] = xyz[2] = 0.0f; }
    void SetPosition(const float*) override {}
    void GetBoundingBox(float* minXyz, float* maxXyz) override;
    void GetBoundingCityCells(SC4CellRect& rect) override;
    void SetRemovalFlags(int flags) override;
    void UnsetRemovalFlags(int flags) override;
    bool CanRemove(int flags) override;
    void PostOccupantMessage(std::uint32_t message, std::uint32_t data) override;
    std::uint32_t GetHighlight() const override { return state_ >> 4; }
    void SetHighlight(int highlight, bool postNow) override;
    void SetVisibility(bool visible, bool post) override;
    cISC43DPlaceableObject* GetPlaceableObject() const override { return placeable_; }
    cISC43DPlaceableObject* GetOrCreatePlaceableObject() override;
    void SetPlaceableObject(cISC43DPlaceableObject* object) override;
    bool IsOccupantGroup(std::uint32_t group) const override;
    void AddOccupantGroup(std::uint32_t group) override;
    bool GetOccupantGroups(std::set<std::uint32_t>& groups) const override;
    bool GetOccupantManagerBBox(std::uint8_t* cells) const override;
    void SetOccupantManagerBBox(const std::uint8_t* cells) override;
    bool GetLotTag(std::uint32_t&) const override { return false; }
    void SetLotTag(std::uint32_t) override {}
    void SetFlag(std::uint32_t flag) override;
    void SetAllFlags(std::uint32_t flags) override { flags_ = static_cast<std::uint8_t>(flags); }
    void ClearFlag(std::uint32_t flag) override;
    bool IsFlagSet(std::uint32_t flag) override { return (flags_ & flag) == flag; }
    std::uint32_t GetFlags() override { return flags_; }

    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4Occupant; }

    // static helpers on exemplar keys (the occupant's groups come from its exemplar)
    static bool IsOccupantGroup(const cGZPersistResourceKey& exemplarKey, std::uint32_t group);
    static void GetOccupantGroups(const cGZPersistResourceKey& exemplarKey, std::vector<std::uint32_t>& groups);

protected:
    std::uint32_t type_ = kOccupantDefaultType;                 // +0x48
    std::uint8_t bbox_[4] = {0xff, 0, 0, 0};                    // +0x4c
    std::uint16_t removal_ = 0;                    // +0x50 (see layout above)
    std::uint8_t state_ = 0;                                    // +0x52
    std::uint8_t flags_ = kOccupantFlagVisible;                                    // +0x53
    cISC43DPlaceableObject* placeable_ = nullptr;               // +0x58
};

}  // namespace sc4
