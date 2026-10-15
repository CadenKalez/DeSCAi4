#include "gz/Bit1.h"

#include <cstring>

void Bit1_PixelSet(std::uint32_t* bits, int row, int column, int size) {
    bits[(row * size + column) >> 5] |= 1u << (column & 0x1f);
}

bool Bit1_PixelTest(const std::uint32_t* bits, int row, int column, int size) {
    return ((bits[(row * size + column) >> 5] >> (column & 0x1f)) & 1) != 0;
}

void Bit1_Clear(std::uint32_t* bits, int size) {
    const std::uint32_t words = static_cast<std::uint32_t>(size * size) >> 5;
    if (words != 0) std::memset(bits, 0, static_cast<std::size_t>(words) * 4);
}

void Bit1_Xor(std::uint32_t* destination, const std::uint32_t* source, int size) {
    const std::uint32_t words = static_cast<std::uint32_t>(size * size) >> 5;
    for (std::uint32_t i = 0; i < words; ++i) destination[i] ^= source[i];
}

bool Bit1_SelectionGrowHoriz(std::uint32_t* selection, const std::uint32_t* mask, int size, int* rowMin, int* rowMax) {
    int row = *rowMin;
    const int last = *rowMax;
    *rowMin = size;
    *rowMax = 0;
    if (row >= last) return false;
    bool changed = false;
    const int wordsPerRow = size >> 5;
    int word = wordsPerRow * row;
    do {
        std::uint32_t current = selection[word];
        std::uint32_t next = selection[word + 1];
        std::uint32_t grown = ((next << 31) | (current * 2) | (current >> 1) | current) & mask[word];
        if (grown != current) {
            if (row < *rowMin) *rowMin = row;
            if (*rowMax <= row) *rowMax = row + 1;
            selection[word] = grown | current;
            changed = true;
        }
        int index = word + 1;
        std::uint32_t carry = (next * 2) | (next >> 1) | (current >> 31) | next;
        std::uint32_t previous = next;
        if (size > 0x5f) {
            const std::uint32_t* m = mask + word + 1;
            std::uint32_t* s = selection + word + 2;
            std::uint32_t before = next;
            int remaining = size - 0x40;
            do {
                const std::uint32_t after = *s;
                const std::uint32_t value = ((after << 31) | carry) & *m;
                if (value != before) {
                    if (row < *rowMin) *rowMin = row;
                    if (*rowMax <= row) *rowMax = row + 1;
                    s[-1] = value | before;
                    changed = true;
                }
                carry = (after * 2) | (after >> 1) | (before >> 31) | after;
                ++m;
                ++s;
                remaining -= 0x20;
                before = after;
            } while (remaining > 0x1f);
            previous = before;
            index += static_cast<int>((static_cast<std::uint32_t>(size) - 0x60) >> 5) + 1;
        }
        const std::uint32_t tail = mask[index];
        if ((carry & tail) != previous) {
            if (row < *rowMin) *rowMin = row;
            if (*rowMax <= row) *rowMax = row + 1;
            selection[index] = (carry & tail) | previous;
            changed = true;
        }
        word = index + 1;
        ++row;
    } while (row != last);
    return changed;
}

