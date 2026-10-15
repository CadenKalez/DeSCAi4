// String / number utility functions of the RZ layer (0x100019aac..0x10001a95e, 0x10001ac1b..0x10001ac86).
// All keep the original quirks: RZToupperWImp / RZTolowerWImp shift *every* value up to 0x7f by 0x20
// (not only letters); the hex parsers read at most 8 / 16 digits; RZFtoaEnglishA formats through fcvt.
#pragma once
#include <cstdint>
#include <string>

#include "rz/cRZString.h"

char* RZStruprAImp(char* text);
char* RZStrlwrAImp(char* text);
int RZToupperWImp(int ch);
int RZTolowerWImp(int ch);
double RZAtofA(const char* text);
double RZAtofEnglishA(const char* text);
double RZStrtodEnglishA(const char* text, char** end);
// Formats `value` without exponent into `out` (at most `size` bytes including the terminator), keeping up
// to `maxFraction` fractional digits and trimming trailing zeros. False when the buffer was too small.
bool RZFtoaEnglishA(double value, char* out, int size, int maxFraction);
std::int64_t RZAtoi64A(const char* text);
std::uint32_t RZAtoUint32HexA(const char* text);
std::uint64_t RZAtoUint64HexA(const char* text);
std::int64_t RZAtoLongHexA(const char* text);
void RZItoaAImp(int value, char* out, int radix);
char* RZLtoaAImp(long value, char* out, int radix);
// Splits `text` on `delimiter` (space also splits on tabs), honouring double quotes. `count` carries the
// capacity of `out` in and the number of items out. True when the whole text was consumed.
bool RZParseDelimitedText(const char* text, unsigned length, char delimiter, cRZString* out, unsigned& count);
void ConvertBinaryDataToASCIIArray(const std::uint8_t* data, unsigned size, char* out);   // upper-case hex + '0'
bool ConvertASCIIArrayToBinaryData(const char* text, unsigned size, std::uint8_t* out);
void memset8(void* destination, std::uint8_t value, unsigned count);
void memset16(void* destination, std::uint16_t value, unsigned count);
void memset32(void* destination, std::uint32_t value, unsigned count);
