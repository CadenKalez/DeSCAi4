#include "rz/nRZHash.h"

#include <array>
#include <cctype>

namespace {

struct Tables {
    std::array<std::uint32_t, 256> crc24{};
    std::array<std::uint32_t, 256> crc32{};
    Tables() {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t x = i << 16;
            for (int bit = 0; bit < 8; ++bit) {
                x <<= 1;
                if (x & 0x1000000u) x ^= 0x1864cfbu;
            }
            crc24[i] = x;
            std::uint32_t y = i << 24;
            for (int bit = 0; bit < 8; ++bit) y = (y & 0x80000000u) ? (y << 1) ^ 0x04c11db7u : (y << 1);
            crc32[i] = y;
        }
    }
};

const Tables& GetTables() {
    static const Tables tables;
    return tables;
}

}  // namespace

std::uint32_t nRZHash::CRC24(const char* text, bool lowerCase) noexcept {
    if (*text == '\0') return 0xb704ceu;
    const auto& t = GetTables().crc24;
    std::uint32_t crc = 0xb704ceu;
    for (; *text != '\0'; ++text) {
        auto c = static_cast<std::int32_t>(static_cast<signed char>(*text));
        if (lowerCase) c = std::tolower(c);
        crc = (crc << 8) ^ t[((crc >> 16) ^ static_cast<std::uint32_t>(c)) & 0xff];
    }
    return crc & 0xffffffu;
}

std::uint32_t nRZHash::CRC32(const unsigned char* data, std::uint32_t length) noexcept {
    const auto& t = GetTables().crc32;
    std::uint32_t crc = 0xffffffffu;
    for (; length != 0; --length, ++data) crc = (crc << 8) ^ t[(crc >> 24) ^ *data];
    return crc;
}
