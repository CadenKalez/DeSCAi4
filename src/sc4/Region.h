// cSC4Region — the region object: every city of the region and the city locations read from the
// region's config bitmap (37 symbols, 0x1002a246c..0x1002a58ca, iid 0x89d2be26, no GZCOM class id:
// instances are created by cSC4Region::CreateRegion / CreateStarterRegion).
//
// Every city occupies one or more cells. The cells are kept in an unordered map keyed by
// SC4Point<int> whose hash uses only the low four bits of each coordinate (the original's
// RegionalCellHashFunction: ((x & 0xf) << 4) | (z & 0xf), read from the inlined find at
// 0x1002a5936). The map value is a heap cRZAutoRefCount holder shared by every cell of the city.
// A second, plain list holds the city files that have no matching place in the config bitmap; those
// are "overflow" cities and get moved to the region download directory by MoveOverflowCitiesToDownloadDirectory.
//
// Original layout (0xc8 bytes, allocated by CreateRegion):
//   +0x00 vptr
//   +0x08 initialised byte (Init sets it; Shutdown clears it)
//   +0x0c refcount
//   +0x10 name cRZString (vptr +0x10, libc++ std::string +0x18, refcount +0x30)
//   +0x38 directory-name cRZString (std::string +0x40)
//   +0x60 eBaseTerrainType
//   +0x64 water minimum (stored as 32 bits, exposed as a byte by GetWaterPrefs)
//   +0x68 water maximum (same)
//   +0x70 std::list<cRZAutoRefCount<cISC4RegionalCity>> (sentinel +0x70/+0x78, size +0x80)
//   +0x88 std::vector<cLocation> (begin +0x88, end +0x90, capacity +0x98)
//   +0xa0 std::unordered_map<SC4Point<int>, cRZAutoRefCount<cISC4RegionalCity>*>
//         (bucket list +0xa0, first node +0xb0, size +0xb8, max_load_factor 1.0f at +0xc0)
//
// Quirks kept (each one is asserted by scripts/tests/sc4_region_test.cpp):
//  * The destructor destroys the cell map without deleting the cRZAutoRefCount holders (0x1002a251a),
//    and InsertCity overwrites an existing cell without releasing the holder it replaces
//    (0x1002a4812): the holders leak.
//  * RemoveCity / ReloadCity / DeleteCity leave the caller's holder pointer dangling after they
//    delete it (0x1002a4948).
//  * ReloadCity leaves the caller's holder dangling when the save file cannot be read (0x1002a4a5c).
//  * ResetTutorialCity returns true when the tutorial-city record could NOT be read and false after
//    it actually reset the city (0x1002a53d3).
//  * GetWaterPrefs truncates the 32-bit water preferences to bytes (0x1002a58ca).
//  * MoveCity reuses the moving city's old serial when the destination has no city with it; on a
//    collision it tries the freshly generated serial and then descending values (0x1002a4d24).
//  * MoveCity has an unreachable same-region branch (R13 is null when source == this, so the
//    comparison at 0x1002a4d69 can never be true); it is not reproduced.
//  * LoadCityLocations ignores the bitmap area's left/top offsets (it reads pixels from 0,0,
//    0x1002a5713); when the file is missing or fails to load it falls back to the bitmap resource
//    {0x856ddbac, 0x6a1eed2c, 0x2a1f0276} (0x1002a286f).
//  * Delete only deletes the save file of a city that is still in the cell map; after Shutdown the
//    files remain, the rmdir fails and the region directory is left behind (0x1002a4336).
//
// Neighbours that are not recovered (the application/nation directories, the path-based save-file
// helpers, the resource manager and the tutorial registry) are reached through IRegionHost. The
// partial cISC4RegionalCity interface only declares five methods, so every other city virtual the
// region uses also goes through IRegionHost, with its original slot noted there.
#pragma once
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include "gz/cIGZString.h"
#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"
#include "sc4/ISC4RegionalCity.h"
#include "sc4/MiscTypes.h"
#include "sc4/NetworkTool.h"   // sc4::SC4PointT

