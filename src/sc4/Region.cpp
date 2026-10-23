#include "sc4/Region.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <set>

#include "gz/Buffer.h"
#include "rz/RZConvert.h"
#include "rz/cRZDirectory.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "sc4/SC4Utilities.h"
#include "sc4/SLBuffer.h"
#include "sc4/SLString.h"

namespace {

IRegionHost* g_host = nullptr;

constexpr const char* kRegionIniFilename = "region.ini";
constexpr const char* kRegionConfigBitmapFilename = "config.bmp";
constexpr const char* kCitySaveExtension = ".sc4";
constexpr const char* kSectionRegionalSettings = "Regional Settings";
constexpr const char* kEntryName = "Name";
constexpr const char* kEntryType = "Terrain type";
constexpr const char* kEntryWaterMin = "Water Min";
constexpr const char* kEntryWaterMax = "Water Max";

void AppendText(cRZString& text, const char* add) {
    text.AppendChars(add, static_cast<std::uint32_t>(std::strlen(add)));
}

cRZString RegionDirectoryPathFor(const cRZString& directoryName) {
    cRZString path;
    if (IRegionHost* host = RegionHost()) host->GetRegionDirectory(path);
    path.AppendChars(directoryName.Data(), directoryName.Strlen());
    AppendText(path, "/");
    return path;
}

void EraseCityCells(std::unordered_map<sc4::SC4PointT<int>, cRZAutoRefCount<cISC4RegionalCity>*,
                                       cSC4Region::RegionalCellHashFunction>& cells,
                    int x, int z, int width, int height) {
    for (int i = 0; i < width; ++i)
        for (int j = 0; j < height; ++j) cells.erase(sc4::SC4PointT<int>{x + i, z + j});
}

}  // namespace

IRegionHost* RegionHost() { return g_host; }
void SetRegionHost(IRegionHost* host) { g_host = host; }

//---------------------------------------------------------------------------------------------------------------------
// construction / COM plumbing
//---------------------------------------------------------------------------------------------------------------------

// @ 0x1002a246c. The max load factor of the cell map is the 1.0f stored at +0xc0.
cSC4Region::cSC4Region() { citiesByCell_.max_load_factor(1.0f); }

// @ 0x1002a251a (body) / 0x1002a25cc (complete object) / 0x1002a25d6 (deleting). Quirk: the cell map is
// destroyed without deleting its holders, so they leak.
cSC4Region::~cSC4Region() = default;

// @ 0x1002a2604
bool cSC4Region::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cSC4Region) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// @ 0x1002a2626
std::uint32_t cSC4Region::AddRef() { return ++refCount_; }

// @ 0x1002a2634: a count below 2 shuts the region down and deletes it.
std::uint32_t cSC4Region::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

//---------------------------------------------------------------------------------------------------------------------
// names and configuration
//---------------------------------------------------------------------------------------------------------------------

// @ 0x1002a588e
cIGZString* cSC4Region::GetName() { return &name_; }

// @ 0x1002a3672
void cSC4Region::SetName(const cIGZString& name) { name_.AssignFromIGZString(name); }

// @ 0x1002a5898
cIGZString* cSC4Region::GetDirectoryName() { return &directoryName_; }

// @ 0x1002a3680
void cSC4Region::SetDirectoryName(const cIGZString& name) { directoryName_.AssignFromIGZString(name); }

// @ 0x1002a368e: reads the "Regional Settings" section of <regions>/<directory>/region.ini.
void cSC4Region::LoadConfig() {
    cRZString path(RegionDirectoryPath());
    AppendText(path, kRegionIniFilename);
    cRZIniFile ini(path);
    cRZString value;
    if (ini.ReadEntry(kSectionRegionalSettings, kEntryName, value) && value.Strlen() != 0) {
        const char* text = value.Data();
        const std::uint32_t instance = text[0] == '#' ? RZ::ConvertHexToUint(text + 1) : 0;
        if (instance != 0) {
            sc4::cSLString localized(kRegionNameStringGroup, instance, true);
            name_.Copy(localized);
        } else {
            name_.Copy(value);
        }
    }

    int number = 0;
    if (ini.ReadNumber(kSectionRegionalSettings, kEntryType, number) && static_cast<std::uint32_t>(number) < 2)
        baseTerrainType_ = static_cast<std::uint32_t>(number);
    if (ini.ReadNumber(kSectionRegionalSettings, kEntryWaterMin, number) && static_cast<std::uint32_t>(number) < 0xff)
        waterMin_ = static_cast<std::uint32_t>(number);
    if (ini.ReadNumber(kSectionRegionalSettings, kEntryWaterMax, number) && static_cast<std::uint32_t>(number) < 0xff)
        waterMax_ = static_cast<std::uint32_t>(number);
}

