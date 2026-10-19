// The driver's texture table (nSGLDX7::cTextureManager's observable behaviour, minus all D3D7 surface plumbing):
// handle allocation and reuse, validity, per-texture sampler parameters, alpha-bit queries and the pixel-format conversion
// of LoadTextureLevel. Storage is behind ITextureBackend (Metal in the shipping build, a recorder in tests).
//
// Observable rules reproduced:
//  * Init creates texture 0, a 1x1 opaque-white ARGB8888 texture, so the first NewTexture() after Init returns 1.
//  * NewTexture() reuses the most recently freed handle first (LIFO), else appends; the new entry is valid but has no surface.
//  * Create fails (returns false, handle stays allocated) for an invalid handle or a size with (w-1)|(h-1) > 0xfff.
//  * LoadTextureLevel fails for an invalid handle / no surface / level >= mip count (level 0 always allowed) / unsupported
//    (format, type) pair / source-destination pair without a converter. A compressed destination only accepts x=y=0 and
//    silently skips the copy otherwise (still reports success, as the original does).
//  * Sampler parameters (types 0..3: mag, min, wrap S, wrap T) default to (0, 0, 3, 3).
#pragma once
#include <cstdint>
#include <vector>

namespace gd {

// eGDInternalTextureFormat
enum InternalFormat : std::uint32_t { kFmtRGB565 = 0, kFmtRGB888 = 1, kFmtARGB4444 = 2, kFmtARGB1555 = 3, kFmtARGB8888 = 4, kFmtDXT1 = 5, kFmtDXT3 = 6, kFmtDXT5 = 7 };

class ITextureBackend {
public:
    virtual ~ITextureBackend() = default;
    virtual bool CreateSurface(std::uint32_t id, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t mipLevels) = 0;
    virtual void DestroySurface(std::uint32_t id) = 0;
    // Pixels are BGRA8 rows (tight, width*4 bytes) for uncompressed formats, or the level's raw S3TC blocks.
    virtual bool UploadLevel(std::uint32_t id, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                             std::uint32_t height, const std::uint8_t* data, bool compressed) = 0;
};

struct TextureParams {   // cTextureInfo
    std::uint32_t magFilter = 0, minFilter = 0, wrapS = 3, wrapT = 3;
};

class cTextureTable {
public:
    explicit cTextureTable(ITextureBackend* backend) : backend_(backend) {}

    void Init();                                                        // CreateDefaultTextures
    std::uint32_t NewTexture();                                         // @ 0x10060bbc4
    void FreeTexture(std::uint32_t id);                                 // @ 0x10060b452
    bool IsValid(std::uint32_t id) const;                               // @ 0x10060b756
    bool HasTexture(std::uint32_t id) const;                            // @ 0x10060ba5a (valid and has a surface)
    bool HasAlpha(std::uint32_t id) const;                              // @ 0x10060bd20 (more than 1 alpha bit)
    bool CreateTexture(std::uint32_t id, std::uint32_t hint, std::uint32_t format, std::uint32_t width, std::uint32_t height,
                       std::uint32_t mipLevels);                        // @ 0x10060b5bc
    bool LoadTextureLevel(std::uint32_t id, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                          std::uint32_t height, std::uint32_t gdFormat, std::uint32_t gdType, std::uint32_t rowLengthPixels,
                          const void* pixels);                          // @ 0x10060b8aa
    bool SetTextureParam(std::uint32_t id, std::uint32_t type, std::uint32_t value);   // @ 0x10060be44
    bool GetTextureParams(std::uint32_t id, TextureParams& out) const;  // @ 0x10060bedc
    std::uint32_t Size() const { return static_cast<std::uint32_t>(entries_.size()); }
    std::uint32_t FormatOf(std::uint32_t id) const { return id < entries_.size() ? entries_[id].format : 0; }

private:
    struct Entry {
        bool valid = false;
        bool hasSurface = false;
        std::uint32_t hint = 0, width = 0, height = 0, mipLevels = 0, format = 0;
        TextureParams params;
    };
    std::vector<Entry> entries_;
    std::vector<std::uint32_t> freeList_;
    ITextureBackend* backend_;
};

// Decodes `pixels` (a source format of nSGLDX7::eSupportedSourceFormat) into BGRA8 at the precision of `internalFormat`
// (the original converts source -> destination format with nsTexelOp, then the GPU expands). Returns false when there is no
// converter for the pair. `rowBytes` is the source row pitch.
bool ConvertToBGRA8(std::uint32_t sourceFormat, std::uint32_t internalFormat, const void* pixels, int rowBytes, int width,
                    int height, std::vector<std::uint8_t>& bgra);

}  // namespace gd
