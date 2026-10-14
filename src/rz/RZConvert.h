// String-to-number helpers of the RZ framework.
#pragma once
#include <cstdint>

namespace RZ {

// RZConvertToUintPtrA(char const*) @ 0x10001a307
// Parses an unsigned integer: "0x"/"0X" prefix or any A-F/a-f before the first non-digit selects
// base 16, otherwise base 10 (strtoul semantics, truncated to 32 bits).
std::uint32_t ConvertToUint(const char* text) noexcept;

}  // namespace RZ
