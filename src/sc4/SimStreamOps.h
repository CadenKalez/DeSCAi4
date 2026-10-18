// Stream operators for the simulation's small value types (0x1000ce8b3..0x1000cfd0c, 0x100182e4c, 0x100215cb1,
// 0x10030d31a): the random generator, the cell iterators, occupant lists, response curves, dispatch records and rectangles.
// `>>` reads from a cIGZIStream, `<<` writes to a cIGZOStream; field order is the stream order.
#pragma once
#include <cstdint>
#include <list>
#include <vector>

#include "gz/StreamOps.h"
#include "gz/MiscFunctions.h"
#include "rz/cRZRandom.h"
#include "sc4/RandomIterator.h"
#include "sc4/cSC4ResponseCurve.h"

namespace sc4 { class cISC4Occupant; }

// Walks the cells on the border of a rectangle. Layout: +0 / +1 state bytes, +4 / +8 / +0xc counters, +0x10 the rectangle.
struct BoundaryIterator {
    std::int8_t state = 0;
    std::int8_t direction = 0;
    std::int32_t side = 0, step = 0, length = 0;
    cRZRect rect;
};

// Walks every cell of a rectangle in rows. Layout: +0 state, +4 / +8 position, +0xc count, +0x10 the rectangle.
struct ScanningIterator {
    std::int8_t state = 0;
    std::int32_t x = 0, z = 0;
    std::uint32_t remaining = 0;
    cRZRect rect;
};

// One dispatched vehicle of the dispatch manager (cISC4DispatchManager::DispatchInfo).
struct DispatchInfo {
    std::int32_t id = 0;               // +0
    std::uint32_t source = 0;          // +4
    std::uint32_t destination = 0;     // +8
    bool active = false;               // +0x10
    float values[3] = {0, 0, 0};       // +0x14, +0x18, +0x1c
    cIGZUnknown* vehicle = nullptr;    // +0x20 (class 0x26bb767e)
    cIGZUnknown* target = nullptr;     // +0x28 (class 0xa9b40f05)
    cIGZUnknown* route = nullptr;      // +0x30 (class 0xc9b6d7d6)
};

// SC4Rect<unsigned char> is four bytes, SC4Rect<int> four 32-bit values.
struct SC4RectByte { std::uint8_t v[4] = {0, 0, 0, 0}; };

cIGZIStream& operator>>(cIGZIStream& s, cRZRandom& v);                         // @ 0x1000ce8b3
cIGZOStream& operator<<(cIGZOStream& s, const cRZRandom& v);                   // @ 0x1000ce92a
cIGZIStream& operator>>(cIGZIStream& s, BoundaryIterator& v);                  // @ 0x1000ce99f
cIGZOStream& operator<<(cIGZOStream& s, const BoundaryIterator& v);            // @ 0x1000cea25
cIGZIStream& operator>>(cIGZIStream& s, RandomIterator& v);                    // @ 0x1000cea88
cIGZOStream& operator<<(cIGZOStream& s, const RandomIterator& v);              // @ 0x1000ceb93
cIGZIStream& operator>>(cIGZIStream& s, ScanningIterator& v);                  // @ 0x1000cec88
cIGZOStream& operator<<(cIGZOStream& s, const ScanningIterator& v);            // @ 0x1000ced01
// Occupant lists are saved as a count and each occupant as a class-0x26bb767e serializable; a count of 10,000,000 or more
// (or zero) loads nothing. @ 0x1000cfb37 / 0x1000cfbd7
cIGZIStream& operator>>(cIGZIStream& s, std::list<sc4::cISC4Occupant*>& v);
cIGZOStream& operator<<(cIGZOStream& s, const std::list<sc4::cISC4Occupant*>& v);
cIGZIStream& operator>>(cIGZIStream& s, cSC4ResponseCurve& v);                 // @ 0x1000cfc24
cIGZOStream& operator<<(cIGZOStream& s, const cSC4ResponseCurve& v);           // @ 0x1000cfd0c
cIGZIStream& operator>>(cIGZIStream& s, DispatchInfo& v);                      // @ 0x100215cb1
cIGZOStream& operator<<(cIGZOStream& s, const DispatchInfo& v);                // @ 0x100215d52
cIGZOStream& operator<<(cIGZOStream& s, const SC4RectByte& v);                 // @ 0x100182e4c
cIGZOStream& operator<<(cIGZOStream& s, const SC4RectInt& v);                  // @ 0x10030d31a
