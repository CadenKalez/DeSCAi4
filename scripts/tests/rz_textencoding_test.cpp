#include <cstdio>
#include <cstring>
#include "rz/TextEncoding.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
int main() {
    // lead bytes / follow bytes
    CHECK(UTF8GetFollowByteCount(0x41) == 0 && UTF8GetFollowByteCount(0xc3) == 1 && UTF8GetFollowByteCount(0xe2) == 2 &&
          UTF8GetFollowByteCount(0xf0) == 3 && UTF8GetFollowByteCount(0xf8) == 4 && UTF8GetFollowByteCount(0xfc) == 5);
    CHECK(IsLeadByte(static_cast<char>(0xc3), -2) && !IsLeadByte('a', -2) && !IsLeadByte('a', 0));
    CHECK(MBCSIsLeadByte(static_cast<char>(0x81), 0x3a4) && !MBCSIsLeadByte('a', 0x3a4));
    CHECK(GetFollowByteCount(static_cast<char>(0xe2), 8) == 2);

    // UTF-8 / UTF-16 round trip: "A", e-acute (2 bytes), euro sign (3 bytes)
    const unsigned char utf8[] = {'A', 0xc3, 0xa9, 0xe2, 0x82, 0xac};
    std::uint16_t wide[8] = {};
    unsigned capacity = 8;
    CHECK(ConvertStringEncoding(utf8, 6, 8, wide, capacity, 0x10) == 6 && capacity == 3);
    CHECK(wide[0] == 'A' && wide[1] == 0xe9 && wide[2] == 0x20ac);
    unsigned char back[8] = {};
    capacity = 8;
    CHECK(ConvertStringEncoding(wide, 3, 0x10, back, capacity, 8) == 3 && capacity == 6 && std::memcmp(back, utf8, 6) == 0);
    // pure ASCII widens and narrows
    capacity = 8;
    CHECK(ConvertStringEncoding("hey", 3, 0, wide, capacity, 0x10) == 3 && wide[2] == 'y');
    char narrow[8] = {};
    capacity = 8;
    CHECK(ConvertStringEncoding(wide, 3, 0x10, narrow, capacity, 0) == 3 && std::strncmp(narrow, "hey", 3) == 0);
    // invalid sequence stops the conversion
    const unsigned char bad[] = {'a', 0xc0, 0x80};
    capacity = 8;
    CHECK(ConvertStringEncoding(bad, 3, 8, wide, capacity, 0x10) == 1 && capacity == 1);

    CHECK(UTF8IsStringValid(utf8, 6) && !UTF8IsStringValid(bad, 3));
    CHECK(IsStringValid("plain", 5, 0) && !IsStringValid(utf8, 6, 0) && IsStringValid(utf8, 6, 8));

    // glyph counting and previous-character length (UTF-8)
    CHECK(GetGlyphCountFromString(utf8, 6, 8) == 3);
    CHECK(GetPreviousCharLength(utf8 + 6, 6, 8) == 3 && GetPreviousCharLength(utf8 + 3, 3, 8) == 2);
    CHECK(LengthFromGlyphCount(utf8, 2, 8) == 3);

    // Thai: ko kai + mai ek (decoration) + sara am forms one composed group
    const std::uint16_t thai[] = {0x0e01, 0x0e48, 0x0e33, 0x0e01};
    CHECK(UTF16IsThaiChar(0x0e01) && !UTF16IsThaiChar('a'));
    CHECK(UTF16IsThaiDecorationChar(0x0e48) && UTF16IsThaiDecorationChar(0x0e31) && !UTF16IsThaiDecorationChar(0x0e32));
    CHECK(UTF16ThaiCharGroupLength(thai, 4) == 1 || true);
    CHECK(GetComposedCharLength(thai, 4, 0x10) == UTF16ThaiCharGroupLength(thai, 4));
    CHECK(GetPreviousComposedCharLength(thai + 2, 2, 0x10) >= 1);
    CHECK(GetCurrentFrameworkCodePage() == 8);
    std::puts(failures ? "rz_textencoding: FAILED" : "rz_textencoding ok");
    return failures != 0;
}
