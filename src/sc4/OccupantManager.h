// cSC4OccupantManager — spatial index of every occupant in a city: a 192x192 grid of 64 m "world
// cells", each holding the occupants whose bounding box touches it (0x100246b90..0x10024a770), plus the
// three occupant filters it uses (cSC4OccupantFilter/TypeFilter/DefaultFilter, 0x1000a5ce0..0x1000a8f6e).
//
// Grid nodes hold the occupant pointer with bit 0 set when the occupant fits a single cell (queries
// then skip the duplicate-suppression set). The grid owns one reference per occupant. The original
// indexed the grid with unchecked byte coordinates; this port clamps/skips out-of-range cells.
#pragma once
#include "sc4/CommonIIDs.h"
#include <deque>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4OccupantManager = 0xc6bb8334;
constexpr std::uint32_t kGZIID_cISC4OccupantFilter = 0x46a80026;
constexpr std::uint32_t kGZCLSID_cSC4OccupantManager = 0x098f964d;
constexpr std::uint32_t kGZCLSID_cSC4OccupantFilter = 0xa9c74dc9;
constexpr std::uint32_t kGZCLSID_cSC4OccupantTypeFilter = 0xe9c75ed1;
constexpr std::uint32_t kGZCLSID_cSC4DefaultOccupantFilter = 0xc9c75eae;


constexpr int kWorldCells = 192;
constexpr float kWorldCellSize = 64.0f;

class cISC4OccupantFilter : public cIGZUnknown {
public:
    virtual bool IsOccupantIncluded(cISC4Occupant* occupant) = 0;               // [3]
    virtual bool IsOccupantTypeIncluded(std::uint32_t type) = 0;                // [4]
    virtual bool IsPropertyHolderIncluded(cISCPropertyHolder* holder) = 0;      // [5]
};

class cSC4OccupantFilter : public cISC4OccupantFilter, public cIGZSerializable {
public:
    virtual ~cSC4OccupantFilter() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool IsOccupantIncluded(cISC4Occupant* occupant) override { return IsOccupantTypeIncluded(occupant->GetType()); }
    bool IsOccupantTypeIncluded(std::uint32_t) override { return true; }
    bool IsPropertyHolderIncluded(cISCPropertyHolder*) override { return true; }
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4OccupantFilter; }

protected:
    std::uint32_t refCount_ = 0;     // +0x10
};

class cSC4OccupantTypeFilter : public cSC4OccupantFilter {
public:
    bool IsOccupantTypeIncluded(std::uint32_t type) override { return type_ == type; }
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4OccupantTypeFilter; }
    void SetType(std::uint32_t type) { type_ = type; }
    std::uint32_t GetTypeFilter() const { return type_; }

private:
    std::uint32_t type_ = 0;         // +0x14
};

// Excludes occupants that are flora/network style objects: those answering 0x4a10afc2 / 0x8a139184,
// those in occupant groups 0x4217, 0x4208, 0x421d, and (by property) groups 0x49512a09, 0x8a15f1bd
// or with property 0xcaa45670 set.
class cSC4DefaultOccupantFilter : public cSC4OccupantFilter {
public:
    bool IsOccupantIncluded(cISC4Occupant* occupant) override;
    bool IsPropertyHolderIncluded(cISCPropertyHolder* holder) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4DefaultOccupantFilter; }
};

using OccupantList = std::list<cISC4Occupant*>;       // every entry holds a reference
using OccupantCallback = bool (*)(cISC4Occupant*, void*);   // return false to stop