cRZString cSC4Region::RegionDirectoryPath() const { return RegionDirectoryPathFor(directoryName_); }

// @ 0x1002a58a2 / 0x1002a58ac / 0x1002a58b6 / 0x1002a58ca
cISC4Region::eBaseTerrainType cSC4Region::GetBaseTerrainType() {
    return static_cast<eBaseTerrainType>(baseTerrainType_);
}
void cSC4Region::SetBaseTerrainType(eBaseTerrainType type) { baseTerrainType_ = static_cast<std::uint32_t>(type); }
std::uint32_t cSC4Region::GetBaseTerrainHeight() { return sBaseTerrainTypeHeight[baseTerrainType_]; }
void cSC4Region::GetWaterPrefs(std::uint8_t& minimum, std::uint8_t& maximum) {
    minimum = static_cast<std::uint8_t>(waterMin_);
    maximum = static_cast<std::uint8_t>(waterMax_);
}

// @ 0x1002a2e06: shuts every city down and writes the configuration back.
bool cSC4Region::Shutdown() {
    if (!initialized_) return true;
    initialized_ = false;

    while (!citiesByCell_.empty()) {
        cRZAutoRefCount<cISC4RegionalCity>* holder = citiesByCell_.begin()->second;
        RemoveCity(holder);
    }
    for (cRZAutoRefCount<cISC4RegionalCity>& city : cities_) city->Shutdown();
    cities_.clear();

    cRZString path(RegionDirectoryPath());
    AppendText(path, kRegionIniFilename);
    if (!cRZFile::GetAttribute(path, cRZFile::kAttrWritable)) cRZFile::SetAttribute(path, cRZFile::kAttrWritable, true);
    cRZIniFile ini(path);
    ini.WriteEntry(kSectionRegionalSettings, kEntryName, name_);
    ini.WriteNumber(kSectionRegionalSettings, kEntryType, static_cast<int>(baseTerrainType_));
    ini.WriteNumber(kSectionRegionalSettings, kEntryWaterMin, static_cast<int>(waterMin_));
    ini.WriteNumber(kSectionRegionalSettings, kEntryWaterMax, static_cast<int>(waterMax_));
    return true;
}

// @ 0x1002a2678
bool cSC4Region::Init() {
    if (!initialized_) {
        initialized_ = true;
        sStaticInitialized = true;
        LoadConfig();
        LoadCityLocations();
        LoadCities();
        MoveOverflowCitiesToDownloadDirectory();
    }
    return !citiesByCell_.empty();
}

// @ 0x1002a4336: deletes the cities and all files of the region.
void cSC4Region::Delete() {
    while (!citiesByCell_.empty()) {
        cRZAutoRefCount<cISC4RegionalCity>* holder = citiesByCell_.begin()->second;
        DeleteCity(holder);
    }
    cRZString directory(RegionDirectoryPath());
    cRZString iniPath(directory);
    AppendText(iniPath, kRegionIniFilename);
    cRZString bitmapPath(directory);
    AppendText(bitmapPath, kRegionConfigBitmapFilename);
    cRZFile::Remove(iniPath);
    cRZFile::Remove(bitmapPath);
    cRZDirectory::RemoveDirectoryA(directory, true);
    Shutdown();   // quirk: this writes region.ini again, into the directory just removed
}

//---------------------------------------------------------------------------------------------------------------------
// cities and cells
//---------------------------------------------------------------------------------------------------------------------

