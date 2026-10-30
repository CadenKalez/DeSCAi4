// cSC4NetworkWorldCache (see NetworkWorldCache.h).
#include "sc4/NetworkWorldCache.h"

namespace sc4 {

cSC4NetworkCellInfo* cSC4NetworkWorldCache::GetCell(std::uint32_t cellKey) {   // @ 0x10079d774
    auto found = cells_.find(cellKey);
    return found == cells_.end() ? nullptr : &found->second;
}

cSC4NetworkCellInfo* cSC4NetworkWorldCache::CreateCell(std::uint32_t cellKey) {   // @ 0x10079d7a4
    return &cells_[cellKey];
}

void cSC4NetworkWorldCache::Clear() {   // @ 0x10079d868
    cells_.clear();
    saved_.clear();
}

// Saves the cells: the saved map is cleared, takes the cells' load factor and gets every cell (unique insertion, in the cells'
// iteration order).
void cSC4NetworkWorldCache::Push() {   // @ 0x10079d888
    saved_.clear();
    saved_.max_load_factor(cells_.max_load_factor());
    for (const auto& cell : cells_) saved_.insert(cell);
}

// Restores the saved cells, the mirror of Push.
void cSC4NetworkWorldCache::Pop() {   // @ 0x10079db34
    cells_.clear();
    cells_.max_load_factor(saved_.max_load_factor());
    for (const auto& cell : saved_) cells_.insert(cell);
}

void cSC4NetworkWorldCache::GetAllCells(std::vector<cSC4NetworkCellInfo>& out) {   // @ 0x10079ddbe
    for (const auto& cell : cells_) out.push_back(cell.second);
}

// Every cell forgets its index into the solved-cell list (+0xc0 = -1).
void cSC4NetworkWorldCache::ResetSolvedCellIDs() {   // @ 0x10079dec6
    for (auto& cell : cells_) cell.second.solvedCellIndex = -1;
}

}  // namespace sc4
