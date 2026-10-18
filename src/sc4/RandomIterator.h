// RandomIterator / LinearRandomIterator — visit every cell of a rectangle (or every index below a
// count) exactly once in a scrambled order. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (RandomIterator::Reset @ 0x100213f58, LinearRandomIterator::Reset @ 0x10008daf0; the prime table
// kPrimeNumbers is shared, 70 entries).
//
// Two modes. "Modular" (flag at +1, the default): a stride coprime to the element count is chosen
// from the prime table and the visit order is (current + stride) % count, started at a random
// offset. "List" mode: all positions are materialised in a vector and shuffled. In both, Reset
// rewinds the walk; callers rewind when a pass completes.
//
// Rectangle form (RandomIterator): cells (a, b) with a in [minA, maxA], b in [minB, maxB]; list mode
// packs a position as (a << 8) | b so each coordinate must fit a byte. Quirk kept: when the element
// count exceeds 0x1fd8b the count is clamped to 0x1fd8c, so larger rectangles are only partly
// visited. Randomness comes from the C library's rand() like the original.
#pragma once
#include <cstdint>
#include <vector>

class cIGZIStream;
class cIGZOStream;

class RandomIterator {
public:
    friend cIGZIStream& operator>>(cIGZIStream& s, RandomIterator& v);          // sc4/SimStreamOps.cpp
    friend cIGZOStream& operator<<(cIGZOStream& s, const RandomIterator& v);
    RandomIterator() = default;

    void SetRect(int minA, int minB, int maxA, int maxB) { minA_ = minA; minB_ = minB; maxA_ = maxA; maxB_ = maxB; dirty_ = true; }
    void SetModular(bool modular) { modular_ = modular; }
    bool IsModular() const { return modular_; }
    bool IsDone() const { return state_ == kDone; }

    // Rewinds; with `randomize` the starting point (modular) or the order (list) is re-randomised.
    void Reset(bool randomize);
    // Next position; false (and the iterator is done) when the rectangle is exhausted.
    bool Next(int& a, int& b);

private:
    enum State : std::uint8_t { kRunning = 1, kDone = 2 };
    State state_ = kRunning;       // +0
    bool modular_ = true;          // +1
    bool dirty_ = true;            // +2
    int minA_ = 0, minB_ = 0, maxA_ = 0, maxB_ = 0;   // +4,+8,+0xc,+0x10
    int count_ = 0;                // +0x14
    std::vector<std::uint16_t> list_;   // +0x18
    int index_ = 0;                // +0x30
    int current_ = 0;              // +0x34
    int stride_ = 1;               // +0x38
};

class LinearRandomIterator {
public:
    explicit LinearRandomIterator(int count = 0, bool modular = false) : modular_(modular), count_(count) {}

    void SetCount(int count) { count_ = count; }
    bool IsDone() const { return state_ == kDone; }
    void Reset(bool randomize);
    bool Next(std::uint32_t& value);

private:
    enum State : std::uint8_t { kRunning = 1, kDone = 2 };
    State state_ = kRunning;
    bool modular_;
    bool dirty_ = true;
    std::vector<std::uint32_t> list_;   // +8
    int count_;                         // +0x20
    int index_ = 0;                     // +0x24
    int current_ = 0;                   // +0x28
    int stride_ = 1;                    // +0x2c
};
