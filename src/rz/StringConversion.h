// The cIGZString flavours of ConvertStringEncoding (0x10001aa53, 0x10001b41d, 0x10001b4d2, 0x10001b619) and the
// framework-to-destination fix-up the language classes run on every string they hand out (0x100067354).
// Code page -2 and 8 mean UTF-8 (no work), -1 means the system code page, 0x10 is UTF-16.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"

// Raw bytes in `sourcePage` -> `out` as UTF-8 (a straight copy when the page is UTF-8 / framework). Returns the number of
// source bytes consumed. @ 0x10001aa53
unsigned ConvertStringEncoding(const void* source, unsigned size, int sourcePage, cIGZString& out);
// `text` (UTF-8) -> raw bytes in `destinationPage`; `capacity` carries the buffer size in and the produced size out.
// @ 0x10001b41d
unsigned ConvertStringEncoding(const cIGZString& text, void* out, unsigned& capacity, int destinationPage);
// `text` (UTF-8) -> `out` in `destinationPage`; true when every byte was converted (UTF-16 is refused). @ 0x10001b4d2
bool ConvertStringEncoding(const cIGZString& text, int destinationPage, cIGZString& out);
// `text` in `sourcePage` -> `out` as UTF-8; true when every byte was converted. @ 0x10001b619
bool ConvertStringEncoding(const cIGZString& text, cIGZString& out, int sourcePage);
// Re-encodes a UTF-8 string in place into the framework code page when that is not UTF-8 / UTF-16 (at most 256 bytes
// of result; a no-op for the always-UTF-8 framework page). @ 0x100067354
void ConvertUTF8StringToDestinationFormat(cIGZString& text);