class cIGZBuffer;

constexpr std::uint32_t kGZIID_cSC4Region = 0x89d2be26;

// Region configuration: the "region.ini" entries (the strings are the original globals at 0x100b04ef8..).
constexpr std::uint32_t kRegionNameStringGroup = 0x6a231eaa;   // group of the localised region-name strings
constexpr std::uint32_t kRegionConfigBitmapType = 0x856ddbac;  // default config.bmp resource when the file is missing
constexpr std::uint32_t kRegionConfigBitmapGroup = 0x6a1eed2c;
constexpr std::uint32_t kRegionConfigBitmapInstance = 0x2a1f0276;
constexpr std::uint32_t kRegionConfigBitmapFirstCity = 0x6c3568c4;   // first of the numbered city bitmaps
constexpr std::uint32_t kRegionConfigBitmapColorType = 9;             // the cSLBuffer colour type (ARGB8888)
constexpr std::uint32_t kRegionTutorialCityRecordType = 0x6a5b7bf5;
constexpr std::uint32_t kRegionTutorialCityRecordGroup = 0x6a5b7b57;

// The region's list of cities (the original's ilist<cRZAutoRefCount<cISC4RegionalCity>,
// cGZAllocatorServiceSTLAllocator<...>>; the custom allocator is not recovered).
using RegionCityList = std::list<cRZAutoRefCount<cISC4RegionalCity>>;

// Every call cSC4Region makes outside itself. Each method mirrors one original call; the slot or the
// free-function address is in the comment.
class IRegionHost {
public:
    virtual ~IRegionHost() = default;

    // SL::spSC4 (cISC4App, iid 0x26ce01c0) vslot 0x110 -> cISC4Nation vslot 0x28 (GetRegionDirectory).
    virtual void GetRegionDirectory(cRZString& out) = 0;
    // same app/nation, vslot 0x78 (GetRegionDownloadDirectory).
    virtual void GetRegionDownloadDirectory(cRZString& out) = 0;

    // SC4ReadRegionalCity(cIGZString const&, cISC4RegionalCity*&) @ 0x1001e95c3.
    virtual bool ReadRegionalCity(const cIGZString& path, cISC4RegionalCity** out) = 0;
    // SC4WriteRegionalCity(cIGZString const&, cISC4RegionalCity const*) @ 0x1001e9799.
    virtual bool WriteRegionalCity(const cIGZString& path, cISC4RegionalCity* city) = 0;
    // SC4CreateSavePathForCity(directory, cityName, serial, out) @ 0x1001e9aa9.
    virtual void CreateSavePathForCity(const cIGZString& directory, const cIGZString& cityName,
                                       std::uint32_t serial, cRZString& out) = 0;
    // SC4CreateSavePathForCity(cityName, serial, out) @ 0x1001e98a4 (default city directory).
    virtual void CreateSavePathForCity(const cIGZString& cityName, std::uint32_t serial, cRZString& out) = 0;
    // SC4CreateCitySerialNumber @ 0x1001e9ff4.
    virtual std::uint32_t CreateCitySerialNumber() = 0;
    // SC4ReadBinaryRecord(cGZPersistResourceKey const&, cIGZString const&) @ 0x1001e93c7: -1 on failure.
    virtual int ReadBinaryRecordToFile(const cGZPersistResourceKey& key, const cIGZString& path) = 0;
    // SL::spRM vslot 0x70 (resource exists).
    virtual bool HasResource(const cGZPersistResourceKey& key) = 0;
    // cRZRandom::RandomUint32UniformBelow over the engine's global sRandom.
    virtual std::uint32_t RandomUint32UniformBelow(std::uint32_t range) = 0;
    // cSC4TutorialRegistry::SetActive @ 0x1006433ee, reached through cISC4App vslot 0x178.
    virtual void SetTutorialActive(int guid, bool active) = 0;

