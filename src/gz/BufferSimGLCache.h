// cGZBufferSimGLCache — the hardware (GL) texture copy of a cGZBuffer (class vtables 0x100af0160 / 0x100af01f8 / 0x100af0238,
// 29 functions at 0x1007b2a5c..0x1007b3600 of which 13 are non-virtual thunks). The buffer is cut into texture tiles
// (cRZTileSetup, tiles of 16..256 pixels), one GL texture per tile; Refresh uploads the buffer's pixels again whenever its
// change counter moved. A buffer with a colour key is converted to alpha first (nRZSimGL::BltColorkeyToAlpha_*).
//
// It implements three bases in the original: the cIGZBufferHardwareCache of Buffer.h at +0 (iid 0xfa59f7b9; the slot names
// of that interface map to this class as Attach = Init, Slot4 = Shutdown, Slot5 = InitCache, Flush = ShutdownCache, Slot7 =
// IsDirty, Invalidate, IsResident = IsAllocated, Slot10 = Refresh, GetLastUsedClock = GetLastAccess), a tile query interface
// at +8 (iid 0xeb4d101f) and cRZRefCount at +0x10.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/Buffer.h"
#include "render/gd/IGZGDriver.h"
#include "rz/TileSetup.h"
#include "rz/cRZRefCount.h"

constexpr std::uint32_t kGZIID_cIGZBufferSimGLCache = 0xfa59f7b9;
constexpr std::uint32_t kGZIID_cIGZBufferTileSource = 0xeb4d101f;

class cIGZBufferTileSource : public cIGZUnknown {
public:
    virtual std::int32_t GetTileCount() = 0;                                        // [3]
    virtual std::uint32_t GetMaxFragments() = 0;                                    // [4]
    // texture id, texture size, the pieces of the buffer in that texture and their number
    virtual void GetTile(std::uint32_t index, std::uint32_t& texture, cRZTileSize& size, const cRZSubTile*& pieces,
                         std::uint32_t& pieceCount) = 0;                            // [5]
};

// What the cache reaches outside itself.
class IGLCacheHost {
public:
    virtual ~IGLCacheHost() = default;
    virtual void RegisterBufferCache(cIGZBufferHardwareCache* cache) = 0;     // draw system (service 0xeb2ea3cb) vslot 0x30
    virtual void UnregisterBufferCache(cIGZBufferHardwareCache* cache) = 0;   // draw system vslot 0x38 (skipped without a framework)
    virtual gd::cIGZGDriver* GetGDriver() = 0;                               // RZ::GS() interface 0xc47b747c, vslot 0x18; null when unavailable
};

class cGZBufferSimGLCache : public cIGZBufferHardwareCache, public cIGZBufferTileSource, private cRZRefCount {
public:
    explicit cGZBufferSimGLCache(IGLCacheHost* host);   // 0x1007b2a5c
    ~cGZBufferSimGLCache() override;                    // 0x1007b2af6 / 0x1007b2ba0 / 0x1007b2bb8 (+ thunks 0x1007b2baa, 0x1007b2be6)

    // cIGZUnknown (one implementation serves both interface bases and the refcount base)
    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x1007b2c40 (+ thunk 0x1007b2c74): 1 and 0xfa59f7b9 -> primary, 0xeb4d101f -> tile source
    std::uint32_t AddRef() override;                               // 0x1007b2bf4 (+ thunks 0x1007b2c02, 0x1007b2c10)
    std::uint32_t Release() override;                              // 0x1007b2c1a (+ thunks 0x1007b2c28, 0x1007b2c36)

    // cIGZBufferHardwareCache
    bool Attach(cIGZBuffer* buffer) override;      // 0x1007b2cac "Init": remembers the buffer and registers with the draw system
    void Slot4() override;                         // 0x1007b2d22 "Shutdown"
    void Slot5() override;                         // 0x1007b2dae "InitCache" (see InitCache)
    void Flush() override;                         // 0x1007b2f4a "ShutdownCache": deletes the textures
    void Slot7() override {}                       // (IsDirty is reached through IsDirty())
    void Invalidate() override;                    // 0x1007b3078
    bool IsResident() override;                    // 0x1007b3086 "IsAllocated"
    void Slot10() override {}                      // (Refresh is reached through Refresh())
    std::uint32_t GetLastUsedClock() override;     // 0x1007b3550 "GetLastAccess"

    bool InitCache();                              // 0x1007b2dae
    bool IsDirty();                                // 0x1007b3030
    bool Refresh(std::uint32_t clock);             // 0x1007b3098
    void Shutdown() { Slot4(); }
    void ShutdownCache() { Flush(); }

    // cIGZBufferTileSource
    std::int32_t GetTileCount() override;          // 0x1007b355c (+ thunk 0x1007b356a)
    std::uint32_t GetMaxFragments() override;      // 0x1007b3578 (+ thunk 0x1007b3586)
    void GetTile(std::uint32_t index, std::uint32_t& texture, cRZTileSize& size, const cRZSubTile*& pieces,
                 std::uint32_t& pieceCount) override;   // 0x1007b3594 (+ thunk 0x1007b3600)

    std::uint32_t TextureFormat() const { return textureFormat_; }

private:
    IGLCacheHost* host_;
    cIGZBuffer* buffer_ = nullptr;             // +0x20
    std::vector<std::uint32_t> textures_;      // +0x28 one GL texture per tile
    std::uint32_t textureFormat_ = 0;          // +0x40
    cRZTileSetup tiles_;                       // +0x48
    bool dirty_ = false;                       // +0x88
    std::uint32_t changeCounter_ = 0;          // +0x8c the buffer's change counter at the last upload
    std::uint32_t lastAccess_ = 0;             // +0x90
};
