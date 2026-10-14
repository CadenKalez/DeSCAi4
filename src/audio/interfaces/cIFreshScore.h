// cIFreshScore — read access to a music score's cell grid. Slot 3 (+0x18); the class adds the
// destructors at slots 4-5.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIFreshCell.h"

class cIFreshScore : public cIGZUnknown {
public:
    // Returns the cell at (row, column) or null. The third parameter exists in the original
    // signature and is ignored.
    virtual cIFreshCell* GetCell(std::uint32_t row, std::uint32_t column, std::uint32_t ignored) = 0;  // [3]
};
