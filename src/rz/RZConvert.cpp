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

#include <cctype>
#include <cstdlib>

namespace RZ {

std::uint64_t ConvertHexToUint64(const char* text) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (std::isspace(*p)) ++p;
    int skip = 0;
    if (p[0] == '0' && (p[1] | 0x20) == 'x') skip = 2;
    std::uint64_t value = 0;
    for (int i = 0; i < 16; ++i) {
        const unsigned char c = p[skip + i];
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break;
        value = value * 16 + static_cast<std::uint64_t>(digit);
    }
    return value;
}

double ConvertEnglishToDouble(const char* text) noexcept { return std::strtod(text, nullptr); }

void RemoveUnnecessaryFloatingPointDigits(char* text, int length) noexcept {
    if (length == 0) return;
    if (length > 0) {
        int dot = -1;
        int exponent = -1;
        for (int i = 0; i < length; ++i) {
            if (text[i] == '.') dot = i;
            if ((static_cast<unsigned char>(text[i]) | 0x20) == 'e') exponent = i;
        }
        const int fractionStart = dot >= 0 ? dot + 1 : 0;
        int end = exponent >= 0 ? exponent : length;
        int size = length;
        // strip trailing zeros of the fraction (shifting the exponent part down)
        while (fractionStart < end && text[end - 1] == '0') {
            for (int j = end - 1; j < size; ++j) text[j] = text[j + 1];
            --size;
            --end;
        }
        length = size;
        if (dot >= 0 && dot == length - 1) {      // nothing left after the decimal point
            text[dot] = '\0';
            length = dot;
        }
        if (length > 0) {
            int i = 0;
            while (i < length && (text[i] == '0' || text[i] == '.')) ++i;
            if (i == length) text[0] = '0';   // only zeros/points (the original writes just the first char)
            return;
        }
    }
    if (length == 0) text[0] = '0';   // (reached when the text cut down to nothing)
}

}  // namespace RZ
