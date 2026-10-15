// FSH texture container (resource type 0x7ab50e44), read like cS3DShapeFile (0x10046d322..0x10046d7ea):
//   "SHPI", u32 file size, u32 entry count, u32 directory id, then entries { char tag[4]; u32 offset }.
//   Each frame: u8 code, u24 offset of the next attachment (0 = none), s16 width, s16 height, u32 misc
//   (top nibble = mip count - 1, low 12 bits = x centre) followed by pixel data.
// Codes: 0x60 DXT1, 0x61 DXT3, 0x62 DXT5, 0x78 RGB565, 0x7d ARGB8888, 0x7e ARGB1555, 0x7f RGB888,
//        0x6d ARGB4444, 0x7b 8-bit paletted (palette attachment).
#pragma once
#include <cstdint>
#include <vector>

namespace s3d {

struct FshFrameInfo {
    std::uint32_t code = 0;
    std::int32_t width = 0, height = 0;
    std::uint32_t tag = 0;                // directory entry name
    std::uint32_t mipCount = 0;           // levels after the first
    std::int32_t centerX = 0;
    const std::uint8_t* pixels = nullptr; // frame + 0x10
};

class FshFile {
public:
    bool Open(const std::uint8_t* data, std::size_t size);     // copies the buffer
    std::uint32_t FrameCount() const { return entryCount_; }
    bool GetFrame(std::uint32_t index, FshFrameInfo& info) const;
    // Decode a frame's top mip level to tightly packed RGBA8 (R first). False for unsupported codes.
    bool DecodeRgba8(std::uint32_t index, std::vector<std::uint8_t>& rgba, int& width, int& height) const;

private:
    std::vector<std::uint8_t> data_;
    std::uint32_t entryCount_ = 0;
};

// bytes per pixel x 2 of a frame code (cS3DShapeFile::PixelWidthMul2): 1 = DXT1 (4 bpp), 2 = 8 bpp, 4 = 16 bpp,
// 6 = 24 bpp, 8 = 32 bpp, 0 = unsupported
std::uint32_t PixelWidthMul2(std::uint32_t code);

void DecodeDxt1(const std::uint8_t* src, int w, int h, std::uint8_t* rgba);
void DecodeDxt3(const std::uint8_t* src, int w, int h, std::uint8_t* rgba);
void DecodeDxt5(const std::uint8_t* src, int w, int h, std::uint8_t* rgba);

}  // namespace s3d