bool Bit1_SelectionGrowVert(std::uint32_t* selection, const std::uint32_t* mask, int size, int* rowMin, int* rowMax) {
    const int first = *rowMin;
    const int last = *rowMax;
    *rowMin = size;
    *rowMax = 0;
    bool changed = false;
    if (size <= 0) return false;
    const int wordsPerRow = size >> 5;
    const int rowAfter = first + 1;
    const long stride = wordsPerRow;
    const long base = static_cast<long>(first) * wordsPerRow;
    const std::uint32_t* maskNext = mask + stride + base;
    std::uint32_t* selectionNext = selection + stride + base;
    const std::uint32_t* selectionAhead = selection + base + stride * 2;
    for (long bit = 0; bit < size; bit += 0x20) {
        long index = (bit >> 5) + base;
        std::uint32_t above = selection[index];
        std::uint32_t here = selection[index + stride];
        std::uint32_t combined = here | above;
        std::uint32_t allowed = mask[index];
        if ((combined & allowed) != above) {
            if (first < *rowMin) *rowMin = first;
            if (*rowMax <= first) *rowMax = rowAfter;
            selection[index] = (allowed & here) | above;
            changed = true;
        }
        int target = static_cast<int>(index + stride);
        int step = first;
        int rowForUpdate = rowAfter;
        std::uint32_t last_here = here;
        if (rowAfter < last - 1) {
            long offset = 0;
            std::uint32_t before = here;
            int r = first + 2;
            do {
                const std::uint32_t after = *reinterpret_cast<const std::uint32_t*>(
                    reinterpret_cast<const char*>(selectionAhead) + offset * 1);
                std::uint32_t value = (combined | after) & *reinterpret_cast<const std::uint32_t*>(
                                                               reinterpret_cast<const char*>(maskNext) + offset);
                if (value != before) {
                    rowForUpdate = r - 1;
                    if (rowForUpdate < *rowMin) *rowMin = rowForUpdate;
                    if (*rowMax <= rowForUpdate) *rowMax = r;
                    *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(selectionNext) + offset) = value | before;
                    changed = true;
                }
                combined = before | after;
                offset += stride * 4;
                ++r;
                before = after;
                last_here = after;
            } while (last != r);
            target += static_cast<int>((last - first - 2) * wordsPerRow);
            step = last - 2;
            rowForUpdate = last - 1;
        }
        allowed = mask[target];
        if ((combined & allowed) != last_here) {
            if (rowForUpdate < *rowMin) *rowMin = rowForUpdate;
            if (*rowMax <= rowForUpdate) *rowMax = step + 2;
            selection[target] = (combined & allowed) | last_here;
            changed = true;
        }
        ++maskNext;
        ++selectionNext;
        ++selectionAhead;
    }
    return changed;
}

int Bit1_SelectionGrow(std::uint32_t* selection, const std::uint32_t* mask, int size, int maxIterations,
                       int* rowMin, int* rowMax) {
    int minRow = *rowMin;
    int maxRow = *rowMax;
    int paddedMin = minRow > 0 ? minRow - 1 : minRow;
    int paddedMax = maxRow < size - 1 ? maxRow + 1 : maxRow;
    if (maxIterations < 1) return 0;
    int rounds = 1;
    int row = minRow;
    for (;;) {
        if (row < minRow) *rowMin = row;
        if (*rowMax < maxRow) *rowMax = maxRow;
        int horizMin = row, horizMax = maxRow;
        const bool h = Bit1_SelectionGrowHoriz(selection, mask, size, &horizMin, &horizMax);
        const bool v = Bit1_SelectionGrowVert(selection, mask, size, &paddedMin, &paddedMax);
        row = paddedMin;
        if (horizMin <= paddedMin) row = horizMin;
        maxRow = paddedMax;
        if (paddedMax <= horizMax) maxRow = horizMax;
        paddedMin = row;
        if (row > 0) paddedMin = row - 1;
        paddedMax = maxRow;
        if (maxRow < size) paddedMax = maxRow + 1;
        if (!(h || v) || maxIterations <= rounds) break;
        minRow = *rowMin;
        ++rounds;
    }
    return rounds;
}

namespace {
struct RectWords {
    std::uint32_t firstMask;
    std::uint32_t lastMask;
    std::uint32_t firstWord;
    std::uint32_t lastWord;
};
RectWords Words(int size, const cRZRect& r) {
    RectWords w;
    w.firstMask = 0xffffffffu << (r.left & 0x1f);
    w.lastMask = (2u << (r.right & 0x1f)) - 1u;
    w.firstWord = static_cast<std::uint32_t>(r.left + static_cast<std::uint32_t>(r.top) * size) >> 5;
    w.lastWord = static_cast<std::uint32_t>(static_cast<std::uint32_t>(r.top) * size + r.right) >> 5;
    return w;
}
}  // namespace

