#include "rz/RZConvert.h"

#include <cctype>
#include <cstdlib>

namespace RZ {

std::uint32_t ConvertToUint(const char* text) noexcept {
    int base = 10;
    if (text[0] == '0' && (static_cast<unsigned char>(text[1]) | 0x20) == 'x') {
        base = 16;
    } else {
        for (const char* p = text;; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) { base = 16; break; }
            if (c == '\0' || c < '0' || c > '9') break;  // decimal
        }
    }
    return static_cast<std::uint32_t>(std::strtoul(text, nullptr, base));
}

std::uint32_t ConvertHexToUint(const char* text) noexcept {
    while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text))) ++text;
    if (text[0] == '0' && (static_cast<unsigned char>(text[1]) | 0x20) == 'x') text += 2;
    std::uint32_t value = 0;
    for (int i = 0; i < 8; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break;
        value = (value << 4) | digit;
    }
    return value;
}

}  // namespace RZ
