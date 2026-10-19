#include "render/gd/TextureTable.h"

#include <cstring>

#include "render/GDIndexOps.h"
#include "render/TexelOps.h"

namespace gd {

using namespace nSGLDX7;

namespace {
constexpr std::uint32_t kInvalid = 0xffffffff;

bool IsCompressedFormat(std::uint32_t f) { return f >= kFmtDXT1; }

using Converter = nsTexelOp::ConvertFn;
// Destination formats: 0 RGB565, 1 RGB8 (32-bit, alpha ignored), 2 ARGB4, 3 A1RGB5, 4 ARGB8.
Converter FindConverter(std::uint32_t src, std::uint32_t dst) {
    using namespace nsTexelOp;
    static const Converter table[10][5] = {
        {ConvertARGB8ToRGB565, ConvertARGB8ToRGB8, ConvertARGB8ToARGB4, ConvertARGB8ToA1RGB5, ConvertARGB8ToARGB8},
        {ConvertABGR8ToRGB565, ConvertABGR8ToRGB8, ConvertABGR8ToARGB4, ConvertABGR8ToA1RGB5, ConvertABGR8ToARGB8},
        {ConvertRGB8ToRGB565, ConvertRGB8ToRGB8, ConvertRGB8ToARGB4, ConvertRGB8ToA1RGB5, ConvertRGB8ToARGB8},
        {ConvertBGR8ToRGB565, ConvertBGR8ToRGB8, ConvertBGR8ToARGB4, ConvertBGR8ToA1RGB5, ConvertBGR8ToARGB8},
        {ConvertARGB4ToRGB565, ConvertARGB4ToRGB8, ConvertARGB4ToARGB4, ConvertARGB4ToA1RGB5, ConvertARGB4ToARGB8},
        {ConvertABGR4ToRGB565, ConvertABGR4ToRGB8, ConvertABGR4ToARGB4, ConvertABGR4ToA1RGB5, ConvertABGR4ToARGB8},
        {ConvertA1RGB5ToRGB565, ConvertA1RGB5ToRGB8, ConvertA1RGB5ToARGB4, ConvertA1RGB5ToA1RGB5, ConvertA1RGB5ToARGB8},
        {ConvertA1BGR5ToRGB565, ConvertA1BGR5ToRGB8, ConvertA1BGR5ToARGB4, ConvertA1BGR5ToA1RGB5, ConvertA1BGR5ToARGB8},
        {ConvertRGB565ToRGB565, ConvertRGB565ToRGB8, ConvertRGB565ToARGB4, ConvertRGB565ToA1RGB5, ConvertRGB565ToARGB8},
        {ConvertBGR565ToRGB565, ConvertBGR565ToRGB8, ConvertBGR565ToARGB4, ConvertBGR565ToA1RGB5, ConvertBGR565ToARGB8},
    };
    if (src >= 10 || dst >= 5) return nullptr;
    return table[src][dst];
}

// Destination (internal) format -> nsTexelOp destination index, bytes per destination pixel.
struct Dest { std::uint32_t index; int bytes; };
Dest DestFor(std::uint32_t internalFormat) {
    switch (internalFormat) {
    case kFmtRGB565: return {0, 2};
    case kFmtRGB888: return {1, 4};
    case kFmtARGB4444: return {2, 2};
    case kFmtARGB1555: return {3, 2};
    default: return {4, 4};
    }
}

std::uint32_t Up(std::uint32_t v, int bits) {   // widen an n-bit channel to 8 bits by bit replication
    v <<= (8 - bits);
    return v | (v >> bits);
}
}  // namespace

bool ConvertToBGRA8(std::uint32_t sourceFormat, std::uint32_t internalFormat, const void* pixels, int rowBytes, int width,
                    int height, std::vector<std::uint8_t>& bgra) {
    const Dest dest = DestFor(internalFormat);
    Converter convert = FindConverter(sourceFormat, dest.index);
    if (!convert) return false;
    std::vector<std::uint8_t> converted(static_cast<std::size_t>(width) * height * dest.bytes);
    convert(converted.data(), pixels, width * dest.bytes, rowBytes, width, height);
    bgra.assign(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::size_t i = 0, n = static_cast<std::size_t>(width) * height; i < n; ++i) {
        std::uint32_t a = 255, r, g, b;
        const std::uint8_t* p = &converted[i * dest.bytes];
        std::uint16_t v16 = 0;
        if (dest.bytes == 2) std::memcpy(&v16, p, 2);
        switch (dest.index) {
        case 0: r = Up(v16 >> 11, 5); g = Up(v16 >> 5 & 63, 6); b = Up(v16 & 31, 5); break;
        case 2: a = Up(v16 >> 12, 4); r = Up(v16 >> 8 & 15, 4); g = Up(v16 >> 4 & 15, 4); b = Up(v16 & 15, 4); break;
        case 3: a = (v16 >> 15) ? 255 : 0; r = Up(v16 >> 10 & 31, 5); g = Up(v16 >> 5 & 31, 5); b = Up(v16 & 31, 5); break;
        default: {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            a = dest.index == 1 ? 255 : v >> 24;   // X8R8G8B8 ignores the 4th byte
            r = v >> 16 & 0xff; g = v >> 8 & 0xff; b = v & 0xff;
        }
        }
        bgra[i * 4] = static_cast<std::uint8_t>(b);
        bgra[i * 4 + 1] = static_cast<std::uint8_t>(g);
        bgra[i * 4 + 2] = static_cast<std::uint8_t>(r);
        bgra[i * 4 + 3] = static_cast<std::uint8_t>(a);
    }
    return true;
}

void cTextureTable::Init() {
    const std::uint32_t id = NewTexture();   // 0
    CreateTexture(id, 0, kFmtARGB8888, 1, 1, 1);
    const std::uint32_t white = 0xffffffff;
    LoadTextureLevel(id, 0, 0, 0, 1, 1, 3, 1, 1, &white);
}

std::uint32_t cTextureTable::NewTexture() {
    std::uint32_t id;
    if (!freeList_.empty()) {
        id = freeList_.back();
        freeList_.pop_back();
    } else {
        id = static_cast<std::uint32_t>(entries_.size());
        entries_.emplace_back();
    }
    Entry& e = entries_[id];
    e = Entry{};
    e.valid = true;
    return id;
}

void cTextureTable::FreeTexture(std::uint32_t id) {
    if (id >= entries_.size() || !entries_[id].valid) return;
    if (entries_[id].hasSurface) backend_->DestroySurface(id);
    entries_[id].hasSurface = false;
    entries_[id].valid = false;
    freeList_.push_back(id);
}

bool cTextureTable::IsValid(std::uint32_t id) const { return id < entries_.size() && entries_[id].valid; }
bool cTextureTable::HasTexture(std::uint32_t id) const { return IsValid(id) && entries_[id].hasSurface; }
bool cTextureTable::HasAlpha(std::uint32_t id) const { return IsValid(id) && GetFormatInfoNumAlphaBits(static_cast<int>(entries_[id].format)) > 1; }

bool cTextureTable::CreateTexture(std::uint32_t id, std::uint32_t hint, std::uint32_t format, std::uint32_t width,
                                  std::uint32_t height, std::uint32_t mipLevels) {
    if (!IsValid(id) || ((width - 1) | (height - 1)) > 0xfff) return false;
    Entry& e = entries_[id];
    e.hint = hint;
    e.width = width;
    e.height = height;
    e.mipLevels = mipLevels;
    e.format = format;
    if (e.hasSurface) backend_->DestroySurface(id);
    e.hasSurface = backend_->CreateSurface(id, format, width, height, mipLevels);
    return e.hasSurface;
}

bool cTextureTable::LoadTextureLevel(std::uint32_t id, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                     std::uint32_t height, std::uint32_t gdFormat, std::uint32_t gdType,
                                     std::uint32_t rowLengthPixels, const void* pixels) {
    if (!HasTexture(id)) return false;
    const Entry& e = entries_[id];
    if (level != 0 && e.mipLevels <= level) return false;
    std::uint32_t src;
    if (!TranslateGDFormatToSourceFormat(static_cast<int>(gdFormat), gdType, &src)) return false;
    const bool sourceCompressed = src >= kSrcDXT1;
    const bool destCompressed = IsCompressedFormat(e.format);
    if (sourceCompressed != destCompressed) return false;   // no converter between the two families
    if (destCompressed) {
        // S3TC sources must match the destination's block format (DXT1 -> 5, DXT3 -> 6, DXT5 -> 7)
        const std::uint32_t want = src == kSrcDXT1 ? kFmtDXT1 : src == kSrcDXT3 ? kFmtDXT3 : src == kSrcDXT5 ? kFmtDXT5 : kInvalid;
        if (want != e.format) return false;
        if (x != 0 || y != 0) return true;   // partial writes to compressed data are silently skipped
        std::uint32_t lw = e.width >> level, lh = e.height >> level;
        if (lw == 0) lw = 1;
        if (lh == 0) lh = 1;
        backend_->UploadLevel(id, level, 0, 0, lw, lh, static_cast<const std::uint8_t*>(pixels), true);
        return true;
    }
    const int srcBits = static_cast<int>(GetSourceFormatNumBits(src));
    const int rowBytes = static_cast<int>(((rowLengthPixels ? rowLengthPixels : width) * static_cast<std::uint32_t>(srcBits)) >> 3);
    std::vector<std::uint8_t> bgra;
    if (!ConvertToBGRA8(src, e.format, pixels, rowBytes, static_cast<int>(width), static_cast<int>(height), bgra)) return false;
    backend_->UploadLevel(id, level, x, y, width, height, bgra.data(), false);
    return true;
}

bool cTextureTable::SetTextureParam(std::uint32_t id, std::uint32_t type, std::uint32_t value) {
    if (!IsValid(id) || type > 3) return false;
    TextureParams& p = entries_[id].params;
    (type == 0 ? p.magFilter : type == 1 ? p.minFilter : type == 2 ? p.wrapS : p.wrapT) = value;
    return true;
}

bool cTextureTable::GetTextureParams(std::uint32_t id, TextureParams& out) const {
    if (!IsValid(id)) return false;
    out = entries_[id].params;
    return true;
}

}  // namespace gd
