#include "gz/BufferSimGLCache.h"

#include <algorithm>

#include "render/SimGL.h"

namespace {
// Table at 0x100a2afe0, indexed by colour type - 2: the driver's internal texture format of each buffer format.
constexpr std::uint32_t kInternalFormat[8] = {4, 4, 3, 4, 3, 2, 1, 4};
constexpr gd::eGDTextureFormat kUploadTextureFormat = 3;   // the format argument every upload passes
}  // namespace

cGZBufferSimGLCache::cGZBufferSimGLCache(IGLCacheHost* host) : host_(host) {}   // 0x1007b2a5c

cGZBufferSimGLCache::~cGZBufferSimGLCache() { Slot4(); }   // 0x1007b2af6

bool cGZBufferSimGLCache::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZBufferSimGLCache) {
        *out = static_cast<cIGZBufferHardwareCache*>(this);
    } else if (iid == kGZIID_cIGZBufferTileSource) {
        *out = static_cast<cIGZBufferTileSource*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cGZBufferSimGLCache::AddRef() {
    cRZRefCount::AddRef();
    return RefCount();
}

std::uint32_t cGZBufferSimGLCache::Release() { return static_cast<std::uint32_t>(cRZRefCount::Release()); }

// 0x1007b2cac: the draw system keeps the cache in its list (to flush old ones).
bool cGZBufferSimGLCache::Attach(cIGZBuffer* buffer) {
    buffer_ = buffer;
    host_->RegisterBufferCache(this);
    return true;
}

// 0x1007b2d22
void cGZBufferSimGLCache::Slot4() {
    host_->UnregisterBufferCache(this);
    Flush();
    buffer_ = nullptr;
}

// 0x1007b2f4a: hands the textures back to the driver.
void cGZBufferSimGLCache::Flush() {
    if (textures_.empty()) return;
    gd::cIGZGDriver* driver = host_->GetGDriver();
    if (driver != nullptr) {
        std::vector<std::uint32_t> doomed = textures_;
        textures_.clear();
        driver->DeleteTextures(static_cast<int>(doomed.size()), doomed.data());
    }
}

void cGZBufferSimGLCache::Slot5() { InitCache(); }

// 0x1007b2dae: cuts the buffer into tiles and creates one texture per tile.
bool cGZBufferSimGLCache::InitCache() {
    Flush();
    const cRZRect* area = static_cast<gz::IDrawTargetBuffer*>(buffer_)->GetBufferArea();   // vslot 0x68
    tiles_.Init({area->right - area->left, area->bottom - area->top}, 0x10, 0x100, 1);
    gd::cIGZGDriver* driver = host_->GetGDriver();
    if (driver == nullptr) return false;
    const std::uint32_t count = static_cast<std::uint32_t>(tiles_.GetTileCount());
    if (textures_.empty()) {
        const std::uint32_t type = static_cast<std::uint32_t>(buffer_->GetColorType().type);   // vslot 0x80
        textureFormat_ = (type - 2 < 8) ? kInternalFormat[type - 2] : 4;
    }
    textures_.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const cRZTile& tile = tiles_.GetTile(static_cast<int>(i));
        textures_[i] = driver->CreateTexture(textureFormat_, static_cast<std::uint32_t>(tile.size.width),
                                             static_cast<std::uint32_t>(tile.size.height), 1, 1);   // vslot 0x1e0
    }
    dirty_ = true;
    return true;
}

// 0x1007b3030: true when flagged dirty or when the buffer changed since the last upload (which flags it).
bool cGZBufferSimGLCache::IsDirty() {
    if (dirty_) return true;
    if (buffer_->GetChangeCounter() == changeCounter_) return false;
    dirty_ = true;
    return true;
}

void cGZBufferSimGLCache::Invalidate() { dirty_ = true; }   // 0x1007b3078

bool cGZBufferSimGLCache::IsResident() { return !textures_.empty(); }   // 0x1007b3086

std::uint32_t cGZBufferSimGLCache::GetLastUsedClock() { return lastAccess_; }   // 0x1007b3550