// @ 0x1002a461a: the map value is the holder shared by all cells of the city.
cRZAutoRefCount<cISC4RegionalCity>* cSC4Region::GetCity(int x, int z) {
    const auto it = citiesByCell_.find(sc4::SC4PointT<int>{x, z});
    return it == citiesByCell_.end() ? nullptr : it->second;
}

// @ 0x1002a4812. Quirk: a cell that already holds a holder is overwritten without releasing it.
void cSC4Region::InsertCity(cISC4RegionalCity* city) {
    int x = 0, z = 0, width = 0, height = 0;
    RegionHost()->GetCityPosition(city, x, z);
    RegionHost()->GetCitySize(city, width, height);
    auto* holder = new cRZAutoRefCount<cISC4RegionalCity>(city);
    for (int i = 0; i < width; ++i)
        for (int j = 0; j < height; ++j) citiesByCell_[sc4::SC4PointT<int>{x + i, z + j}] = holder;
}

// @ 0x1002a428e: true when any cell of the city's footprint is already taken.
bool cSC4Region::IsCitySpaceOccupied(cISC4RegionalCity* city) {
    int x = 0, z = 0, width = 0, height = 0;
    RegionHost()->GetCityPosition(city, x, z);
    RegionHost()->GetCitySize(city, width, height);
    for (int i = 0; i < width; ++i)
        for (int j = 0; j < height; ++j)
            if (citiesByCell_.find(sc4::SC4PointT<int>{x + i, z + j}) != citiesByCell_.end()) return true;
    return false;
}

// @ 0x1002a4948. Quirk: `city` (the caller's holder pointer) is left dangling.
void cSC4Region::RemoveCity(cRZAutoRefCount<cISC4RegionalCity>*& city) {
    cISC4RegionalCity* object = city->get();
    object->AddRef();
    int x = 0, z = 0, width = 0, height = 0;
    RegionHost()->GetCityPosition(object, x, z);
    RegionHost()->GetCitySize(object, width, height);
    EraseCityCells(citiesByCell_, x, z, width, height);
    object->Shutdown();
    delete city;   // the holder's destructor releases its reference (the original released by hand)
    object->Release();
}

// @ 0x1002a4b6a
void cSC4Region::DeleteCity(cRZAutoRefCount<cISC4RegionalCity>*& city) {
    cISC4RegionalCity* object = city->get();
    object->AddRef();
    cRZString path;
    RegionHost()->GetCitySaveFilePath(object, path);
    RemoveCity(city);
    cRZFile::Remove(path);
    object->Release();
}

// @ 0x1002a4a5c. Quirk: when the save file cannot be read the caller's holder stays dangling.
void cSC4Region::ReloadCity(cRZAutoRefCount<cISC4RegionalCity>*& city) {
    cRZString path;
    RegionHost()->GetCitySaveFilePath(city->get(), path);
    RemoveCity(city);
    cISC4RegionalCity* object = nullptr;
    if (RegionHost()->ReadRegionalCity(path, &object)) {
        RegionHost()->SetCitySaveFilePath(object, path);
        InsertCity(object);
        int x = 0, z = 0;
        RegionHost()->GetCityPosition(object, x, z);
        city = GetCity(x, z);
    }
    if (object) object->Release();
}

// @ 0x1002a464e: one entry per distinct city, in pointer order (the original collects them in a
// std::set<cISC4RegionalCity*> first).
void cSC4Region::GetAllCities(RegionCityList& out) const {
    out.clear();
    std::set<cISC4RegionalCity*> unique;
    for (const auto& entry : citiesByCell_)
        if (entry.second && entry.second->get()) unique.insert(entry.second->get());
    for (cISC4RegionalCity* city : unique) out.push_back(cRZAutoRefCount<cISC4RegionalCity>(city));
}

// @ 0x1002a557a: the bounding rectangle of the configured city locations; all zero when empty or
// when the rectangle is degenerate.
void cSC4Region::GetBoundingRect(sc4::SC4RectT<int>& out) {
    out.x0 = out.z0 = INT_MAX;
    out.x1 = out.z1 = INT_MIN;
    if (!locations_.empty()) {
        for (const cLocation& location : locations_) {
            out.x0 = std::min(out.x0, location.x);
            out.z0 = std::min(out.z0, location.z);
            out.x1 = std::max(out.x1, location.x);
            out.z1 = std::max(out.z1, location.z);
        }
        if (out.x0 < out.x1 && out.z0 < out.z1) return;
    }
    out.x0 = out.z0 = out.x1 = out.z1 = 0;
}