    // cISC4RegionalCity virtuals the partial interface does not declare (original vslot).
    virtual void GetCityPosition(const cISC4RegionalCity* city, int& x, int& z) = 0;        // 0x28
    virtual void GetCitySize(const cISC4RegionalCity* city, int& width, int& height) = 0;   // 0x38
    virtual void SetCityPosition(cISC4RegionalCity* city, int x, int z, bool unknown) = 0;  // 0x30
    virtual std::uint32_t GetCitySerialNumber(const cISC4RegionalCity* city) = 0;           // 0xd8
    virtual void SetCitySerialNumber(cISC4RegionalCity* city, std::uint32_t serial) = 0;    // 0xe0
    virtual void GetCitySaveFilePath(const cISC4RegionalCity* city, cRZString& out) = 0;    // 0xf8
    virtual void SetCitySaveFilePath(cISC4RegionalCity* city, const cIGZString& path) = 0;  // 0x100
    virtual void GetCityName(const cISC4RegionalCity* city, cRZString& out) = 0;            // 0x108
    virtual std::uint32_t GetTutorialGUID(const cISC4RegionalCity* city) = 0;               // 0x208
    virtual void SetTutorialGUID(cISC4RegionalCity* city, std::uint32_t guid) = 0;          // 0x210
};
IRegionHost* RegionHost();
void SetRegionHost(IRegionHost* host);

// cISC4Region — the interface the region exposes (vtable 0x100a90f60, slots 3..24; slots 25/26 are
// the complete/deleting destructors of cSC4Region).
class cISC4Region : public cIGZUnknown {
public:
    enum eBaseTerrainType : std::uint32_t { kBaseTerrainType0 = 0, kBaseTerrainType1 = 1 };

    // One entry of the region's config bitmap: the top-left cell of a city and log2 of its edge length
    // (1 -> 1x1, 2 -> 2x2, 4 -> 4x4). 12 bytes in the original.
    struct cLocation {
        std::int32_t x = 0;
        std::int32_t z = 0;
        std::int32_t size = 0;
    };

    virtual cIGZString* GetName() = 0;                                                   // [3] 0x18
    virtual void SetName(const cIGZString& name) = 0;                                    // [4] 0x20
    virtual cIGZString* GetDirectoryName() = 0;                                          // [5] 0x28
    virtual void SetDirectoryName(const cIGZString& name) = 0;                           // [6] 0x30
    virtual void LoadConfig() = 0;                                                       // [7] 0x38
    virtual bool Init() = 0;                                                             // [8] 0x40
    virtual bool Shutdown() = 0;                                                         // [9] 0x48
    virtual void Delete() = 0;                                                           // [10] 0x50
    virtual cRZAutoRefCount<cISC4RegionalCity>* GetCity(int x, int z) = 0;               // [11] 0x58
    virtual void InsertCity(cISC4RegionalCity* city) = 0;                                // [12] 0x60
    virtual void RemoveCity(cRZAutoRefCount<cISC4RegionalCity>*& city) = 0;              // [13] 0x68
    virtual void DeleteCity(cRZAutoRefCount<cISC4RegionalCity>*& city) = 0;              // [14] 0x70
    virtual void ReloadCity(cRZAutoRefCount<cISC4RegionalCity>*& city) = 0;              // [15] 0x78
    virtual bool MoveCity(cISC4Region* source, cISC4RegionalCity* city, int x, int z) = 0;   // [16] 0x80
    virtual void GetAllCities(RegionCityList& out) const = 0;                            // [17] 0x88
    virtual eBaseTerrainType GetBaseTerrainType() = 0;                                   // [18] 0x90
    virtual void SetBaseTerrainType(eBaseTerrainType type) = 0;                          // [19] 0x98
    virtual std::uint32_t GetBaseTerrainHeight() = 0;                                    // [20] 0xa0
    virtual void GetWaterPrefs(std::uint8_t& minimum, std::uint8_t& maximum) = 0;        // [21] 0xa8
    virtual bool ResetTutorialCity(int guid) = 0;                                        // [22] 0xb0
    virtual void GetCityLocations(std::vector<cLocation>& out) = 0;                      // [23] 0xb8
    virtual void GetBoundingRect(sc4::SC4RectT<int>& out) = 0;                           // [24] 0xc0
};

