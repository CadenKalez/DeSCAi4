// nRZHash — CRC helpers of the RZ framework. Tables are generated at startup in the binary by
// cRZHashStringCRCInitializer @ 0x100368154; the port builds them once on first use.
#pragma once
#include <cstdint>

namespace nRZHash {

// CRC24 @ 0x1003681dc: OpenPGP polynomial 0x864CFB, init 0xB704CE, MSB first over the (optionally
// lower-cased) bytes. The empty string returns the unmasked initial value 0xB704CE.
std::uint32_t CRC24(const char* text, bool lowerCase) noexcept;

// CRC32 @ 0x100368246: polynomial 0x04C11DB7, init 0xFFFFFFFF, MSB first, no final xor.
std::uint32_t CRC32(const unsigned char* data, std::uint32_t length) noexcept;

}  // namespace nRZHash
