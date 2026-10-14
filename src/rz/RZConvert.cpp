#include "rz/RZConvert.h"

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

}  // namespace RZ
