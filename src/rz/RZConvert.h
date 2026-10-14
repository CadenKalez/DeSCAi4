// String-to-number helpers of the RZ framework.
#pragma once
#include <cstdint>

namespace RZ {

// RZConvertToUintPtrA(char const*) @ 0x10001a307
// Parses an unsigned integer: "0x"/"0X" prefix or any A-F/a-f before the first non-digit selects
// base 16, otherwise base 10 (strtoul semantics, truncated to 32 bits).
std::uint32_t ConvertToUint(const char* text) noexcept;

// RZAtoUint32HexA(char const*) @ 0x100019fcf
// Skips leading whitespace, an optional "0x"/"0X", then accumulates up to eight hex digits
// (stopping at the first non-hex character). Returns 0 when there are no digits.
std::uint32_t ConvertHexToUint(const char* text) noexcept;

}  // namespace RZ

namespace RZ {

// RZAtoUint64HexA @ 0x10001a0d1 / RZAtoLongHexA @ 0x10001a1ec (identical code): skips leading
// whitespace, an optional "0x"/"0X", then accumulates at most 16 hex digits (stopping at the first
// non-hex character); 0 when there are none.
std::uint64_t ConvertHexToUint64(const char* text) noexcept;

// RZAtofEnglishA @ 0x100019b4c (RZStrtodEnglishA): a locale-independent decimal parse ('.' is the
// decimal point). The port uses strtod in the C locale.
double ConvertEnglishToDouble(const char* text) noexcept;

// RemoveUnnecessaryFloatingPointDigits @ 0x100305f0a: trims trailing zeros of the fraction in a
// "%f"-style number (keeping at least one digit before an exponent and never emptying the text);
// "x.000" becomes "x", all-zero text becomes "0". Operates in place; `length` is the text length.
void RemoveUnnecessaryFloatingPointDigits(char* text, int length) noexcept;

}  // namespace RZ