class cSC4Region : public cISC4Region {
public:
    // ((x & 0xf) << 4) | (z & 0xf), from the inlined unordered_map find at 0x1002a5936.
    struct RegionalCellHashFunction {
        std::size_t operator()(const sc4::SC4PointT<int>& cell) const noexcept {
            return static_cast<std::size_t>(((cell.x & 0xf) << 4) | (cell.z & 0xf));
        }
    };

    cSC4Region();
    virtual ~cSC4Region();

    // cIGZUnknown
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;

    // cISC4Region
    cIGZString* GetName() override;
    void SetName(const cIGZString& name) override;
    cIGZString* GetDirectoryName() override;
    void SetDirectoryName(const cIGZString& name) override;
    void LoadConfig() override;
    bool Init() override;
    bool Shutdown() override;
    void Delete() override;
    cRZAutoRefCount<cISC4RegionalCity>* GetCity(int x, int z) override;
    void InsertCity(cISC4RegionalCity* city) override;
    void RemoveCity(cRZAutoRefCount<cISC4RegionalCity>*& city) override;
    void DeleteCity(cRZAutoRefCount<cISC4RegionalCity>*& city) override;
    void ReloadCity(cRZAutoRefCount<cISC4RegionalCity>*& city) override;
    bool MoveCity(cISC4Region* source, cISC4RegionalCity* city, int x, int z) override;
    void GetAllCities(RegionCityList& out) const override;
    eBaseTerrainType GetBaseTerrainType() override;
    void SetBaseTerrainType(eBaseTerrainType type) override;
    std::uint32_t GetBaseTerrainHeight() override;
    void GetWaterPrefs(std::uint8_t& minimum, std::uint8_t& maximum) override;
    bool ResetTutorialCity(int guid) override;
    void GetCityLocations(std::vector<cLocation>& out) override;
    void GetBoundingRect(sc4::SC4RectT<int>& out) override;

    // --- helpers the original keeps out of the vtable (private there; exposed for the test) ------
    void LoadCityLocations();                                                        // 0x1002a26ce
    void LoadCities();                                                               // 0x1002a2982
    void MoveOverflowCitiesToDownloadDirectory();                                    // 0x1002a2c1c
    static void LoadCitiesFromDirectory(RegionCityList& out, const cRZString& directory);   // 0x1002a3fd4
    bool IsCitySpaceOccupied(cISC4RegionalCity* city);                                // 0x1002a428e
    void ParseCityLocations(cIGZBuffer* buffer);                                      // 0x1002a5630
    static cSC4Region* CreateRegion(const cIGZString& name);                          // 0x1002a4ec8
    static cSC4Region* CreateStarterRegion();                                         // 0x1002a5390

    // cSC4Region::sBaseTerrainTypeHeight @ 0x100b248e4: two 32-bit heights indexed by the base terrain
    // type (the second one is the original's DAT_100b248e8); the region screen fills both.
    static inline std::uint32_t sBaseTerrainTypeHeight[2] = {0, 0};
    // cSC4Region::mbStaticInitialized @ 0x100b248e0.
    static inline bool sStaticInitialized = false;

private:
    cRZString RegionDirectoryPath() const;     // regions directory + directory name + '/'

    cRZString name_;                // +0x10
    cRZString directoryName_;       // +0x38
    std::uint32_t baseTerrainType_ = 0;   // +0x60
    std::uint32_t waterMin_ = 0x3c;       // +0x64
    std::uint32_t waterMax_ = 100;        // +0x68
    RegionCityList cities_;               // +0x70 (loaded cities with no place in the config bitmap)
    std::vector<cLocation> locations_;    // +0x88
    // +0xa0. The value is a raw holder, see the leaks quirk above.
    std::unordered_map<sc4::SC4PointT<int>, cRZAutoRefCount<cISC4RegionalCity>*, RegionalCellHashFunction> citiesByCell_;
    bool initialized_ = false;            // +0x08
    std::uint32_t refCount_ = 0;          // +0x0c
};
