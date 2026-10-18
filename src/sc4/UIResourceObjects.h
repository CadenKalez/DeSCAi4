// Two small UI resource objects.
//  * cSC4CachedStringTexture (9 functions, 0x10070de06..0x10070dfb4): a rendered string held as a texture,
//    counted from the glyph texture manager's cache. Layout: +8 / +0xa texture width / height, +0xc refcount,
//    +0x10 manager (counted), +0x20 the string, +0x40 the texture binding. Release has a cache twist: when a
//    manager is attached the object stays alive at a count of 1 (the cache's own reference) and Release answers 0.
//  * cSC4ScriptServiceResourceFactory (9 functions, 0x100306388..0x1003064e6, iid 0x8aa22237): loads UI script
//    buffers and strings by resource key for the script service.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

namespace sc4 {

class cSC4CachedStringTexture;

class IGlyphTextureCache : public cIGZUnknown {
public:
    virtual std::uint32_t GetBoundIdForTexture(cSC4CachedStringTexture* texture) = 0;
};

class cSC4CachedStringTexture {
public:
    cSC4CachedStringTexture(std::uint16_t width, std::uint16_t height, cIGZUnknown* binding)
        : width_(width), height_(height), binding_(binding) {}
    virtual ~cSC4CachedStringTexture();

    bool QueryInterface(std::uint32_t, void**) { return false; }
    int AddRef() { return ++refCount_; }
    int Release();
    void SetManager(IGlyphTextureCache* manager);
    std::uint32_t GetTextureId() { return manager_->GetBoundIdForTexture(this); }
    void GetTextureSize(std::uint32_t& width, std::uint32_t& height) const { width = width_; height = height_; }
    cRZString& Text() { return text_; }

private:
    std::uint16_t width_, height_;               // +8, +0xa
    int refCount_ = 0;                           // +0xc
    IGlyphTextureCache* manager_ = nullptr;      // +0x10
    cRZString text_;                             // +0x20
    cIGZUnknown* binding_;                       // +0x40 (counted)
};

constexpr std::uint32_t kGZIID_cISC4ScriptServiceResourceFactory = 0x8aa22237;

class IScriptResourceHost {
public:
    virtual ~IScriptResourceHost() = default;
    virtual cIGZUnknown* LoadScriptBuffer(const cGZPersistResourceKey& key) = 0;                       // cSLBuffer(key, 1, 0)
    virtual bool LoadString(std::uint32_t group, std::uint32_t instance, cIGZString& out) = 0;         // SC4LoadString
};

class cSC4ScriptServiceResourceFactory {
public:
    explicit cSC4ScriptServiceResourceFactory(IScriptResourceHost* host) : host_(host) {}
    virtual ~cSC4ScriptServiceResourceFactory() = default;

    bool QueryInterface(std::uint32_t iid, void** out);
    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();
    cIGZUnknown* GZUIScriptLoadBuffer(const cGZPersistResourceKey& key);     // counted, or null
    cRZString* GZUIScriptLoadString(const cGZPersistResourceKey& key);       // new counted string, or null on failure
    void GZUIScriptLoadString(const cGZPersistResourceKey& key, cIGZString& out);

private:
    IScriptResourceHost* host_;
    std::uint32_t refCount_ = 0;   // +8
};

}  // namespace sc4
