// A handful of small record / helper types the binary instantiates on their own: SC4Rect<T> union/intersection, the
// HTTP message header lookup, the reference-counted buffer holders (SharedMemoryPointer 0x1003685dc, CacheData
// 0x100630d34), cookie / tree-node / grid-cell records with their copy constructors, HELI_TYPE_DATA, and the sprite
// animation table adapter (cAnimatedSpriteTableData::Init 0x1001a6240).
//
// The copy constructors the binary generates for the std::map-based types (MatrixRow<T> 0x10054b3a0 / 0x10054b896 /
// 0x10054b... / 0x10025def0 / 0x10025d912, CompareCitiesForExport 0x10022aa14) are the defaulted copies of
// std::map<int, T> (cRZSparseMatrix::Row) and std::map<const void*, float>; the test copies them to pin that down.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "rz/cRZString.h"

namespace sc4 {

// SC4Rect<T>: x0, z0 (min corner) .. x1, z1 (max corner), inclusive for the int version.
template <class T>
struct SC4RectT {
    T x0{}, z0{}, x1{}, z1{};
    // 0x100212e68 (int): the bounding rectangle of `a` and `b`.
    void UnionRect(const SC4RectT& a, const SC4RectT& b) {
        x0 = a.x0 < b.x0 ? a.x0 : b.x0;
        z0 = a.z0 < b.z0 ? a.z0 : b.z0;
        x1 = b.x1 < a.x1 ? a.x1 : b.x1;
        z1 = b.z1 < a.z1 ? a.z1 : b.z1;
    }
    // 0x100314ce8 (int) / 0x1007584e4 (float): the overlap of `a` and `b`; false (and an all-zero rectangle) when they are disjoint.
    // Touching rectangles overlap (a.x1 == b.x0 is not disjoint).
    bool IntersectRect(const SC4RectT& a, const SC4RectT& b) {
        if (a.x1 < b.x0 || b.x1 < a.x0 || a.z1 < b.z0 || b.z1 < a.z0) {
            x0 = z0 = x1 = z1 = T{};
            return false;
        }
        x0 = b.x0 < a.x0 ? a.x0 : b.x0;
        z0 = b.z0 < a.z0 ? a.z0 : b.z0;
        x1 = a.x1 < b.x1 ? a.x1 : b.x1;
        z1 = a.z1 < b.z1 ? a.z1 : b.z1;
        return true;
    }
};
using SC4RectInt32 = SC4RectT<int>;
using SC4RectFloat32 = SC4RectT<float>;

// HTTPMessage's header table: +0xa8 vector of 0x30-byte entries (name, value).
struct HTTPHeaderEntry { std::string name, value; };
class HTTPMessage {
public:
    std::vector<HTTPHeaderEntry> headers;   // +0xa8
    // 0x100634b20: the `index`-th header whose name matches `name` ignoring case; copies its value into `value` (if given).
    // Not found: `value` is cleared and false returned.
    bool GetMessageHeaderEntryCopy(const std::string& name, std::string* value, unsigned index) const;
    // 0x100634c18: the same by header field id (1..54, see MessageHeaderFieldToFieldString); false for an unknown id.
    bool GetMessageHeaderEntryCopy(int field, std::string* value, unsigned index) const;
    // 0x100634d20: "Cache-Control" for 1 ... "Cookie" for 54 (the table at 0x100ad9e60); null outside 1..54.
    static const char* MessageHeaderFieldToFieldString(int field);
    // 0x100634d40: splits `text` at `separator` into at most `maxCount` of `out`; returns how many were filled. With `quoted`
    // a '"' toggles a quoted section (the quote characters are dropped and separators inside are kept); with `trim` blanks
    // (space / tab) around each part are removed. Quirk kept: when no part is produced the whole text goes to out[0] and 0 is returned.
    static unsigned GetSeparatedStringList(const std::string& text, std::string* out, unsigned maxCount, bool quoted, char separator, bool trim);
};

// Reference-counted raw buffer holders. Release() frees the array and the holder when the count drops below 2.
class SharedMemoryPointer {
public:
    unsigned char* data = nullptr;   // +0
    int refCount = 0;                // +8
    int Release();                   // 0x1003685dc
};
class CacheData {
public:
    unsigned char* data = nullptr;   // +0
    std::uint32_t size = 0;          // +8 (the NetworkFileCache RAM record: bytes in data)
    std::uint16_t refCount = 0;      // +0xc
    short Release();                 // 0x100630d34
};

// Cookie record (0xdc bytes): six strings, a name/value list and 0x4c bytes of numbers; copying is memberwise.
struct CookieData {
    std::string field0;                                      // +0
    std::vector<std::pair<std::string, std::string>> attributes;   // +0x18
    std::string field30, field48, field60, field78;
    std::uint64_t numbers[9] = {};                           // +0x90
    std::uint32_t flags = 0;                                 // +0xd8
};

// Tree-view node (0xaa bytes): three strings and the node's numbers; the copy constructor copies all of them.
struct TVNode {
    cRZString label;                 // +0x08
    cRZString tag;                   // +0x28
    cRZString text;                  // +0x50
    std::uint8_t numbers[0x2a] = {}; // +0x78..+0xa8 (ids, flags, state)
};

// Cell record of the window grid: a pointer-sized pair, a string and three more words (0x50 bytes).
struct WinGridCellData {
    std::uint64_t first = 0;         // +0
    std::uint32_t second = 0;        // +8
    cRZString text;                  // +0x10 (cleared by the constructor)
    std::uint32_t third = 0;         // +0x30
    std::uint64_t tail[3] = {};      // +0x38
    WinGridCellData() { text.Erase(0, text.Strlen()); }   // 0x10054a662
};

// 0x1006f1fe6: helicopter type record, 0x80 bytes of numbers. The copy constructor zeroes the first word and copies the rest.
struct HELI_TYPE_DATA {
    std::uint32_t words[32] = {};
    HELI_TYPE_DATA() = default;
    HELI_TYPE_DATA(const HELI_TYPE_DATA& other) {
        words[0] = 0;
        for (int i = 1; i < 32; ++i) words[i] = other.words[i];
    }
};

// std::map<const cISC4RegionalCity*, float> wrapper copied by the export sort functor.
struct CompareCitiesForExport {
    std::map<const void*, float> scores;
};

// 0x1001a6240: derived layout of an animated sprite table. The table object is reference counted and exposes a header
// of counts and angles; angular steps are 2*pi / count.
struct SpriteAnimTableHeader {
    std::uint32_t countA, countB, countC;   // +0, +4, +8
    float startB;                           // +0xc
    float endB;                             // +0x10
    float startC;                           // +0x14
    float endC;                             // +0x18
};
class ISpriteAnimTable {
public:
    virtual ~ISpriteAnimTable() = default;
    virtual void AddRef() = 0;                          // slot 0x08
    virtual void Release() = 0;                         // slot 0x10
    virtual const SpriteAnimTableHeader* GetData() = 0; // slot 0x18 (the frame data follows 0x20 bytes later)
};
class cAnimatedSpriteTableData {
public:
    ISpriteAnimTable* table = nullptr;        // +0
    const unsigned char* frames = nullptr;    // +8 data + 0x20
    float stepA = 0, startB = 0, stepB = 0, startC = 0, stepC = 0;   // +0x10, +0x14, +0x18, +0x1c, +0x20
    std::uint32_t countA = 0, countB = 0, countC = 0, frameCount = 0;   // +0x24, +0x28, +0x2c, +0x30
    // Drops the old table, takes a reference on `newTable` (null just clears) and derives the step sizes and counts.
    void Init(ISpriteAnimTable* newTable);
};

}  // namespace sc4
