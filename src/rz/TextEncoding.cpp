#include "rz/TextEncoding.h"

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "rz/CharLength.h"

namespace {
int g_systemCodePage = -3;
}

int GetCurrentSystemCodePage() {
    if (g_systemCodePage == -3) {
        g_systemCodePage = 0;
        const char* locale = std::setlocale(LC_ALL, nullptr);
        if (locale) {
            if (std::strcmp(locale, "C") == 0 && std::setlocale(LC_ALL, "") == nullptr) return g_systemCodePage;
            if (const char* dot = std::strchr(locale, '.')) g_systemCodePage = std::atoi(dot + 1);
        }
    }
    return g_systemCodePage;
}

int GetCurrentFrameworkCodePage() { return 8; }

bool MBCSIsLeadByte(char byte, int codePage) {
    const auto b = static_cast<unsigned char>(byte);
    if (codePage < 0x3b5) {
        if (codePage == 0x3a4) {                          // Shift-JIS
            if (static_cast<unsigned char>(b + 0x7f) <= 0x1e) return true;
            return static_cast<unsigned char>(b + 0x20) < 0x1d;
        }
        if (codePage == 0x3a8) return static_cast<unsigned char>(b + 0x5f) < 0x5c;     // GBK
        return false;
    }
    if (codePage == 0x3b5) return static_cast<unsigned char>(b + 0x50) < 0x19;         // Korean
    if (codePage == 0x3b6) return b != 0xff && b > 0x80;                               // Big5
    return false;
}

bool IsLeadByte(char byte, int codePage) {
    if (codePage != -2) {
        if (codePage == -1) codePage = GetCurrentSystemCodePage();
        if (codePage != 8) {
            if (codePage < 100) return false;
            return MBCSIsLeadByte(byte, codePage);
        }
    }
    return static_cast<unsigned char>(byte + 0x40) < 0x3e;
}

int UTF8GetFollowByteCount(unsigned char b) {
    if (b <= 0x80) return 0;
    if (static_cast<unsigned char>(b + 0x3e) <= 0x1d) return 1;
    if ((b & 0xf0) == 0xe0) return 2;
    if ((b & 0xf8) == 0xf0) return 3;
    if ((b & 0xfc) == 0xf8) return 4;
    return (b & 0xfe) == 0xfc ? 5 : 0;
}

int GetFollowByteCount(char byte, int codePage) {
    if (codePage != -2) {
        if (codePage == -1) codePage = GetCurrentSystemCodePage();
        if (codePage != 8) return codePage >= 100 ? (MBCSIsLeadByte(byte, codePage) ? 1 : 0) : 0;
    }
    return UTF8GetFollowByteCount(static_cast<unsigned char>(byte));
}

unsigned GetPreviousCharLength(const void* end, unsigned available, int codePage) {
    if (available == 0) return 0;
    const auto* p = static_cast<const unsigned char*>(end);
    if (codePage != -2) {
        if (codePage == -1) codePage = GetCurrentSystemCodePage();
        if ((codePage | 0x10) == 0x10) return 1;
        if (codePage != 8) {
            // forward scan over the whole run: the last character is two bytes when it ends a lead/trail pair
            const unsigned char* begin = p - available;
            unsigned lastLength = 1;
            for (const unsigned char* q = begin; q < p;) {
                lastLength = MBCSIsLeadByte(static_cast<char>(*q), codePage) && q + 1 < p ? 2 : 1;
                q += lastLength;
            }
            return lastLength;
        }
    }
    unsigned i = 0;
    for (;;) {
        const unsigned char b = p[-static_cast<long>(i + 1)];
        if (b < 0x80) return i + 1;
        if (b > 0xbf) return i + 1;
        ++i;
        if (available == i) return i + 1;
    }
}