class cISC4OccupantManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                    // [3]
    virtual bool Shutdown() = 0;                                                                // [4]
    virtual bool SetCitySize(float x, float z) = 0;                                             // [5]
    virtual void GetOccupantManagerCellSizes(float& x, float& z) const = 0;                     // [6]
    virtual void GetWorldCellCount(int& x, int& z) const = 0;                                   // [7]
    virtual void GetOccupantManagerCellCount(int& x, int& z) const = 0;                         // [8]
    virtual void GetOccupantManagerCellBounds(int& x0, int& z0, int& x1, int& z1) const = 0;    // [9]
    virtual void WorldCellToOccupantManagerCell(int, int, int&, int&) const = 0;                // [10]
    virtual void OccupantManagerCellToWorldCell(int, int, int&, int&) const = 0;                // [11]
    virtual void WorldCellToStandardCityCell(int, int, int&, int&) const = 0;                   // [12]
    virtual void StandardCityCellToWorldCell(int, int, int&, int&) const = 0;                   // [13]
    virtual void WorldCellToPosition(int, int, float&, float&) const = 0;                       // [14]
    virtual void PositionToWorldCell(float, float, int&, int&) const = 0;                       // [15]
    virtual void OccupantManagerCellToPosition(int, int, float&, float&) const = 0;             // [16]
    virtual void PositionToOccupantManagerCell(float, float, int&, int&) const = 0;             // [17]
    virtual void StandardCityCellToPosition(int, int, float&, float&) const = 0;                // [18]
    virtual void PositionToStandardCityCell(float, float, int&, int&) const = 0;                // [19]
    virtual bool InsertOccupant(cISC4Occupant* occupant, std::uint32_t data) = 0;               // [20]
    virtual bool RemoveOccupant(cISC4Occupant* occupant, bool shutdown, std::uint32_t data) = 0;  // [21]
    virtual bool RemoveOccupants(int type, bool shutdown, std::uint32_t data) = 0;              // [22]
    virtual bool RemoveOccupants(cISC4OccupantFilter* filter, bool shutdown, std::uint32_t data) = 0;  // [23]
    virtual bool MoveOccupant(cISC4Occupant* occupant, bool post) = 0;                          // [24]
    virtual bool IsCellEmpty(int x, int z) const = 0;                                           // [25]
    virtual int GetBoundingCells(int x, int z, int (*cells)[2], int& count) = 0;                // [26]
    virtual bool FindOccupant(cISC4Occupant* occupant, bool searchAll, int* x, int* z) = 0;     // [27]
    virtual bool GetFirstOccupantByPosition(cISC4Occupant*& out, float x, float z, std::uint32_t type) = 0;           // [28]
    virtual bool GetFirstOccupantByPosition(cISC4Occupant*& out, float x, float z, cISC4OccupantFilter* f) = 0;       // [29]
    virtual bool GetFirstOccupant(cISC4Occupant*& out, int x, int z, std::uint32_t type) = 0;                         // [30]
    virtual bool GetFirstOccupant(cISC4Occupant*& out, int x, int z, cISC4OccupantFilter* f) = 0;                     // [31]
    virtual bool GetFirstOccupantByStandardCityCell(cISC4Occupant*& out, int x, int z, std::uint32_t type) = 0;       // [32]
    virtual bool GetFirstOccupantByStandardCityCell(cISC4Occupant*& out, int x, int z, cISC4OccupantFilter* f) = 0;   // [33]
    virtual bool GetFirstOccupantByStandardCityCells(cISC4Occupant*& out, const int* a, const int* b, std::uint32_t type) = 0;        // [34]
    virtual bool GetFirstOccupantByStandardCityCells(cISC4Occupant*& out, const int* a, const int* b, cISC4OccupantFilter* f) = 0;    // [35]
    virtual bool GetOccupantsByBBox(OccupantList& out, const float* min, const float* max, std::uint32_t type, std::uint32_t maxCount) = 0;        // [36]
    virtual bool GetOccupantsByBBox(OccupantList& out, const float* min, const float* max, cISC4OccupantFilter* f, std::uint32_t maxCount) = 0;    // [37]
    virtual bool GetOccupantsByOccupantManagerCells(OccupantList& out, const int* a, const int* b, std::uint32_t type, std::uint32_t maxCount) = 0;     // [38]
    virtual bool GetOccupantsByOccupantManagerCells(OccupantList& out, const int* a, const int* b, cISC4OccupantFilter* f, std::uint32_t maxCount) = 0; // [39]
    virtual bool GetOccupantsByStandardCityCells(OccupantList& out, const int* a, const int* b, std::uint32_t type, std::uint32_t maxCount) = 0;       // [40]
    virtual bool GetOccupantsByStandardCityCells(OccupantList& out, const int* a, const int* b, cISC4OccupantFilter* f, std::uint32_t maxCount) = 0;   // [41]
    virtual bool IterateOccupantsByBBox(OccupantCallback cb, void* ctx, const float* min, const float* max, std::uint32_t type) = 0;             // [42]
    virtual bool IterateOccupantsByBBox(OccupantCallback cb, void* ctx, const float* min, const float* max, cISC4OccupantFilter* f) = 0;         // [43]
    virtual bool IterateOccupants(OccupantCallback cb, void* ctx, const int* a, const int* b, std::uint32_t type) = 0;                           // [44]
    virtual bool IterateOccupants(OccupantCallback cb, void* ctx, const int* a, const int* b, cISC4OccupantFilter* f) = 0;                       // [45]
    virtual bool IterateOccupantsByStandardCityCell(OccupantCallback cb, void* ctx, const int* a, const int* b, cISC4OccupantFilter* f) = 0;     // [46]
    virtual bool IterateOccupantsByStandardCityCell(OccupantCallback cb, void* ctx, const int* a, const int* b, std::uint32_t type) = 0;         // [47]
    virtual void ReleaseOccupantList(OccupantList& list) = 0;                                   // [48]
    virtual ~cISC4OccupantManager() = default;                                                  // [49],[50]
};

