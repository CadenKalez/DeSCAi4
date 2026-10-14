#include "rz/CharLength.h"

int GetCharLength(const void* data, std::uint32_t remaining, int codePage) {
    if (remaining == 0) return 0;
    if (codePage != -2 && codePage != 8) return 1;   // single byte (0, 16) or an unrecovered MBCS page
    const auto* p = static_cast<const unsigned char*>(data);
    const unsigned char lead = p[0];
    int length = 1;
    if (lead >= 0x80 && static_cast<unsigned char>(lead + 0x40) < 0x3e && remaining > 1 && p[1] >= 0x80 && p[1] < 0xc0) {
        length = 2;
        if (lead > 0xdf) {
            length = 3;
            if (lead > 0xef) {
                length = 4;
                if (lead > 0xf7) length = 6 - (lead < 0xfc ? 1 : 0);
            }
        }
    }
    return length;
}

bool SC4LimitStringLength(cIGZString& text, std::uint32_t maxBytes) {
    const std::uint32_t total = text.Strlen();
    const auto* chars = reinterpret_cast<const unsigned char*>(text.Data());
    std::uint32_t kept = 0;
    if (total != 0) {
        for (;;) {
            const int length = GetCharLength(chars + kept, total - kept, -2);
            if (length == 0) break;
            const std::uint32_t next = kept + static_cast<std::uint32_t>(length);
            if (next <= maxBytes) kept = next;
            if (kept >= total || maxBytes < next) break;
        }
    }
    if (kept < total) text.Resize(kept);
    return kept < total;
}
