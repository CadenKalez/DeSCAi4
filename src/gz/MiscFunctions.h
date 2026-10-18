// Small free functions of the game binary that stand on their own: the HTML view's whitespace helpers, URL decoding,
// float-text trimming, Cohen-Sutherland line clipping, hill-to-terrain rectangle rotation, an altitude lookup that clamps to
// the map, and a few arithmetic one-liners. Each comment names the original address.
#pragma once
#include <cstdint>

#include "rz/cRZString.h"

// The int-rectangle the terrain tools use: left (x), top (z), right, bottom, each inclusive. Layout of SC4Rect<int>.
struct SC4RectInt {
    int left = 0, top = 0, right = 0, bottom = 0;
};

// True when the character at `text` is whitespace for HTML layout: tab, LF, FF, CR and space. In the framework's UTF-8
// mode only those ASCII characters count (no-break and zero width spaces do not). @ 0x1005c1326
bool HTMLIsSpace(const char* text, int length);
bool HTMLIsSpace(const cRZString& text, int index);       // @ 0x1005c13d0
int HTMLAdvance(const cRZString& text, int index);        // index + length of the character at index. @ 0x1005c140a
int HTMLBackspace(const cRZString& text, int index);      // index - length of the character before index. @ 0x1005c1453

std::uint32_t gCombineGUIDs(int a, int b);                // a ^ b. @ 0x1003fc0be
// Decodes %XX escapes; a '%' without two hex digits after it is kept as is. @ 0x1003fbf4c
bool gConvertURLFromEndodedForm(const cRZString& in, cRZString& out);

// Trims trailing zeros of the fraction of a printed number (and a then-empty '.'), keeping any exponent; an all-zero
// result becomes "0". `length` is the text length without the terminator. @ 0x100305f0a
void RemoveUnnecessaryFloatingPointDigits(char* text, int length);

// Cohen-Sutherland clipping of the segment (x0,y0)-(x1,y1) against the rectangle [minX,maxX] x [minY,maxY]; false when
// it lies entirely outside. Intercepts use truncating integer division. @ 0x1002b394b
bool ClipLineToRect(int& x0, int& y0, int& x1, int& y1, int minX, int minY, int maxX, int maxY);

// Maps a rectangle between the hill (view) frame and the terrain frame for rotation 0..3 of a width x depth map.
// Other rotations leave `out` untouched. @ 0x100777646
void HillToTerrainRect(int rotation, int width, int depth, const SC4RectInt& in, SC4RectInt& out);

// Adds `offset` to `count` 16-bit values. @ 0x10060a069
void FastAddOffset(std::uint16_t* values, std::uint32_t count, int offset);

// The terrain as SafeGetAltitude uses it (cISTETerrain vslots 0x30, 0x38, 0x40, 0x60).
class ISTETerrainAltitude {
public:
    virtual ~ISTETerrainAltitude() = default;
    virtual float GetCellSize() = 0;
    virtual int GetWidthInCells() = 0;
    virtual int GetDepthInCells() = 0;
    virtual float GetAltitude(float x, float z) = 0;
};
// Altitude at (x, z) with both coordinates clamped into [0, cells * cell size]. @ 0x1002d3961
float SafeGetAltitude(ISTETerrainAltitude* terrain, float x, float z);

// ASCII case-insensitive comparison on signed chars: -1, 0 or 1. @ 0x1006421d8
int stricmp(const char* a, const char* b);
