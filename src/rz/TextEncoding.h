// Text encoding helpers of the RZ layer (0x10001aca3..0x10001dd1a): code page lookup, multibyte character
// lengths, UTF-8 / UTF-16 conversion and the Thai combining-mark rules used for caret movement.
// Code page ids: 0 = ASCII, 8 = UTF-8, 0x10 = UTF-16, -1 = system code page, -2 = UTF-8 (framework),
// 0x3a4/0x3a8/0x3b5/0x3b6 = double byte pages (Shift-JIS/GBK/Korean/Big5 lead byte tables), 0x6faf = Mac Roman
// (converted through CoreFoundation in the original; unsupported here).
#pragma once
#include <cstdint>

int GetCurrentSystemCodePage();
int GetCurrentFrameworkCodePage();                 // always 8
bool MBCSIsLeadByte(char byte, int codePage);
bool IsLeadByte(char byte, int codePage);
int GetFollowByteCount(char byte, int codePage);
int UTF8GetFollowByteCount(unsigned char byte);
unsigned GetPreviousCharLength(const void* end, unsigned available, int codePage);
unsigned GetGlyphCountFromString(const void* text, unsigned size, int codePage);
int LengthFromGlyphCount(const void* text, unsigned glyphs, int codePage);
unsigned GetComposedCharLength(const void* text, unsigned size, int codePage);
unsigned GetPreviousComposedCharLength(const void* end, unsigned available, int codePage);
bool UTF16IsThaiChar(unsigned ch);
unsigned UTF16ThaiCharGroupLength(const std::uint16_t* text, unsigned count);
bool UTF16IsThaiDecorationChar(unsigned ch);
bool UTF8IsStringValid(const std::uint8_t* text, unsigned size);
bool IsStringValid(const void* text, unsigned size, int codePage);
// Converts `size` source units from `sourcePage` into `out`; `outCapacity` carries the capacity (in
// destination units) in and the produced units out. Returns the number of source units consumed.
unsigned ConvertStringEncoding(const void* source, unsigned size, int sourcePage, void* out, unsigned& outCapacity,
                               int destinationPage);
std::uint16_t GetUTF16Character(const void* text, unsigned& size, int codePage);
std::uint16_t GetPreviousUTF16Character(const void* end, unsigned& size, int codePage);
