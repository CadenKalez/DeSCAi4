#include "s3d/FSH.h"

#include <algorithm>
#include <cstring>

namespace s3d {

std::uint32_t PixelWidthMul2(std::uint32_t code) {
    switch (code) {
        case 0x60: case 0x7a: return 1;
        case 0x61: case 0x62: case 0x64: case 0x7b: return 2;
        case 0x65: case 0x6d: case 0x78: case 0x7e: return 4;
        case 0x7d: return 8;
        case 0x7f: return 6;
        default: return 0;
    }
}

bool FshFile::Open(const std::uint8_t* data, std::size_t size) {
    if (size < 16 || std::memcmp(data, "SHPI", 4) != 0) return false;
    std::uint32_t declared;
    std::memcpy(&declared, data + 4, 4);
    if (declared > size) return false;
    data_.assign(data, data + size);
    std::memcpy(&entryCount_, data + 8, 4);
    if (16 + std::size_t(entryCount_) * 8 > size) { data_.clear(); entryCount_ = 0; return false; }
    return entryCount_ != 0;
}

bool FshFile::GetFrame(std::uint32_t index, FshFrameInfo& info) const {
    if (index >= entryCount_) return false;
    std::uint32_t tag, offset;
    std::memcpy(&tag, &data_[16 + std::size_t(index) * 8], 4);
    std::memcpy(&offset, &data_[16 + std::size_t(index) * 8 + 4], 4);
    if (offset + 16 > data_.size()) return false;
    const std::uint8_t* f = &data_[offset];
    std::int16_t w, h;
    std::uint32_t misc;
    std::memcpy(&w, f + 4, 2);
    std::memcpy(&h, f + 6, 2);
    std::memcpy(&misc, f + 12, 4);
    info.code = f[0];
    info.width = w;
    info.height = h;
    info.tag = tag;
    info.mipCount = misc >> 28;
    info.centerX = static_cast<std::int16_t>(static_cast<std::int32_t>(misc << 20) >> 20);
    info.pixels = f + 16;
    return true;
}

bool FshFile::SetCurrentFrameByIndex(std::uint32_t index) {
    if (current_ == index) return true;
    if (index >= entryCount_) return false;
    current_ = index;
    attachment_ = 0;
    std::uint32_t offset;
    std::memcpy(&offset, &data_[16 + std::size_t(index) * 8 + 4], 4);
    auto read32 = [&](std::size_t at, std::uint32_t& out) {
        if (at + 4 > data_.size()) return false;
        std::memcpy(&out, &data_[at], 4);
        return true;
    };
    std::uint32_t header;
    if (!read32(offset, header)) return true;
    std::uint32_t next = header;
    if ((next & 0xfe) == 0x7a && (next >>= 8) != 0) {
        std::size_t at = offset;
        do {
            std::uint32_t word;
            if (!read32(at + next, word)) return true;
            at += next;
            if (static_cast<std::uint8_t>(word) < 0x2e && ((0x261000000000ull >> (word & 0xff)) & 1)) {
                attachment_ = at;
                return true;
            }
            next = word >> 8;
        } while (next != 0);
    }
    return true;
}

bool FshFile::GetCurrentFrameInfo(FshFrameInfo& info) const {
    if (current_ >= entryCount_ || !GetFrame(current_, info)) return false;
    info.attachment = attachment_ ? &data_[attachment_] : nullptr;
    return true;
}

namespace {
inline void Rgb565(std::uint16_t v, std::uint8_t out[3]) {
    out[0] = static_cast<std::uint8_t>(((v >> 11) & 31) * 255 / 31);
    out[1] = static_cast<std::uint8_t>(((v >> 5) & 63) * 255 / 63);
    out[2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
}

// colour block shared by DXT1/3/5; returns whether the 3-colour (punch-through) mode applies
void ColourBlock(const std::uint8_t* b, std::uint8_t palette[4][4], bool dxt1) {
    std::uint16_t c0, c1;
    std::memcpy(&c0, b, 2);
    std::memcpy(&c1, b + 2, 2);
    Rgb565(c0, palette[0]);
    Rgb565(c1, palette[1]);
    palette[0][3] = palette[1][3] = 255;
    if (!dxt1 || c0 > c1) {
        for (int i = 0; i < 3; ++i) {
            palette[2][i] = static_cast<std::uint8_t>((2 * palette[0][i] + palette[1][i]) / 3);
            palette[3][i] = static_cast<std::uint8_t>((palette[0][i] + 2 * palette[1][i]) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) {
            palette[2][i] = static_cast<std::uint8_t>((palette[0][i] + palette[1][i]) / 2);
            palette[3][i] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
    }
}

template <class AlphaFn>
void DecodeBlocks(const std::uint8_t* src, int w, int h, std::uint8_t* rgba, int blockBytes, bool dxt1, AlphaFn alphaFn) {
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const std::uint8_t* block = src + (std::size_t(by) * bw + bx) * blockBytes;
            std::uint8_t pal[4][4];
            ColourBlock(block + blockBytes - 8, pal, dxt1);
            std::uint32_t bits;
            std::memcpy(&bits, block + blockBytes - 4, 4);
            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    const int x = bx * 4 + px, y = by * 4 + py;
                    const std::uint8_t* c = pal[(bits >> (2 * (py * 4 + px))) & 3];
                    if (x >= w || y >= h) continue;
                    std::uint8_t* o = rgba + (std::size_t(y) * w + x) * 4;
                    o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
                    o[3] = dxt1 ? c[3] : alphaFn(block, py * 4 + px);
                }
            }
        }
    }
}
}  // namespace

