// cSC4CatalogItem (0x1002fcdfc..0x1002ff070, iid 0x49d98263) — one entry of a build catalog: name, description,
// cost, icon, item type / id and an enabled flag. Layout: +8 name, +0x30 description, +0x58 cost, +0x60 icon
// (released with it), +0x68 type, +0x6c id, +0x70 refcount, +0x74 enabled (initially true).
// cSC4GlyphTextureInfo (0x10070dc98..0x10070dfaa) — where a glyph sits inside a shared glyph texture. Layout:
// +8 glyph, +0x30/+0x32 texture size, +0x34/+0x36 glyph size, +0x38 texture coordinates, +0x48 texture binding,
// +0x50 refcount, +0x58 manager. When the last outside reference goes (count 2 -> 1) and a manager exists the
// manager is told the info is unused (it keeps the cache reference) instead of destroying it.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4CatalogItem = 0x49d98263;

class cISC4CatalogItem : public cIGZUnknown {
public:
    virtual const cRZString& GetName() const = 0;               // [3]
    virtual const cRZString& GetDescription() const = 0;        // [4]
    virtual std::uint32_t GetCost() const = 0;                  // [5]
    virtual cIGZUnknown* GetIcon() const = 0;                   // [6]
    virtual std::uint32_t GetItemType() const = 0;              // [7]
    virtual std::uint32_t GetItemID() const = 0;                // [8]
    virtual bool GetEnabled() const = 0;                        // [9]
    virtual void SetEnabled(bool enabled) = 0;                  // [10]
};

class cSC4CatalogItem final : public cISC4CatalogItem {
public:
    cSC4CatalogItem() = default;
    ~cSC4CatalogItem();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    const cRZString& GetName() const override { return name_; }
    const cRZString& GetDescription() const override { return description_; }
    std::uint32_t GetCost() const override { return cost_; }
    cIGZUnknown* GetIcon() const override { return icon_; }
    std::uint32_t GetItemType() const override { return itemType_; }
    std::uint32_t GetItemID() const override { return itemId_; }
    bool GetEnabled() const override { return enabled_; }
    void SetEnabled(bool enabled) override { enabled_ = enabled; }

    // Filling a catalog item (done by the catalog builders).
    void SetName(const cRZString& name) { name_ = name; }
    void SetDescription(const cRZString& description) { description_ = description; }
    void SetCost(std::uint32_t cost) { cost_ = cost; }
    void SetItemType(std::uint32_t type) { itemType_ = type; }
    void SetItemID(std::uint32_t id) { itemId_ = id; }
    void SetIcon(cIGZUnknown* icon);

private:
    cRZString name_;                  // +8
    cRZString description_;           // +0x30
    std::uint32_t cost_ = 0;          // +0x58
    cIGZUnknown* icon_ = nullptr;     // +0x60
    std::uint32_t itemType_ = 0;      // +0x68
    std::uint32_t itemId_ = 0;        // +0x6c
    std::uint32_t refCount_ = 0;      // +0x70
    bool enabled_ = true;             // +0x74
};

class cSC4GlyphTextureInfo;

// The glyph texture manager (cSC4GlyphTextureManager): counted, and told when an info loses its last user.
class IGlyphTextureManager : public cIGZUnknown {
public:
    virtual void SignalUnused(cSC4GlyphTextureInfo* info) = 0;
};

// cS3DTextureBinding as the glyph info holds it (counted).
class IGlyphTextureBinding {
public:
    virtual ~IGlyphTextureBinding() = default;
    virtual void Release() = 0;
};

struct GlyphSize { std::uint32_t width, height; };
struct GlyphTexCoords { float left, top, right, bottom; };

class cSC4GlyphTextureInfo {
public:
    cSC4GlyphTextureInfo(const cRZString& glyph, std::uint16_t textureW, std::uint16_t textureH, std::uint16_t glyphW,
                         std::uint16_t glyphH, const GlyphTexCoords& coords, IGlyphTextureBinding* binding)
        : glyph_(glyph), textureW_(textureW), textureH_(textureH), glyphW_(glyphW), glyphH_(glyphH), coords_(coords), binding_(binding) {}
    virtual ~cSC4GlyphTextureInfo();

    bool QueryInterface(std::uint32_t, void**) { return false; }
    int AddRef() { return ++refCount_; }
    int Release();
    void GetGlyph(cIGZString& out) const { out.AssignFromIGZString(glyph_); }
    GlyphSize GetTextureSize() const { return {textureW_, textureH_}; }
    GlyphSize GetGlyphSize() const { return {glyphW_, glyphH_}; }
    void GetGlyphTextureCoordinates(GlyphTexCoords& out) const { out = coords_; }
    IGlyphTextureBinding* GetTextureBinding() const { return binding_; }
    void SetManager(IGlyphTextureManager* manager);

private:
    cRZString glyph_;                         // +8
    std::uint16_t textureW_, textureH_;       // +0x30
    std::uint16_t glyphW_, glyphH_;           // +0x34
    GlyphTexCoords coords_;                   // +0x38
    IGlyphTextureBinding* binding_;           // +0x48
    int refCount_ = 1;                        // +0x50
    IGlyphTextureManager* manager_ = nullptr; // +0x58
};

}  // namespace sc4