unsigned GetGlyphCountFromString(const void* text, unsigned size, int codePage) {
    if (size == 0) return 0;
    const auto* p = static_cast<const unsigned char*>(text);
    const unsigned char* end = p + size;
    if (codePage != -2) {
        if (codePage == -1) codePage = GetCurrentSystemCodePage();
        if ((codePage | 0x10) == 0x10) return size;
        if (codePage != 8) {
            unsigned glyphs = 0;
            do {
                if (MBCSIsLeadByte(static_cast<char>(*p), codePage)) ++p;
                ++p;
                ++glyphs;
            } while (p < end);
            return glyphs;
        }
    }
    unsigned glyphs = 0;
    for (; p < end; ++p) glyphs += ((*p >> 7) ^ 1) | (*p > 0xbf ? 1 : 0);
    return glyphs;
}

int LengthFromGlyphCount(const void* text, unsigned glyphs, int codePage) {
    if (codePage == -1) codePage = GetCurrentSystemCodePage();
    else if (codePage == -2) codePage = 8;
    int length = 0;
    const auto* p = static_cast<const unsigned char*>(text);
    while (glyphs != 0) {
        const int n = GetCharLength(p, 0x3fffffff, codePage);
        length += n;
        p += n * (codePage == 0x10 ? 2 : 1);
        --glyphs;
    }
    return length;
}

bool UTF16IsThaiChar(unsigned ch) { return (ch & 0xffffff80u) == 0xe00; }

bool UTF16IsThaiDecorationChar(unsigned ch) {
    // non-spacing marks: 0E31, 0E34..0E3A, 0E47..0E4E
    if (ch - 0xe31u > 0x1d) return false;
    return ch == 0xe31 || (ch >= 0xe34 && ch <= 0xe3a) || (ch >= 0xe47 && ch <= 0xe4e);
}

unsigned UTF16ThaiCharGroupLength(const std::uint16_t* text, unsigned count) {
    if (count == 0) return 0;
    if (count == 1) return 1;
    if (text[1] != 0xe33) {
        if (!UTF16IsThaiDecorationChar(text[1])) return 1;
        if (count > 2) {
            if (text[2] == 0xe33) return 3;
            if (!UTF16IsThaiDecorationChar(text[2])) return 2;
            return (text[3] == 0xe33) + 3;
        }
    }
    return 2;
}

unsigned GetComposedCharLength(const void* text, unsigned size, int codePage) {
    if (size == 0) return 0;
    if (codePage == 0x10) {
        const auto* w = static_cast<const std::uint16_t*>(text);
        if ((w[0] & 0xff80) == 0xe00) return UTF16ThaiCharGroupLength(w, size);
        return 1;
    }
    if (codePage == 8) {
        const auto* b = static_cast<const char*>(text);
        if (b[0] >= 0) return 1;
        std::uint16_t units[3];
        unsigned produced = 3;
        if (ConvertStringEncoding(text, size, 8, units, produced, 0x10) != 0 && (units[0] & 0xff80) == 0xe00) {
            unsigned glyphs = UTF16ThaiCharGroupLength(units, produced);
            unsigned total = 0;
            const auto* p = static_cast<const unsigned char*>(text);
            while (glyphs--) {
                const unsigned n = static_cast<unsigned>(GetCharLength(p, 0x3fffffff, 8));
                total += n;
                p += n;
            }
            return total;
        }
    }
    return static_cast<unsigned>(GetCharLength(text, size, codePage));
}