void DecodeDxt1(const std::uint8_t* src, int w, int h, std::uint8_t* rgba) {
    DecodeBlocks(src, w, h, rgba, 8, true, [](const std::uint8_t*, int) { return std::uint8_t(255); });
}

void DecodeDxt3(const std::uint8_t* src, int w, int h, std::uint8_t* rgba) {
    DecodeBlocks(src, w, h, rgba, 16, false, [](const std::uint8_t* block, int i) {
        const std::uint8_t byte = block[i / 2];
        const int a4 = (i & 1) ? byte >> 4 : byte & 15;
        return static_cast<std::uint8_t>(a4 * 17);
    });
}

void DecodeDxt5(const std::uint8_t* src, int w, int h, std::uint8_t* rgba) {
    DecodeBlocks(src, w, h, rgba, 16, false, [](const std::uint8_t* block, int i) {
        std::uint8_t a[8];
        a[0] = block[0];
        a[1] = block[1];
        if (a[0] > a[1]) {
            for (int k = 1; k < 7; ++k) a[k + 1] = static_cast<std::uint8_t>(((7 - k) * a[0] + k * a[1]) / 7);
        } else {
            for (int k = 1; k < 5; ++k) a[k + 1] = static_cast<std::uint8_t>(((5 - k) * a[0] + k * a[1]) / 5);
            a[6] = 0;
            a[7] = 255;
        }
        std::uint64_t bits = 0;
        for (int k = 0; k < 6; ++k) bits |= std::uint64_t(block[2 + k]) << (8 * k);
        return a[(bits >> (3 * i)) & 7];
    });
}

bool FshFile::DecodeRgba8(std::uint32_t index, std::vector<std::uint8_t>& rgba, int& width, int& height) const {
    FshFrameInfo f;
    if (!GetFrame(index, f) || f.width <= 0 || f.height <= 0) return false;
    width = f.width;
    height = f.height;
    const std::size_t n = std::size_t(width) * height;
    // the pixel data must lie inside the file
    const std::size_t avail = data_.size() - static_cast<std::size_t>(f.pixels - data_.data());
    const std::size_t need = n * PixelWidthMul2(f.code) / 2;
    if (PixelWidthMul2(f.code) == 0 || (f.code != 0x60 && need > avail)) return false;
    rgba.assign(n * 4, 255);
    const std::uint8_t* p = f.pixels;
    switch (f.code) {
        case 0x60: DecodeDxt1(p, width, height, rgba.data()); return true;
        case 0x61: DecodeDxt3(p, width, height, rgba.data()); return true;
        case 0x62: DecodeDxt5(p, width, height, rgba.data()); return true;
        case 0x7d:
            for (std::size_t i = 0; i < n; ++i) {       // stored B, G, R, A
                rgba[i * 4 + 0] = p[i * 4 + 2];
                rgba[i * 4 + 1] = p[i * 4 + 1];
                rgba[i * 4 + 2] = p[i * 4 + 0];
                rgba[i * 4 + 3] = p[i * 4 + 3];
            }
            return true;
        case 0x7f:
            for (std::size_t i = 0; i < n; ++i) {
                rgba[i * 4 + 0] = p[i * 3 + 2];
                rgba[i * 4 + 1] = p[i * 3 + 1];
                rgba[i * 4 + 2] = p[i * 3 + 0];
            }
            return true;
        case 0x78:
            for (std::size_t i = 0; i < n; ++i) {
                std::uint16_t v;
                std::memcpy(&v, p + i * 2, 2);
                Rgb565(v, &rgba[i * 4]);
            }
            return true;
        case 0x7e:
            for (std::size_t i = 0; i < n; ++i) {
                std::uint16_t v;
                std::memcpy(&v, p + i * 2, 2);
                rgba[i * 4 + 0] = static_cast<std::uint8_t>(((v >> 10) & 31) * 255 / 31);
                rgba[i * 4 + 1] = static_cast<std::uint8_t>(((v >> 5) & 31) * 255 / 31);
                rgba[i * 4 + 2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
                rgba[i * 4 + 3] = (v & 0x8000) ? 255 : 0;
            }
            return true;
        case 0x6d:
            for (std::size_t i = 0; i < n; ++i) {
                std::uint16_t v;
                std::memcpy(&v, p + i * 2, 2);
                rgba[i * 4 + 0] = static_cast<std::uint8_t>(((v >> 8) & 15) * 17);
                rgba[i * 4 + 1] = static_cast<std::uint8_t>(((v >> 4) & 15) * 17);
                rgba[i * 4 + 2] = static_cast<std::uint8_t>((v & 15) * 17);
                rgba[i * 4 + 3] = static_cast<std::uint8_t>(((v >> 12) & 15) * 17);
            }
            return true;
        default: return false;     // 0x7b paletted textures need the palette attachment (5 of ~16k in the game)
    }
}

}  // namespace s3d
