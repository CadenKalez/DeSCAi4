// cSC4NetworkWorldCache — the network tool's per-cell cache (the tool's +0x30 member, 0x50 bytes): the cells the current drag
// touches, keyed x | z << 16, plus one saved copy that Push takes and Pop restores while PlaceNetwork retries a placement.
//
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (0x10079d6fe..0x10079dee4). Layout: +0x00 unordered_map<uint32,
// cSC4NetworkCellInfo> cells (max_load_factor 1.0 at +0x20), +0x28 the saved map (max_load_factor at +0x48).
// Port note: cSC4NetworkCellInfo::occupant is a bare pointer here; the original's is a cRZAutoRefCount, so its copies
// (Push / Pop / GetAllCells) also AddRef the occupant.
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "sc4/NetworkTool.h"

namespace sc4 {

class cSC4NetworkWorldCache final : public INetworkWorldCache {
public:
    cSC4NetworkWorldCache() = default;                                          // @ 0x10079d6fe
    ~cSC4NetworkWorldCache() override = default;                                // @ 0x10079d730

    cSC4NetworkCellInfo* GetCell(std::uint32_t cellKey) override;              // @ 0x10079d774
    cSC4NetworkCellInfo* CreateCell(std::uint32_t cellKey) override;           // @ 0x10079d7a4
    void Clear() override;                                                      // @ 0x10079d868
    void Push();                                                                // @ 0x10079d888
    void Pop();                                                                 // @ 0x10079db34
    void GetAllCells(std::vector<cSC4NetworkCellInfo>& out) override;          // @ 0x10079ddbe
    void ResetSolvedCellIDs();                                                  // @ 0x10079dec6

    std::size_t CellCount() const { return cells_.size(); }
    std::size_t SavedCount() const { return saved_.size(); }

private:
    std::unordered_map<std::uint32_t, cSC4NetworkCellInfo> cells_;    // +0x00
    std::unordered_map<std::uint32_t, cSC4NetworkCellInfo> saved_;    // +0x28
};

}  // namespace sc4