unsigned GetPreviousComposedCharLength(const void* end, unsigned available, int codePage) {
    if (available == 0) return 0;
    if (codePage == 0x10) {
        const auto* w = static_cast<const std::uint16_t*>(end);
        if (w[-1] <= 0x7f) return 1;
        unsigned count = 0;
        const std::uint16_t* p = w;
        int remaining = -static_cast<int>(available);
        bool decoration;
        do {
            ++remaining;
            --p;
            decoration = UTF16IsThaiDecorationChar(*p);
            if (remaining == 0) break;
        } while (decoration);
        count = static_cast<unsigned>(w - p);
        return count;
    }
    if (codePage != 8) return GetPreviousCharLength(end, available, codePage);
    const auto* b = static_cast<const char*>(end);
    if (b[-1] >= 0) return 1;
    const char* p = b;
    bool decoration;
    do {
        const unsigned n = GetPreviousCharLength(p, available, 8);
        p -= n;
        std::uint16_t unit = 0;
        unsigned produced = 1;
        ConvertStringEncoding(p, n, 8, &unit, produced, 0x10);
        decoration = UTF16IsThaiDecorationChar(unit);
        available -= n;
        if (available == 0) break;
    } while (decoration);
    return static_cast<unsigned>(b - p);
}

bool UTF8IsStringValid(const std::uint8_t* s, unsigned size) {
    const std::uint8_t* end = s + size;
    auto cont = [&](const std::uint8_t* p, long n) {
        if (end - p < n) return false;
        for (long i = 1; i < n; ++i)
            if (static_cast<std::int8_t>(p[i]) > -0x41) return false;
        return true;
    };
    while (s < end) {
        const std::uint8_t b = *s;
        long length = 1;
        if (b & 0x80) {
            if (b < 0xc2) return false;
            if (b < 0xe0) { if (!cont(s, 2)) return false; length = 2; }
            else if (b < 0xf0) {
                if (!cont(s, 3)) return false;
                if (b < 0xe1 && s[1] < 0xa0) return false;
                length = 3;
            } else if (b < 0xf8) {
                if (!cont(s, 4)) return false;
                if (b < 0xf1 && s[1] < 0x90) return false;
                length = 4;
            } else if (b < 0xfc) {
                if (!cont(s, 5)) return false;
                if (b < 0xf9 && s[1] < 0x88) return false;
                length = 5;
            } else {
                if (b > 0xfd || !cont(s, 6)) return false;
                if (b < 0xfd && s[1] < 0x84) return false;
                length = 6;
            }
        }
        s += length;
    }
    return s == end;
}

bool IsStringValid(const void* text, unsigned size, int codePage) {
    if (codePage == -2) return UTF8IsStringValid(static_cast<const std::uint8_t*>(text), size);
    if (codePage == -1) codePage = GetCurrentSystemCodePage();
    if (codePage == 0) {
        const auto* p = static_cast<const char*>(text);
        for (unsigned i = 0; i < size; ++i)
            if (p[i] < 0) return false;
        return true;
    }
    if (codePage == 0x10) {
        const auto* w = static_cast<const std::int16_t*>(text);
        for (unsigned i = 0; i < size; ++i)
            if (static_cast<std::uint16_t>(w[i] + 2) <= 2) return false;       // 0xfffe, 0xffff, 0
        return true;
    }
    if (codePage == 8) return UTF8IsStringValid(static_cast<const std::uint8_t*>(text), size);
    return true;
}

// ------------------------------------------------------------------------------------ conversion

static unsigned ConvertViaUTF16(const void* source, unsigned size, int sourcePage, void* out, unsigned& capacity,
                                int destinationPage) {
    std::vector<char> scratch;
    unsigned unitCount = size * 2 + 4;
    scratch.resize(unitCount);
    unsigned produced = unitCount;
    const unsigned consumed = ConvertStringEncoding(source, size, sourcePage, scratch.data(), produced, 0x10);
    if (consumed == size) {
        const unsigned back = ConvertStringEncoding(scratch.data(), produced, 0x10, out, capacity, destinationPage);
        if (back == produced) return size;
    }
    capacity = 0;
    return 0;
}