class cSC4OccupantManager final : public cISC4OccupantManager, public cIGZSerializable, public cIGZMessageTarget2 {
public:
    cSC4OccupantManager() = default;
    ~cSC4OccupantManager() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;

    bool Init() override;
    bool Shutdown() override;
    bool SetCitySize(float x, float z) override;
    void GetOccupantManagerCellSizes(float& x, float& z) const override { x = z = 64.0f; }
    void GetWorldCellCount(int& x, int& z) const override { x = z = kWorldCells; }
    void GetOccupantManagerCellCount(int& x, int& z) const override { x = cellsX_; z = cellsZ_; }
    void GetOccupantManagerCellBounds(int& x0, int& z0, int& x1, int& z1) const override;
    void WorldCellToOccupantManagerCell(int x, int z, int& ox, int& oz) const override { ox = x - 0x40; oz = z - 0x40; }
    void OccupantManagerCellToWorldCell(int x, int z, int& ox, int& oz) const override { ox = x + 0x40; oz = z + 0x40; }
    void WorldCellToStandardCityCell(int x, int z, int& ox, int& oz) const override;
    void StandardCityCellToWorldCell(int x, int z, int& ox, int& oz) const override;
    void WorldCellToPosition(int x, int z, float& px, float& pz) const override;
    void PositionToWorldCell(float px, float pz, int& x, int& z) const override;
    void OccupantManagerCellToPosition(int x, int z, float& px, float& pz) const override;
    void PositionToOccupantManagerCell(float px, float pz, int& x, int& z) const override;
    void StandardCityCellToPosition(int x, int z, float& px, float& pz) const override;
    void PositionToStandardCityCell(float px, float pz, int& x, int& z) const override;
    bool InsertOccupant(cISC4Occupant* occupant, std::uint32_t data) override;
    bool RemoveOccupant(cISC4Occupant* occupant, bool shutdown, std::uint32_t data) override;
    bool RemoveOccupants(int type, bool shutdown, std::uint32_t data) override;
    bool RemoveOccupants(cISC4OccupantFilter* filter, bool shutdown, std::uint32_t data) override;
    bool MoveOccupant(cISC4Occupant* occupant, bool post) override;
    bool IsCellEmpty(int x, int z) const override;
    int GetBoundingCells(int x, int z, int (*cells)[2], int& count) override;
    bool FindOccupant(cISC4Occupant* occupant, bool searchAll, int* x, int* z) override;
    bool GetFirstOccupantByPosition(cISC4Occupant*& out, float x, float z, std::uint32_t type) override;
    bool GetFirstOccupantByPosition(cISC4Occupant*& out, float x, float z, cISC4OccupantFilter* f) override;
    bool GetFirstOccupant(cISC4Occupant*& out, int x, int z, std::uint32_t type) override;
    bool GetFirstOccupant(cISC4Occupant*& out, int x, int z, cISC4OccupantFilter* f) override;
    bool GetFirstOccupantByStandardCityCell(cISC4Occupant*& out, int x, int z, std::uint32_t type) override;
    bool GetFirstOccupantByStandardCityCell(cISC4Occupant*& out, int x, int z, cISC4OccupantFilter* f) override;
    bool GetFirstOccupantByStandardCityCells(cISC4Occupant*& out, const int* a, const int* b, std::uint32_t type) override;
    bool GetFirstOccupantByStandardCityCells(cISC4Occupant*& out, const int* a, const int* b, cISC4OccupantFilter* f) override;
    bool GetOccupantsByBBox(OccupantList& out, const float* min, const float* max, std::uint32_t type, std::uint32_t maxCount) override;
    bool GetOccupantsByBBox(OccupantList& out, const float* min, const float* max, cISC4OccupantFilter* f, std::uint32_t maxCount) override;
    bool GetOccupantsByOccupantManagerCells(OccupantList& out, const int* a, const int* b, std::uint32_t type, std::uint32_t maxCount) override;
    bool GetOccupantsByOccupantManagerCells(OccupantList& out, const int* a, const int* b, cISC4OccupantFilter* f, std::uint32_t maxCount) override;
    bool GetOccupantsByStandardCityCells(OccupantList& out, const int* a, const int* b, std::uint32_t type, std::uint32_t maxCount) override;
    bool GetOccupantsByStandardCityCells(OccupantList& out, const int* a, const int* b, cISC4OccupantFilter* f, std::uint32_t maxCount) override;
    bool IterateOccupantsByBBox(OccupantCallback cb, void* ctx, const float* min, const float* max, std::uint32_t type) override;
    bool IterateOccupantsByBBox(OccupantCallback cb, void* ctx, const float* min, const float* max, cISC4OccupantFilter* f) override;
    bool IterateOccupants(OccupantCallback cb, void* ctx, const int* a, const int* b, std::uint32_t type) override;
    bool IterateOccupants(OccupantCallback cb, void* ctx, const int* a, const int* b, cISC4OccupantFilter* f) override;
    bool IterateOccupantsByStandardCityCell(OccupantCallback cb, void* ctx, const int* a, const int* b, cISC4OccupantFilter* f) override;
    bool IterateOccupantsByStandardCityCell(OccupantCallback cb, void* ctx, const int* a, const int* b, std::uint32_t type) override;
    void ReleaseOccupantList(OccupantList& list) override;

    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4OccupantManager; }
    bool DoMessage(cIGZMessage2* message) override;

    using Cell = std::list<std::uintptr_t>;     // tagged occupant pointers (bit 0 = single cell)

    // The set the original uses to remove duplicates while it walks cells: keys are kept in a plain vector until
    // RehashVectorToHashTable turns them into a chained hash table (17 buckets), which RehashHashTable then grows
    // (2n+1 buckets). Nodes live in a deque and are pushed at the front. Layout: +0 bucket count, +4 entry count,
    // +8 buckets / keys, +0x20 the previous vector, +0x38 the node deque.
    class InternalOccupantHashSet {
    public:
        struct HashNode { HashNode* next; std::uint64_t key; };
        InternalOccupantHashSet();                      // @ 0x1002465e0 (the destructor, @ 0x100246686, is the compiler's)
        bool RehashVectorToHashTable();                 // @ 0x1002468e8
        bool RehashHashTable();                         // @ 0x100246a4c
        // State the callers (the cell walks) manage:
        std::uint32_t bucketCount = 0;                  // +0
        std::uint32_t entryCount = 0;                   // +4
        std::vector<void*> table;                       // +8 (keys while in vector mode, bucket heads afterwards)
        std::uint64_t tableSize = 0;                    // +0x18
        std::vector<void*> previous;                    // +0x20
        std::uint64_t previousSize = 0;                 // +0x30
        std::deque<HashNode> nodes;                     // +0x38
    };

    // @ 0x100247b90 / 0x1002485d2 / 0x10024878e: the per-cell occupant lists (created on demand when `create`).
    Cell* GetOccupantListNoClip(int x, int z, bool create);
    Cell* GetOccupantList(int x, int z, bool create);                       // the cell is clamped to 0..0xbf
    Cell* GetOccupantList(cISC4Occupant* occupant, int& x, int& z);        // the list of the occupant's first cell