void Bit1_RectSet(std::uint32_t* bits, int size, const cRZRect& r) {
    const RectWords w = Words(size, r);
    std::uint32_t row = static_cast<std::uint32_t>(r.top);
    std::uint32_t first = w.firstWord;
    const std::uint32_t bottom = static_cast<std::uint32_t>(r.bottom);
    if (w.firstWord == w.lastWord) {
        for (; row <= bottom; ++row) {
            bits[first] |= w.lastMask & w.firstMask;
            first += static_cast<std::uint32_t>(size >> 5);
        }
        return;
    }
    int offset = 0;
    for (; row <= bottom; ++row) {
        bits[first + offset] |= w.firstMask;
        bits[w.lastWord + offset] |= w.lastMask;
        if (first + 1 + offset < w.lastWord + offset)
            std::memset(bits + (first + 1 + offset), 0xff, (static_cast<std::size_t>(w.lastWord - 2 - first)) * 4 + 4);
        offset += size >> 5;
    }
}

void Bit1_RectClear(std::uint32_t* bits, int size, const cRZRect& r) {
    RectWords w = Words(size, r);
    const std::uint32_t keepFirst = ~(0xffffffffu << (r.left & 0x1f));
    const std::uint32_t keepLast = 0xfffffffeu << (r.right & 0x1f);
    std::uint32_t row = static_cast<std::uint32_t>(r.top);
    std::uint32_t first = w.firstWord;
    const std::uint32_t bottom = static_cast<std::uint32_t>(r.bottom);
    if (w.firstWord == w.lastWord) {
        for (; row <= bottom; ++row) {
            bits[first] &= keepLast | keepFirst;
            first += static_cast<std::uint32_t>(size >> 5);
        }
        return;
    }
    int offset = 0;
    for (; row <= bottom; ++row) {
        bits[first + offset] &= keepFirst;
        bits[w.lastWord + offset] &= keepLast;
        if (first + 1 + offset < w.lastWord + offset)
            std::memset(bits + (first + 1 + offset), 0, (static_cast<std::size_t>(w.lastWord - 2 - first)) * 4 + 4);
        offset += size >> 5;
    }
}

bool Bit1_RectTestAny(const std::uint32_t* bits, int size, const cRZRect& r) {
    const RectWords w = Words(size, r);
    std::uint32_t row = static_cast<std::uint32_t>(r.top);
    std::uint32_t first = w.firstWord;
    std::uint32_t last = w.lastWord;
    const std::uint32_t bottom = static_cast<std::uint32_t>(r.bottom);
    const int rowWords = size >> 5;
    if (first == last) {
        for (; row <= bottom; ++row) {
            if ((bits[first] & w.lastMask & w.firstMask) != 0) return true;
            first += rowWords;
        }
        return false;
    }
    for (; row <= bottom; ++row) {
        if ((bits[first] & w.firstMask) != 0) return true;
        std::uint32_t value = bits[last] & w.lastMask;
        std::uint32_t at = first;
        for (;;) {
            if (value != 0) return true;
            ++at;
            if (last <= at) break;
            value = bits[at];
        }
        first += rowWords;
        last += rowWords;
    }
    return false;
}

void Bit1_RectOr(std::uint32_t* destination, const std::uint32_t* source, int size, const cRZRect& r) {
    const RectWords w = Words(size, r);
    std::uint32_t row = static_cast<std::uint32_t>(r.top);
    std::uint32_t first = w.firstWord;
    std::uint32_t last = w.lastWord;
    const std::uint32_t bottom = static_cast<std::uint32_t>(r.bottom);
    const int rowWords = size >> 5;
    if (first == last) {
        for (; row <= bottom; ++row) {
            destination[first] |= source[first] & w.lastMask & w.firstMask;
            first += rowWords;
        }
        return;
    }
    const int between = static_cast<int>((last - 1) - first);
    for (; row <= bottom; ++row) {
        destination[first] |= source[first] & w.firstMask;
        destination[last] |= source[last] & w.lastMask;
        if (first + 1 < last) {
            // (sic) the middle words are never advanced over: only word `first` is re-ORed
            std::uint32_t value = destination[first];
            for (int i = between; i != 0; --i) {
                value |= source[first];
                destination[first] = value;
            }
        }
        first += rowWords;
        last += rowWords;
    }
}