unsigned ConvertStringEncoding(const void* source, unsigned size, int sourcePage, void* out, unsigned& capacity,
                               int destinationPage) {
    if (size == 0) {
        capacity = 0;
        return 0;
    }
    if (sourcePage == -1) sourcePage = GetCurrentSystemCodePage();
    else if (sourcePage == -2) sourcePage = 8;
    if (destinationPage == -1) destinationPage = GetCurrentSystemCodePage();
    else if (destinationPage == -2) destinationPage = 8;

    int effective = 0x10;
    if (sourcePage != 0x10) {
        effective = 0;
        const auto* bytes = static_cast<const char*>(source);
        for (unsigned i = 0; i < size; ++i)
            if (bytes[i] < 0) { effective = sourcePage; break; }
    }
    unsigned units = size;
    if (effective == destinationPage || (destinationPage != 0x10 && effective == 0) ||
        (destinationPage == 0 && effective != 0x10)) {
        if (source != out) {
            if (capacity < size) size = capacity;
            units = size;
            std::memcpy(out, source, destinationPage == 0x10 ? static_cast<std::size_t>(size) * 2 : size);
        } else if (capacity < size) {
            units = capacity;
        }
        capacity = units;
        return units;
    }
    if (destinationPage == 0 && effective == 0x10) {
        if (capacity < size) size = capacity;
        const auto* w = static_cast<const std::uint16_t*>(source);
        auto* o = static_cast<std::uint8_t*>(out);
        for (unsigned i = 0; i < size; ++i) o[i] = static_cast<std::uint8_t>(w[i]);
        capacity = size;
        return size;
    }
    if (source == out) return 0;
    if (destinationPage == 0x10 && effective == 8) {
        const auto* in = static_cast<const std::uint8_t*>(source);
        const std::uint8_t* inEnd = in + size;
        auto* o = static_cast<std::uint16_t*>(out);
        std::uint16_t* oEnd = o + capacity;
        std::uint16_t* op = o;
        const std::uint8_t* ip = in;
        while (op < oEnd) {
            const std::uint8_t b = *ip;
            std::uint16_t unit = b;
            long length = 1;
            if (b & 0x80) {
                if (b < 0xc2) break;
                if (b < 0xe0) {
                    if (inEnd - ip < 2 || (ip[1] ^ 0x80) > 0x3f) break;
                    unit = static_cast<std::uint16_t>(((b & 0x1f) << 6) | (ip[1] ^ 0x80));
                    length = 2;
                } else if (b < 0xf0) {
                    if (inEnd - ip < 3 || static_cast<std::int8_t>(ip[1]) > -0x41 ||
                        static_cast<std::int8_t>(ip[2]) > -0x41 || (b < 0xe1 && ip[1] < 0xa0)) break;
                    unit = static_cast<std::uint16_t>((ip[2] ^ 0x80) | ((ip[1] ^ 0x80) << 6) | (static_cast<unsigned>(b) << 12));
                    length = 3;
                } else if (b < 0xf8) {
                    if (inEnd - ip < 4 || static_cast<std::int8_t>(ip[1]) > -0x41 ||
                        static_cast<std::int8_t>(ip[2]) > -0x41 || static_cast<std::int8_t>(ip[3]) > -0x41 ||
                        (b < 0xf1 && ip[1] < 0x90)) break;
                    unit = static_cast<std::uint16_t>((ip[3] ^ 0x80) | ((ip[2] ^ 0x80) << 6) | (static_cast<unsigned>(ip[1]) << 12));
                    length = 4;
                } else if (b < 0xfc) {
                    if (inEnd - ip < 5 || static_cast<std::int8_t>(ip[1]) > -0x41 ||
                        static_cast<std::int8_t>(ip[2]) > -0x41 || static_cast<std::int8_t>(ip[3]) > -0x41 ||
                        static_cast<std::int8_t>(ip[4]) > -0x41 || (b < 0xf9 && ip[1] < 0x88)) break;
                    unit = static_cast<std::uint16_t>((ip[4] ^ 0x80) | ((ip[3] ^ 0x80) << 6) | (static_cast<unsigned>(ip[2]) << 12));
                    length = 5;
                } else {
                    if (b > 0xfd || inEnd - ip < 6 || static_cast<std::int8_t>(ip[1]) > -0x41 ||
                        static_cast<std::int8_t>(ip[2]) > -0x41 || static_cast<std::int8_t>(ip[3]) > -0x41 ||
                        static_cast<std::int8_t>(ip[4]) > -0x41 || static_cast<std::int8_t>(ip[5]) > -0x41 ||
                        (b < 0xfd && ip[1] < 0x84)) break;
                    unit = static_cast<std::uint16_t>((ip[5] ^ 0x80) | ((ip[4] ^ 0x80) << 6) | (static_cast<unsigned>(ip[3]) << 12));
                    length = 6;
                }
            }
            *op++ = unit;
            ip += length;
            if (op >= oEnd || ip >= inEnd) break;
        }
        capacity = static_cast<unsigned>(op - o);
        return static_cast<unsigned>(ip - in);
    }
    if (destinationPage == 8 && effective == 0x10) {
        const auto* w = static_cast<const std::uint16_t*>(source);
        const std::uint16_t* wEnd = w + size;
        auto* o = static_cast<std::uint8_t*>(out);
        std::uint8_t* op = o;
        const unsigned room = capacity;
        const std::uint16_t* p = w;
        while (p < wEnd) {
            const std::uint16_t c = *p;
            const unsigned length = c < 0x80 ? 1 : (c < 0x800 ? 2 : 3);
            if (static_cast<unsigned>(op - o) + length > room) break;
            if (length == 1) {
                op[0] = static_cast<std::uint8_t>(c);
            } else if (length == 2) {
                op[1] = static_cast<std::uint8_t>((c & 0x3f) | 0x80);
                op[0] = static_cast<std::uint8_t>((c >> 6) | 0xc0);
            } else {
                op[2] = static_cast<std::uint8_t>((c & 0x3f) | 0x80);
                const unsigned mid = (c >> 6) | 0x800;
                op[1] = static_cast<std::uint8_t>((mid & 0x3f) | 0x80);
                op[0] = static_cast<std::uint8_t>((mid >> 6) | 0xc0);
            }
            op += length;
            ++p;
        }
        capacity = static_cast<unsigned>(op - o);
        return static_cast<unsigned>(p - w);
    }
    if (destinationPage == 0x10 && effective == 0) {
        if (capacity < size) size = capacity;
        const auto* b = static_cast<const std::uint8_t*>(source);
        auto* o = static_cast<std::uint16_t*>(out);
        for (unsigned i = 0; i < size; ++i) o[i] = b[i];
        capacity = size;
        return size;
    }
    if (destinationPage == 8 && effective != 0x10) return ConvertViaUTF16(source, size, effective, out, capacity, 8);
    if (destinationPage == 0x10 || effective != 8) return 0;      // (Mac Roman -> UTF-16 used CoreFoundation)
    return ConvertViaUTF16(source, size, effective, out, capacity, destinationPage);
}

std::uint16_t GetUTF16Character(const void* text, unsigned& size, int codePage) {
    std::uint16_t unit = 0;
    if (size == 0) return 0;
    const auto* b = static_cast<const std::uint8_t*>(text);
    if (codePage != 0x10 && b[0] <= 0x7e) return b[0];
    unsigned produced = 1;
    size = ConvertStringEncoding(text, size, codePage, &unit, produced, 0x10);
    return unit;
}

std::uint16_t GetPreviousUTF16Character(const void* end, unsigned& size, int codePage) {
    if (size == 0) return 0;
    const auto* b = static_cast<const std::uint8_t*>(end);
    if (codePage != 0x10 && b[-1] <= 0x7e) return b[-1];
    std::uint16_t unit = 0;
    unsigned produced = 1;
    const unsigned length = GetPreviousCharLength(end, size, codePage);
    size = length;
    size = ConvertStringEncoding(static_cast<const char*>(end) - length, length, codePage, &unit, produced, 0x10);
    return unit;
}