private:

    Cell* CellAtClamped(int x, int z, bool create);
    Cell* CellAt(int x, int z) const;           // null outside the grid
    void EraseFromCell(int x, int z, cISC4Occupant* occupant);
    void ComputeCells(cISC4Occupant* occupant, std::uint8_t cells[4]) const;
    bool PassesFilter(cISC4OccupantFilter* f, cISC4Occupant* o) const { return !f || f->IsOccupantIncluded(o); }
    cSC4OccupantTypeFilter* TypeFilter(std::uint32_t type);
    bool CollectByBBox(OccupantList& out, const float* min, const float* max, cISC4OccupantFilter* f, std::uint32_t maxCount);
    bool IterateCells(int x0, int z0, int x1, int z1, OccupantCallback cb, void* ctx, cISC4OccupantFilter* f);
    static bool InitOccupantCallback(cISC4Occupant* occupant, void*) { occupant->Init(); return true; }

    bool initialized_ = false;                                  // +0x18
    bool loaded_ = false;                                       // +0x19
    std::uint32_t refCount_ = 0;                                // +0x1c
    mutable std::recursive_mutex mutex_;                        // +0x20
    float sizeX_ = 0, sizeZ_ = 0;                               // +0x30
    int cellsX_ = 0, cellsZ_ = 0;                               // +0x38 occupant-manager cells (size / 64)
    int standardX_ = 0, standardZ_ = 0;                         // +0x40 standard city cells (size / 16)
    cSC4OccupantTypeFilter* filter_ = nullptr;                  // +0x48
    std::vector<std::vector<Cell*>> grid_;                      // +0x50
};

}  // namespace sc4