// @ 0x1002a5604
void cSC4Region::GetCityLocations(std::vector<cLocation>& out) {
    if (&locations_ == &out) return;
    out.assign(locations_.begin(), locations_.end());
}

//---------------------------------------------------------------------------------------------------------------------
// config bitmap and city files
//---------------------------------------------------------------------------------------------------------------------

// @ 0x1002a5630: reads the city locations out of config.bmp. Each city is a square of one pure
// colour followed by 0xff in all three channels: 1 -> 1x1, 2 -> 2x2, 4 -> 4x4. Quirk: the bitmap
// area's left/top offsets are ignored.
void cSC4Region::ParseCityLocations(cIGZBuffer* buffer) {
    locations_.clear();
    if (!buffer) return;
    const cRZRect* area = buffer->GetBufferArea();
    const int width = area->right - area->left;
    const int height = area->bottom - area->top;
    if (width <= 0 || height <= 0) return;

    std::vector<bool> occupied(static_cast<std::size_t>(height) * static_cast<std::size_t>(width), false);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(col);
            if (occupied[index]) continue;
            const std::uint32_t native = buffer->GetPixel(static_cast<std::uint32_t>(col), static_cast<std::uint32_t>(row));
            std::uint8_t r = 0, g = 0, b = 0;
            buffer->ConvertNativeValueToRGB(native, r, g, b);
            const std::uint32_t size = (r == 0xff ? 1u : 0u) | (g == 0xff ? 2u : 0u) | (b == 0xff ? 4u : 0u);
            if (size == 0 || (size & (size - 1)) != 0) continue;
            if (col + static_cast<int>(size) > width) continue;
            if (row + static_cast<int>(size) > height) continue;
            for (std::uint32_t i = 0; i < size; ++i)
                for (std::uint32_t j = 0; j < size; ++j)
                    occupied[static_cast<std::size_t>(row + static_cast<int>(i)) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(col + static_cast<int>(j))] = true;
            cLocation location;
            location.x = col;
            location.z = row;
            location.size = static_cast<std::int32_t>(size >> 1);
            locations_.push_back(location);
        }
    }
}

// @ 0x1002a26ce: <regions>/<directory>/config.bmp, or the default bitmap resource when the file is
// missing or cannot be loaded.
void cSC4Region::LoadCityLocations() {
    cRZString path(RegionDirectoryPath());
    AppendText(path, kRegionConfigBitmapFilename);
    if (cRZFile::FileExists(path)) {
        cSLBuffer buffer(path, static_cast<std::int32_t>(kRegionConfigBitmapColorType), false);
        if (buffer.GetBuffer()) {
            ParseCityLocations(buffer.GetBuffer());
            return;
        }
    }
    cSLBuffer fallback(kRegionConfigBitmapType, kRegionConfigBitmapGroup, kRegionConfigBitmapInstance,
                       static_cast<std::int32_t>(kRegionConfigBitmapColorType), false);
    if (fallback.GetBuffer()) ParseCityLocations(fallback.GetBuffer());
}

// @ 0x1002a3fd4: every .sc4 file of `directory`, read through the save-file helper.
void cSC4Region::LoadCitiesFromDirectory(RegionCityList& out, const cRZString& directory) {
    if (!cRZDirectory::DoesDirectoryExist(directory)) return;
    cRZDirectory entries(directory, true, false, false);
    const std::uint32_t count = entries.GetDirectoryEntryCount();
    for (std::uint32_t index = 0; index < count; ++index) {
        const cRZDirectoryEntry* entry = entries.GetNthEntry(index);
        if (!entry || entry->kind != kRZDirectoryEntryFile) continue;
        cRZString extension;
        cRZFile::GetPathExtension(entry->name, extension);
        if (extension.Strlen() != 4 || std::strcmp(extension.ToChar(), kCitySaveExtension) != 0) continue;
        cRZString path(directory);
        path.AppendChars(entry->name.Data(), entry->name.Strlen());
        cISC4RegionalCity* city = nullptr;
        if (!RegionHost()->ReadRegionalCity(path, &city)) continue;
        if (!cRZFile::GetAttribute(path, cRZFile::kAttrWritable)) cRZFile::SetAttribute(path, cRZFile::kAttrWritable, true);
        out.push_back(cRZAutoRefCount<cISC4RegionalCity>(city));
        RegionHost()->SetCitySaveFilePath(city, path);
        city->Release();
    }
}

