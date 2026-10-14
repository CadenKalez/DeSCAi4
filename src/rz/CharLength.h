// GetCharLength / SC4LimitStringLength — character-length helpers of the string layer.
// GetCharLength @ 0x10001b75a (UTF-8 mode only: code page -2 or 8; 0 and 16 are single byte;
// other multibyte code pages need MBCSIsLeadByte, which is not recovered and counts one byte),
// SC4LimitStringLength @ 0x100039404.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"

// Byte length of the character at `data` (at most `remaining` bytes available); 0 when empty.
int GetCharLength(const void* data, std::uint32_t remaining, int codePage);
// Truncates `text` to at most `maxBytes` bytes without splitting a character. True when it was cut.
bool SC4LimitStringLength(cIGZString& text, std::uint32_t maxBytes);
