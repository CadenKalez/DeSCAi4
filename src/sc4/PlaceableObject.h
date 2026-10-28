// cSC43DPlaceableObject — the models and terrain overlays an occupant shows (25 functions at
// 0x10012055c..0x100120d94). Layout: +8 reference count (16 bit), +0xa model count (16 bit), +0x10 first model,
// +0x18 array of the remaining models, +0x20 overlay ids, +0x38 the terrain-view overlay manager, +0x58 flags:
// bit 0 overlays visible (initially set), bit 1 visible (models are in the renderer), bits 2-6 the zoom levels the
// models were created for, bits 7-10 the rotations.
//
// Order is preserved as in the binary: removing the first model moves the *last* model into its place, removing any
// other model shifts the later ones down.
#pragma once
#include <vector>

#include "sc4/Occupant.h"

namespace sc4 {

// The renderer and terrain-view overlay manager the object talks to (SL::spRenderer vslots 0xe0 / 0xe8, the
// overlay manager from SL::TerrainView() vslot 0x90 with vslots 0x78 / 0x80).
class IPlaceableHost {
public:
    virtual ~IPlaceableHost() = default;
    virtual bool AddModelToRenderer(cIS3DModelInstance* model) = 0;
    virtual void RemoveModelFromRenderer(cIS3DModelInstance* model) = 0;
    virtual void RemoveOverlay(std::uint32_t overlayId) = 0;
    virtual void SetOverlayVisible(std::uint32_t overlayId, bool visible) = 0;
};
IPlaceableHost* PlaceableHost();
void SetPlaceableHost(IPlaceableHost* host);

class cSC43DPlaceableObject : public cISC43DPlaceableObject {
public:
    cSC43DPlaceableObject() = default;
    virtual ~cSC43DPlaceableObject();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;

    int GetModelCount() override { return static_cast<int>(models_.size()); }
    std::uint32_t GetModels(cIS3DModelInstance** out, std::uint32_t& count) override;
    bool AddModel(cIS3DModelInstance* model) override;
    bool RemoveModel(cIS3DModelInstance* model) override;
    // @ 0x100120752: reallocates the array that holds the models after the first one, keeping the existing entries. The
    // port keeps every model in one vector, so this only reserves room for `capacity` models behind the first.
    void ResizeModelInstanceArray(int capacity) { models_.reserve(static_cast<std::size_t>(capacity) + 1); }
    bool AddOverlay(std::uint32_t overlayId) override;
    void SetVisibility(bool visible) override;
    void SetOverlaysVisible(bool visible) override;
    bool IsVisible() const override { return (flags_ & 2) != 0; }
    void RemoveAll() override;
    std::uint32_t GetZoomLevelsCreatedFor() const override { return (flags_ >> 2) & 0x1f; }
    void SetZoomLevelsCreatedFor(std::uint32_t zoomLevels) override { flags_ = (zoomLevels << 2) | (flags_ & 0xffffff83u); }
    std::uint32_t GetRotationsCreatedFor() const override { return (flags_ >> 7) & 0xf; }
    void SetRotationsCreatedFor(std::uint32_t rotations) override { flags_ = (rotations << 7) | (flags_ & 0xfffff87fu); }

private:
    void DoOverlayVisibilityChange();
    bool AddModelsToRenderer();
    void RemoveModelsFromRenderer();
    void FinalRelease();

    std::int16_t refCount_ = 0;                   // +8
    std::vector<cIS3DModelInstance*> models_;     // +0xa count, +0x10 / +0x18 storage
    std::vector<std::uint32_t> overlays_;         // +0x20
    std::uint32_t flags_ = 1;                     // +0x58
};

}  // namespace sc4