// @ 0x1002a2982: loads the region's city files, puts the ones matching a config-bitmap location into
// the cell map and keeps the others in the overflow list.
void cSC4Region::LoadCities() {
    cRZString directory(RegionDirectoryPath());
    RegionCityList loaded;
    LoadCitiesFromDirectory(loaded, directory);
    for (auto it = loaded.begin(); it != loaded.end();) {
        cISC4RegionalCity* city = it->get();
        bool placed = false;
        if (!IsCitySpaceOccupied(city)) {
            int x = 0, z = 0, width = 0, height = 0;
            RegionHost()->GetCityPosition(city, x, z);
            RegionHost()->GetCitySize(city, width, height);
            for (const cLocation& location : locations_) {
                const std::uint32_t size = 1u << (location.size & 0x1f);
                if (static_cast<std::uint32_t>((z - location.z) | (x - location.x)) < size) {
                    if (x == location.x && z == location.z && size == static_cast<std::uint32_t>(width)) {
                        InsertCity(city);
                        placed = true;
                    }
                    break;
                }
            }
        }
        if (placed)
            it = loaded.erase(it);
        else
            ++it;
    }
    cities_.splice(cities_.end(), loaded);
}

// @ 0x1002a2c1c: moves the overflow cities' save files to the region download directory.
void cSC4Region::MoveOverflowCitiesToDownloadDirectory() {
    while (!cities_.empty()) {
        cISC4RegionalCity* city = cities_.front().get();
        if (city) city->AddRef();
        cities_.pop_front();
        city->Shutdown();
        cRZString savePath, cityName, downloadDirectory, newPath;
        RegionHost()->GetCitySaveFilePath(city, savePath);
        RegionHost()->GetCityName(city, cityName);
        const std::uint32_t serial = RegionHost()->GetCitySerialNumber(city);
        RegionHost()->GetRegionDownloadDirectory(downloadDirectory);
        RegionHost()->CreateSavePathForCity(downloadDirectory, cityName, serial, newPath);
        if (cRZFile::Copy(savePath, newPath, true)) cRZFile::Remove(savePath);
        city->Release();
    }
}

//---------------------------------------------------------------------------------------------------------------------
// moving, reloading and tutorial cities
//---------------------------------------------------------------------------------------------------------------------

// @ 0x1002a4c28: moves `city` to this region's (x, z). Both regions are shut down, the save file is
// renamed, its region record is updated and this region is initialised again.
bool cSC4Region::MoveCity(cISC4Region* source, cISC4RegionalCity* city, int x, int z) {
    cISC4Region* other = this != source ? source : nullptr;
    cRZAutoRefCount<cISC4RegionalCity>* occupant = GetCity(x, z);
    if (occupant) {
        if (occupant->get() == city) return true;
        DeleteCity(occupant);
    }

    cRZString savePath, cityName;
    RegionHost()->GetCitySaveFilePath(city, savePath);
    RegionHost()->GetCityName(city, cityName);
    // Quirk: the serial used is the first value of the sequence old serial, generated serial,
    // generated serial - 1, ... that no city of the destination has. A collision with the old serial
    // therefore makes the city keep a freshly generated serial, not a decremented one.
    const std::uint32_t oldSerial = RegionHost()->GetCitySerialNumber(city);
    std::uint32_t candidate = RegionHost()->CreateCitySerialNumber();
    std::uint32_t serial = oldSerial;
    for (;;) {
        bool collision = false;
        for (const auto& entry : citiesByCell_) {
            if (RegionHost()->GetCitySerialNumber(entry.second->get()) == serial) {
                serial = candidate;
                candidate = candidate - 1;
                collision = true;
                break;
            }
        }
        if (!collision) break;
    }

    if (other) other->Shutdown();
    Shutdown();

    bool ok;
    cRZString newPath;
    RegionHost()->CreateSavePathForCity(cityName, serial, newPath);
    ok = cRZFile::Rename(savePath, newPath);
    if (ok) {
        cRZFile::SetAttribute(newPath, cRZFile::kAttrWritable, true);
        cISC4RegionalCity* moved = nullptr;
        ok = RegionHost()->ReadRegionalCity(newPath, &moved);
        if (ok) {
            RegionHost()->SetCitySerialNumber(moved, serial);
            RegionHost()->SetCityPosition(moved, x, z, true);
            RegionHost()->WriteRegionalCity(newPath, moved);
            moved->Shutdown();
            moved->Release();
        }
    }
    Init();
    return ok;
}

