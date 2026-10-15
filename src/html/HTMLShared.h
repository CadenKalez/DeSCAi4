// cHTMLShared — attribute value helpers shared by the HTML elements (5 functions, 0x1005c148a..0x1005c21ce).
#pragma once
#include <cstdint>

#include "html/HTMLTypes.h"

namespace html {

struct tLength {                     // a width / height attribute: pixels or percent
    enum Unit : std::int32_t { kPixels = 0, kPercent = 1, kUnset = 2 };
    std::int32_t unit = kPixels;
    std::uint32_t value = 0;
};

class cHTMLShared {
public:
    // "#RRGGBB", "RRGGBB" or one of the sixteen HTML colour names (case-insensitive) -> 0xffRRGGBB; leaves `color`
    // untouched for anything else.
    static void SetColor(const cRZString& text, Color& color);
    // "NN%" -> percent, otherwise pixels (the number is parsed with ConvertToUint, so hex and decimal both work).
    static void SetLength(const cRZString& text, tLength& length);
    static void SetPixels(const cRZString& text, std::uint32_t& pixels);
    // First `count` characters of `text` equal the first `count` of `literal` (false when `text` is shorter or `literal` ends early).
    static bool StringLeftEquals(const cRZString& text, std::uint32_t count, const char* literal);
    static bool StringLeftEquals(const cRZString& text, char c);
};

}  // namespace html
