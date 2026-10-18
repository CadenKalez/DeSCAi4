// cSLBuffer — a lazily loaded picture used by the UI code (17 functions at 0x10004367c..0x100043ed0). It names a bitmap by
// resource key or by file path plus the colour type the caller wants; Load() fetches it (once, unless the buffer was made
// "always reload") and, when the stored picture is not already in that colour type, copies it into a fresh buffer of that
// type. Quirk kept: the converted copy does not get the original's colour key (the key is only put back on the original).
//
// Resource keys with type 0 mean the standard bitmap type 0x856ddbac. When the render settings ask for low-quality
// textures the lookup first tries group 1 of the same key and falls back to the requested group when group 1 is missing.
// Layout: +8 buffer, +0x10 key (type, group, instance), +0x20 path, +0x48 colour type, +0x4c always-reload flag.
#pragma once
#include <cstdint>
#include <string>

#include "gz/Buffer.h"
#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"

// The services Load talks to: the persistence resource manager (SL::spRM), the render properties and the graphics factory
// (SL::spGF), plus the buffer class factory (GZCOM, class 0xc470d325).
class ISLBufferHost {
public:
    virtual ~ISLBufferHost() = default;
    // Loads the bitmap resource `key` as a buffer (a counted reference). `uncached` skips the resource cache.
    virtual bool GetBufferResource(const cGZPersistResourceKey& key, std::int32_t colorType, bool uncached, cIGZBuffer** out) = 0;
    virtual bool HasResource(const cGZPersistResourceKey& key) = 0;      // resource manager slot 0x70
    virtual bool LowQualityTextures() = 0;                               // render properties +0x878
    virtual bool LoadBufferFromFile(const std::string& path, std::int32_t colorType, cIGZBuffer** out) = 0;   // SL::spGF slot 0x20
    virtual cIGZBuffer* CreateBuffer() = 0;                              // a new, uninitialised cGZBuffer (counted)
};
ISLBufferHost* SLBufferHost();
void SetSLBufferHost(ISLBufferHost* host);

constexpr std::uint32_t kSLBitmapResourceType = 0x856ddbac;

class cSLBuffer {
public:
    explicit cSLBuffer(bool alwaysReload);                                                  // @ 0x10004367c
    cSLBuffer(const cGZPersistResourceKey& key, std::int32_t colorType, bool alwaysReload);   // @ 0x1000436c2
    cSLBuffer(std::uint32_t type, std::uint32_t group, std::uint32_t instance, std::int32_t colorType, bool alwaysReload);   // @ 0x100043bdc
    cSLBuffer(const cRZString& path, std::int32_t colorType, bool alwaysReload);            // @ 0x100043c80
    virtual ~cSLBuffer() = default;                                                         // @ 0x100043d4a

    bool Load();                                                                            // @ 0x100043760
    void Unload() { buffer_ = nullptr; }                                                    // @ 0x100043dce
    cIGZBuffer* GetBuffer() const { return buffer_.get(); }
    void GetResKey(cGZPersistResourceKey& key) const { key = key_; }                        // @ 0x100043e72
    void SetResKey(const cGZPersistResourceKey& key) { key_ = key; }                        // @ 0x100043e90
    void SetResKey(std::uint32_t type, std::uint32_t group, std::uint32_t instance) { key_ = {type, group, instance}; }   // @ 0x100043ea8
    void SetPath(const cRZString& path) { path_ = std::string(path.ToChar(), path.Strlen()); }   // @ 0x100043ebe
    void SetColorType(std::int32_t colorType) { colorType_ = colorType; }                   // @ 0x100043ed0

private:
    cRZAutoRefCount<cIGZBuffer> buffer_;     // +8
    cGZPersistResourceKey key_{};            // +0x10
    std::string path_;                       // +0x28 (inside the cRZString at +0x20)
    std::int32_t colorType_ = 1;             // +0x48
    bool alwaysReload_ = false;              // +0x4c
};