// @ 0x1002a53e4. Quirk: the result is true when the tutorial-city record could NOT be read.
bool cSC4Region::ResetTutorialCity(int guid) {
    if (guid == -1) return false;
    RegionCityList cities;
    GetAllCities(cities);
    bool result = false;
    for (cRZAutoRefCount<cISC4RegionalCity>& holder : cities) {
        cISC4RegionalCity* city = holder.get();
        if (static_cast<int>(RegionHost()->GetTutorialGUID(city)) != guid) continue;
        cRZString path;
        RegionHost()->GetCitySaveFilePath(city, path);
        const cGZPersistResourceKey key{kRegionTutorialCityRecordType, kRegionTutorialCityRecordGroup,
                                        static_cast<std::uint32_t>(guid)};
        const int readResult = RegionHost()->ReadBinaryRecordToFile(key, path);
        result = readResult == -1;
        if (readResult != -1) {
            int x = 0, z = 0;
            RegionHost()->GetCityPosition(city, x, z);
            cRZAutoRefCount<cISC4RegionalCity>* reloaded = GetCity(x, z);
            if (reloaded) {
                ReloadCity(reloaded);
                // Quirk: if ReloadCity could not read the file `reloaded` is dangling here.
                RegionHost()->SetTutorialGUID(reloaded->get(), static_cast<std::uint32_t>(guid));
            }
            RegionHost()->SetTutorialActive(guid, true);
        }
        break;
    }
    cities.clear();
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
// region creation
//---------------------------------------------------------------------------------------------------------------------

// @ 0x1002a4ec8: creates the region object and its directory, then extracts the config bitmap of one
// of the numbered city resources into config.bmp.
cSC4Region* cSC4Region::CreateRegion(const cIGZString& requestedName) {
    auto* region = new cSC4Region();

    cRZString sanitized;
    sc4::SC4SanitizeFileName(cRZString(requestedName), sanitized);
    cRZString candidate(sanitized);
    cRZString path;
    std::uint32_t counter = 2;
    for (;;) {
        path = RegionDirectoryPathFor(candidate);
        if (!cRZDirectory::DoesDirectoryExist(path)) break;
        cRZString suffixed;
        suffixed.Sprintf("%s (%d)", sanitized.ToChar(), counter);
        ++counter;
        candidate = suffixed;
    }
    region->SetName(requestedName);
    region->SetDirectoryName(candidate);

    if (cRZDirectory::CreateDirectoryA(path)) {
        cGZPersistResourceKey key{kRegionConfigBitmapType, kRegionConfigBitmapGroup, kRegionConfigBitmapFirstCity};
        std::uint32_t count = 0;
        while (RegionHost()->HasResource(key)) {
            ++count;
            ++key.instance;
        }
        if (count > 0) {
            key.instance = kRegionConfigBitmapFirstCity + RegionHost()->RandomUint32UniformBelow(count);
            cRZString bitmapPath(path);
            AppendText(bitmapPath, kRegionConfigBitmapFilename);
            RegionHost()->ReadBinaryRecordToFile(key, bitmapPath);
        }
    }
    return region;
}

// @ 0x1002a5390
cSC4Region* cSC4Region::CreateStarterRegion() {
    sc4::cSLString name;
    return CreateRegion(name);
}