// 0x1007b3098: re-uploads a dirty buffer, tile by tile.
bool cGZBufferSimGLCache::Refresh(std::uint32_t clock) {
    lastAccess_ = clock;
    if (textures_.empty()) InitCache();
    if (!IsDirty()) return true;

    const std::int32_t type = buffer_->GetColorType().type;
    // pixel type handed to the driver: 1 for ARGB8888, 13 for ARGB4444, 10 for the other 16-bit formats
    const std::uint32_t pixelType = (type == kBufferColorARGB8888) ? 1 : ((type == kBufferColorARGB4444) ? 3 : 0) + 10;
    auto* bits = static_cast<std::uint8_t*>(buffer_->GetColorSurfaceBits());
    const std::uint32_t stride = buffer_->GetColorSurfaceStride();
    const std::uint32_t bytesPerPixel = buffer_->GetBytesPerPixel();
    gd::cIGZGDriver* driver = host_->GetGDriver();
    if (driver != nullptr) {
        const int tileCount = tiles_.GetTileCount();
        if (!buffer_->GetTransparency()) {
            for (int i = 0; i < tileCount; ++i) {
                for (const cRZSubTile& s : tiles_.GetTile(i).subTiles) {
                    driver->LoadTextureLevel(textures_[static_cast<std::size_t>(i)], 0, static_cast<std::uint32_t>(s.tileX),
                                             static_cast<std::uint32_t>(s.tileY), static_cast<std::uint32_t>(s.width),
                                             static_cast<std::uint32_t>(s.height), kUploadTextureFormat, pixelType,
                                             stride / bytesPerPixel,
                                             bits + static_cast<std::int64_t>(s.x) * bytesPerPixel + static_cast<std::int64_t>(s.y) * stride);   // vslot 0x1e8
                }
            }
        } else {
            // Colour keyed: every tile is assembled in a scratch image with the key turned into alpha 0.
            std::uint32_t colorKey = 0;
            buffer_->GetTransparentColor(colorKey);   // vslot 0xe0
            std::uint32_t largest = 0;
            for (int i = 0; i < tileCount; ++i) {
                const cRZTile& tile = tiles_.GetTile(i);
                largest = std::max(largest, static_cast<std::uint32_t>(tile.size.width * tile.size.height));
            }
            std::vector<std::uint8_t> scratch(static_cast<std::size_t>(largest) * bytesPerPixel);   // kept across tiles
            for (int i = 0; i < tileCount; ++i) {
                const cRZTile& tile = tiles_.GetTile(i);
                const std::uint32_t tileWidth = static_cast<std::uint32_t>(tile.size.width);
                for (const cRZSubTile& s : tile.subTiles) {
                    const std::uint8_t* source = bits + static_cast<std::int64_t>(s.y) * stride + static_cast<std::int64_t>(s.x) * bytesPerPixel;
                    // Pixels are converted for four of the six formats; RGB565 and XRGB8888 are skipped (kept).
                    switch (type) {
                        case kBufferColorXRGB1555:
                        case kBufferColorARGB1555:
                            nRZSimGL::BltColorkeyToAlpha_ARGB1555(scratch.data() + (static_cast<std::size_t>(s.tileY) * tileWidth + s.tileX) * 2, tileWidth * 2, source, stride,
                                                                  s.width, s.height, colorKey);
                            break;
                        case kBufferColorARGB4444:
                            nRZSimGL::BltColorkeyToAlpha_ARGB4444(scratch.data() + (static_cast<std::size_t>(s.tileY) * tileWidth + s.tileX) * 2, tileWidth * 2, source, stride,
                                                                  s.width, s.height, colorKey);
                            break;
                        case kBufferColorARGB8888:
                            nRZSimGL::BltColorkeyToAlpha_ARGB8888(scratch.data() + (static_cast<std::size_t>(s.tileY) * tileWidth + s.tileX) * 4, tileWidth * 4, source, stride,
                                                                  s.width, s.height, colorKey);
                            break;
                        default:
                            break;
                    }
                }
                driver->LoadTextureLevel(textures_[static_cast<std::size_t>(i)], 0, 0, 0, tileWidth, static_cast<std::uint32_t>(tile.size.height),
                                         kUploadTextureFormat, pixelType, tileWidth, scratch.data());
            }
        }
    }
    dirty_ = false;
    changeCounter_ = buffer_->GetChangeCounter();
    return true;
}

std::int32_t cGZBufferSimGLCache::GetTileCount() { return tiles_.GetTileCount(); }                 // 0x1007b355c
std::uint32_t cGZBufferSimGLCache::GetMaxFragments() { return tiles_.GetMaxFragmentsPerTile(); }   // 0x1007b3578

// 0x1007b3594
void cGZBufferSimGLCache::GetTile(std::uint32_t index, std::uint32_t& texture, cRZTileSize& size, const cRZSubTile*& pieces,
                                  std::uint32_t& pieceCount) {
    const cRZTile& tile = tiles_.GetTile(static_cast<int>(index));
    texture = textures_[index];
    size = tile.size;
    pieceCount = static_cast<std::uint32_t>(tile.subTiles.size());
    pieces = tile.subTiles.data();
}
