// cSC4Nation — see Nation.h for the layout, the vtable groups and the quirks. Everything the original reached
// through SL:: goes through INationHost; the game installs the host with SetNationHost.
#include "sc4/Nation.h"

#include <cstring>

#include "gz/cGZDate.h"
#include "rz/cRZDirectory.h"
#include "rz/cRZFile.h"
#include "sc4/SCProperty.h"
#include "sc4/SCPropertyHelp.h"

namespace {

INationHost* g_host = nullptr;

// The directory-name string of a region (cISC4Region slot 0x28), never null in the original.
cRZString RegionDirectoryName(cISC4Region* region) {
    if (!region) return cRZString();
    const cIGZString* name = region->GetDirectoryName();
    return name ? cRZString(*name) : cRZString();
}

}  // namespace

INationHost* NationHost() { return g_host; }
void SetNationHost(INationHost* host) { g_host = host; }

//---------------------------------------------------------------------------------------------------------------------
// construction, COM plumbing, static initialisation
//---------------------------------------------------------------------------------------------------------------------

// @ 0x1002887ac. The three vptrs are written by the compiler; the body zeroes the initialised byte, the refcount
// and the 0x1c..0x3b block (the region vector and the active index).
cSC4Nation::cSC4Nation() {
    initialised_ = false;
    refCount_ = 0;
    activeRegionIndex_ = 0;   // the ctor zeroes +0x38; Init is what sets -1
}

// @ 0x100288810 (body) / 0x100288876 (complete) / 0x100288880 (deleting). The destructor does not delete the
// curve objects of the economy map (only Shutdown does), so an instance that dies without Shutdown leaks them.
cSC4Nation::~cSC4Nation() = default;

// @ 0x1002888ae. Quirk: the serializable view (0xe4fda3d4) is not answered.
bool cSC4Nation::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0x452294aa) {   // cIGZMessageTarget2, the +8 view
        *out = static_cast<cIGZMessageTarget2*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cISC4Nation) {
        *out = static_cast<cISC4Nation*>(this);
        AddRef();
        return true;
    }
    return false;
}

// @ 0x1002888fe (the +8 / +0x10 thunks read +0x14 / +0xc instead of +0x1c).
std::uint32_t cSC4Nation::AddRef() { return ++refCount_; }

// @ 0x100288928: below two the object shuts itself down and deletes itself through the deleting destructor.
std::uint32_t cSC4Nation::Release() {
    if (refCount_ >= 2) return --refCount_;
    Shutdown();
    delete this;
    return 0;
}

// @ 0x1002889e0. The one-shot flag is set before the application is looked at, so an instance created before the
// application is up leaves the directory strings empty for the rest of the run.
bool cSC4Nation::StaticInit() {
    if (mbStaticInitialized) return true;
    mbStaticInitialized = true;
    INationHost* h = NationHost();
    if (!h || !h->ApplicationPresent()) return true;

    cRZString directory;
    h->RegionsDirectory(directory);
    msRegionDirectory = directory;
    if (!cRZDirectory::DoesDirectoryExist(msRegionDirectory)) cRZDirectory::CreateDirectoryA(msRegionDirectory);

    msRegionDownloadDirectory = msRegionDirectory;
    msRegionDownloadDirectory.AppendChars(kRegionDownloadDirName, static_cast<std::uint32_t>(std::strlen(kRegionDownloadDirName)));
    msRegionDownloadDirectory.AppendChars("/", 1);
    if (!cRZDirectory::DoesDirectoryExist(msRegionDownloadDirectory))
        cRZDirectory::CreateDirectoryA(msRegionDownloadDirectory);
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
// the economy curves
//---------------------------------------------------------------------------------------------------------------------

// @ 0x1002891b2. Reads the nation's tuning exemplar: a u32 array of demand types (property 0x4a1f629f) with one
// response curve per entry (properties 0x4a1f62a0 + i) and the per-difficulty tunables (property 0x4a1f6300).
// Quirk: the tunable read runs once per curve, inside the loop.
void cSC4Nation::ReadSimulatorTunableValues() {
    tunables_.resize(3, 0.0f);

    INationHost* h = NationHost();
    if (!h) return;
    const cGZPersistResourceKey key{kNationTuningType, kNationTuningGroup, kNationTuningInstance};
    sc4::cISCPropertyHolder* holder = h->LoadExemplar(key);
    if (!holder) return;

    std::vector<std::uint32_t> demandTypes;
    if (!sc4::cSCPropertyHelp::GetPropertyValue(holder, kNationTuningDemandTypes, demandTypes)) {
        h->ReleaseExemplar(holder);
        return;
    }
    for (std::size_t i = 0; i < demandTypes.size(); ++i) {
        const std::uint32_t demandType = demandTypes[i];
        auto it = economyCurves_.find(demandType);
        if (it == economyCurves_.end()) {
            auto* curve = new cSC4ResponseCurve();
            economyCurves_[demandType] = curve;
            it = economyCurves_.find(demandType);
        }
        std::vector<float> values;
        if (sc4::cSCPropertyHelp::GetPropertyValue(holder, kNationTuningCurveBase + static_cast<std::uint32_t>(i), values))
            it->second->SetResponseCurveData(values.data(), values.size());
        sc4::cSCPropertyHelp::GetPropertyValue(holder, kNationTuningTunables, tunables_);
    }
    h->ReleaseExemplar(holder);
}

cSC4ResponseCurve* cSC4Nation::EconomyCurve(std::uint32_t demandType) const {
    auto it = economyCurves_.find(demandType);
    return it == economyCurves_.end() ? nullptr : it->second;
}

// @ 0x10028b0d0. The input of the curve is the number of months between the simulation start date (1/1/2000) and
// the current simulation date; the city's difficulty level picks one of the tunable values that is added to it.
float cSC4Nation::GetEconomyModifier(std::uint32_t demandType) {
    INationHost* h = NationHost();

    cGZDate startDate;   // today, then overwritten with the simulation start date
    if (h) h->SimStartDate(startDate);

    int months = 0;
    if (h) {
        if (cIGZDate* date = h->SimDate()) {
            const int years = static_cast<int>(date->Year()) - static_cast<int>(startDate.Year());
            months = years * 12 + static_cast<int>(date->Month()) - static_cast<int>(startDate.Month());
        }
    }

    cSC4ResponseCurve* curve = nullptr;
    auto it = economyCurves_.find(demandType);
    if (it != economyCurves_.end()) {
        curve = it->second;
    } else if (!economyCurves_.empty() && economyCurves_.begin()->first == 0) {
        // The fallback is the leftmost key, but only when that key is 0 (the original reads through the tree's
        // end node otherwise, which is out of bounds; the port leaves the curve out in that case).
        curve = economyCurves_.begin()->second;
    }

    float value = curve ? curve->GetOutputValue(static_cast<float>(months)) : 0.0f;
    if (h && h->CityPresent()) {
        const int difficulty = h->CityDifficultyLevel();
        if (difficulty >= 0 && static_cast<std::size_t>(difficulty) < tunables_.size())
            value += tunables_[static_cast<std::size_t>(difficulty)];
    }
    return value;
}

//---------------------------------------------------------------------------------------------------------------------
// Init / Shutdown
//---------------------------------------------------------------------------------------------------------------------

// @ 0x100288a9a. Sets up the statics, reads the tuning, loads (or creates) the regions, picks the active one from
// the preferences or from the tutorial registry and finally resets the tutorial cities when it has to.
bool cSC4Nation::Init() {
    if (initialised_) return true;
    initialised_ = true;

    StaticInit();
    ReadSimulatorTunableValues();

    regions_.clear();   // releases every region
    if (!LoadRegions() && !CopyNewUserRegions()) {
        cSC4Region* starter = cSC4Region::CreateStarterRegion();
        if (starter) starter->AddRef();
        regions_.emplace_back(starter);
        if (starter) starter->Release();
    }

    INationHost* h = NationHost();
    const int count = static_cast<int>(regions_.size());
    const cRZString defaultDirectory =
        h ? h->TutorialRegionDefaultDirName(h->TutorialLanguageIndex()) : cRZString();

    activeRegionIndex_ = -1;
    int index = 0;
    if (GetIndexOfCurrentRegionSettingInPreferences(index)) {
        activeRegionIndex_ = index;
    } else {
        // The tutorial regions are the ones with a city that has a tutorial number; the first of them becomes the
        // active region when the preferences did not name one.
        std::list<int> tutorialRegions;
        for (int i = 0; i < count; ++i) {
            if (activeRegionIndex_ < 0) activeRegionIndex_ = i;
            cISC4Region* region = regions_[static_cast<std::size_t>(i)].get();
            if (!region || !region->Init()) continue;
            RegionCityList cities;
            region->GetAllCities(cities);
            for (const cRZAutoRefCount<cISC4RegionalCity>& city : cities) {
                if (city.get() && h && h->CityTutorialGUID(city.get()) != -1) {
                    tutorialRegions.push_front(i);   // the original pushes at the front and pops the back
                    break;
                }
            }
            region->Shutdown();
        }
        while (!tutorialRegions.empty()) {
            const int candidate = tutorialRegions.back();
            tutorialRegions.pop_back();
            cISC4Region* region = regions_[static_cast<std::size_t>(candidate)].get();
            cRZString directory(RegionDirectoryName(region));
            directory.MakeLower();
            if (directory.IsEqualToStringRef(defaultDirectory, true)) {
                activeRegionIndex_ = candidate;
                break;
            }
        }
    }

    bool activeInitFailed = true;
    if (activeRegionIndex_ >= 0 && static_cast<std::size_t>(activeRegionIndex_) < regions_.size()) {
        cISC4Region* region = regions_[static_cast<std::size_t>(activeRegionIndex_)].get();
        activeInitFailed = !region || !region->Init();
    }

    // The host is the message server too (the original's `if (SL::spMS2)` guard).
    if (h) {
        h->AddNotification(static_cast<cIGZMessageTarget2*>(this), kNationMessage1);
        h->AddNotification(static_cast<cIGZMessageTarget2*>(this), kNationMessage2);
        h->AddNotification(static_cast<cIGZMessageTarget2*>(this), kNationResetTutorialCitiesMessage);
    }

    if (activeInitFailed) {
        ResetTutorialCities();
        return true;
    }

    // When no region carries the tutorial registry's default directory name and this is the first run after the
    // installation, the tutorial cities are reset.
    bool directoryFound = false;
    for (int i = 0; i < count && !directoryFound; ++i) {
        cRZString directory(RegionDirectoryName(regions_[static_cast<std::size_t>(i)].get()));
        directory.MakeLower();
        directoryFound = directory.IsEqualToStringRef(defaultDirectory, true);
    }
    if (!directoryFound && h && h->IsRunFirstTimeAfterInstall()) ResetTutorialCities();
    return true;
}

// @ 0x100289748. Always answers true; the inactive case is a no-op.
bool cSC4Nation::Shutdown() {
    if (!initialised_) return true;
    initialised_ = false;

    if (INationHost* h = NationHost()) {
        h->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), kNationMessage1);
        h->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), kNationMessage2);
        h->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), kNationResetTutorialCitiesMessage);
    }

    // Quirk: the original indexes the vector with the active index without checking it.
    if (activeRegionIndex_ >= 0 && static_cast<std::size_t>(activeRegionIndex_) < regions_.size()) {
        if (cISC4Region* region = regions_[static_cast<std::size_t>(activeRegionIndex_)].get()) region->Shutdown();
    }
    regions_.clear();

    for (auto& entry : economyCurves_) delete entry.second;
    economyCurves_.clear();
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
// persistence: only a version word; the regions are rebuilt by Init
//---------------------------------------------------------------------------------------------------------------------

// @ 0x10028988c
bool cSC4Nation::Write(cIGZOStream& stream) const {
    stream.SetUint16(kNationStreamVersion);
    return !stream.GetError();
}

// @ 0x1002898e4
bool cSC4Nation::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != kNationStreamVersion) return false;
    return !stream.GetError();
}

// @ 0x10028b52a
std::uint32_t cSC4Nation::GetGZCLSID() const { return kGZCLSID_cSC4Nation; }

// @ 0x100289a2c
bool cSC4Nation::DoMessage(cIGZMessage2* message) {
    if (message && message->GetType() == kNationResetTutorialCitiesMessage) ResetTutorialCities();
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
// the regions
//---------------------------------------------------------------------------------------------------------------------

// @ 0x10028b4d0 / 0x10028b4de / 0x10028b4f0 / 0x10028b4fa / 0x10028b50c / 0x10028b51c
cRZString* cSC4Nation::GetRegionDirectory() { return &msRegionDirectory; }
std::uint32_t cSC4Nation::GetRegionCount() const { return static_cast<std::uint32_t>(regions_.size()); }
int cSC4Nation::GetActiveRegionIndex() const { return activeRegionIndex_; }
cRZString* cSC4Nation::GetRegionDownloadDirectory() { return &msRegionDownloadDirectory; }

cISC4Region* cSC4Nation::GetActiveRegion() const {
    if (activeRegionIndex_ < 0 || static_cast<std::size_t>(activeRegionIndex_) >= regions_.size()) return nullptr;
    return regions_[static_cast<std::size_t>(activeRegionIndex_)].get();
}

cISC4Region* cSC4Nation::GetRegion(std::uint32_t index) {
    if (index >= regions_.size()) return nullptr;
    return regions_[index].get();
}

// @ 0x100289a88. A no-op when the index is already active; otherwise the old region is shut down and the new one
// initialised. The original does not check the range (see Nation.h).
void cSC4Nation::SetActiveRegion(std::uint32_t index) {
    if (static_cast<std::uint32_t>(activeRegionIndex_) == index) return;
    if (activeRegionIndex_ >= 0 && static_cast<std::size_t>(activeRegionIndex_) < regions_.size()) {
        if (cISC4Region* region = regions_[static_cast<std::size_t>(activeRegionIndex_)].get()) region->Shutdown();
    }
    activeRegionIndex_ = static_cast<std::int32_t>(index);
    if (index < regions_.size()) {
        if (cISC4Region* region = regions_[index].get()) region->Init();
    }
}

// @ 0x100289ac6. Compares the 0x40-byte region name stored in the preferences with every region's name; the
// comparison is the whole stored field against the region name (std::string::compare(0, 0x40, field)).
bool cSC4Nation::GetIndexOfCurrentRegionSettingInPreferences(int& index) {
    INationHost* h = NationHost();
    const std::uint8_t* preferences = h ? h->Preferences() : nullptr;

    char field[kRegionNamePreferenceLength + 1];
    if (preferences) {
        std::memcpy(field, preferences + kRegionNamePreferenceOffset, kRegionNamePreferenceLength);
    } else {
        std::memset(field, 0, sizeof field);
    }
    field[kRegionNamePreferenceLength] = '\0';

    // The original copies the whole 0x40-byte field into a string (embedded NULs included) and then cuts it at
    // the first NUL.
    cRZString preferenceName;
    preferenceName.AssignFromChars(field, kRegionNamePreferenceLength);
    const std::size_t terminator = preferenceName.str().find('\0');
    if (terminator != std::string::npos) preferenceName.Erase(static_cast<std::uint32_t>(terminator), 0xffffffffu);

    index = 0;
    const std::uint32_t count = GetRegionCount();
    for (std::uint32_t i = 0; i < count; ++i) {
        cISC4Region* region = GetRegion(i);
        if (!region || !region->GetName()) continue;
        const cRZString name(*region->GetName());
        if (name.str().compare(0, kRegionNamePreferenceLength, preferenceName.str().c_str()) == 0) {
            index = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

// @ 0x100289362. Every sub-directory of the region directory except "Downloads" becomes a region; the region's
// name and directory name are the entry's name, then region.ini is read.
bool cSC4Nation::LoadRegions() {
    cRZDirectory directory(msRegionDirectory, true, false, false);
    const std::uint32_t count = directory.GetDirectoryEntryCount();
    const cRZString downloadName(kRegionDownloadDirName);
    int added = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const cRZDirectoryEntry* entry = directory.GetNthEntry(i);
        if (!entry || entry->kind != kRZDirectoryEntryDirectory) continue;
        if (entry->name.IsEqualToStringRef(downloadName, false)) continue;
        auto* region = new cSC4Region();
        region->AddRef();
        regions_.emplace_back(region);
        region->Release();
        region->SetName(entry->name);
        region->SetDirectoryName(entry->name);
        region->LoadConfig();
        ++added;
    }
    return added != 0;
}

// @ 0x100289582. Copies the data directory's "Regions" folder, then the current language's "Regions" folder, into
// the user's region directory and loads the result. Quirk: the data-directory read is thrown away before the
// language folder is built, and the return value of both copies is ignored.
bool cSC4Nation::CopyNewUserRegions() {
    if (msRegionDownloadDirectory.Strlen() == 0) return false;
    if (!cRZDirectory::DoesDirectoryExist(msRegionDirectory)) return false;

    INationHost* h = NationHost();
    if (!h) return false;

    cRZString source;
    h->DataDirectory(source);
    source.AppendChars("Regions", 7);
    source.AppendChars("/", 1);
    CopyRegionFiles(msRegionDirectory, source);

    cRZString languageDirectory;
    if (h->LanguageDirectoryName(languageDirectory, 0)) {
        source = languageDirectory;   // the original overwrites the data directory it just read
        source.AppendChars("/", 1);
        source.AppendChars("Regions", 7);
        source.AppendChars("/", 1);
        CopyRegionFiles(msRegionDirectory, source);
    }
    return LoadRegions();
}

// @ 0x100289d54. Makes the region active, deletes it, picks the neighbour as the new active region and removes
// the entry from the vector (the active index shifts down when it was above the removed one).
void cSC4Nation::DeleteRegion(std::uint32_t index) {
    SetActiveRegion(index);
    if (index >= regions_.size()) return;

    cISC4Region* region = regions_[index].get();
    if (!region) return;

    region->Delete();
    if (static_cast<std::uint32_t>(activeRegionIndex_) == index) {
        const std::uint32_t next = (index + 1 == regions_.size()) ? index - 1 : index + 1;
        SetActiveRegion(next);
    }

    const cRZString directoryName(RegionDirectoryName(region));
    cRZString path(msRegionDirectory);
    path.AppendString(directoryName);
    cRZDirectory::RemoveDirectoryA(path, false);

    regions_.erase(regions_.begin() + index);
    if (static_cast<std::uint32_t>(activeRegionIndex_) > index) --activeRegionIndex_;
}

// @ 0x100289f04. Creates a region with that name and appends it; the index it got is the answer.
std::uint32_t cSC4Nation::AddNewRegion(const cIGZString& name) {
    cSC4Region* region = cSC4Region::CreateRegion(name);
    if (region) region->AddRef();
    regions_.emplace_back(region);
    if (region) region->Release();
    return static_cast<std::uint32_t>(regions_.size() - 1);
}

//---------------------------------------------------------------------------------------------------------------------
// tutorial cities
//---------------------------------------------------------------------------------------------------------------------

// @ 0x100289fa6. Re-creates the tutorial cities of every tutorial region: the region directories the tutorial
// registry lists are walked (the registry's default one last), each region is found by directory name, a region
// without a usable directory is deleted and created again (its files copied out of the data directory), then every
// tutorial city the registry lists is read from its record file and moved into the region.
//
// The original also destroys and recreates the region screen when that is the current screen, which is a seam here
// (IsRegionScreenCurrent / DestroyCurrentScreen / CreateRegionScreen).
void cSC4Nation::ResetTutorialCities() {
    INationHost* h = NationHost();
    if (!h || !h->ApplicationPresent()) return;
    if (h->CityPresent()) return;

    const bool regionScreenWasCurrent = h->IsRegionScreenCurrent();

    std::list<cRZString> regionDirectories;
    h->TutorialRegionDirectoryNames(regionDirectories);
    const cRZString defaultDirectory = h->TutorialRegionDefaultDirName(h->TutorialLanguageIndex());

    // Move the registry's default directory to the end of the list.
    for (auto it = regionDirectories.begin(); it != regionDirectories.end(); ++it) {
        if (it->IsEqualToStringRef(defaultDirectory, true)) {
            const cRZString directory(*it);
            regionDirectories.erase(it);
            regionDirectories.push_back(directory);
            break;
        }
    }

    if (regionScreenWasCurrent) h->DestroyCurrentScreen();

    bool failed = false;
    while (!regionDirectories.empty()) {
        const cRZString directory = regionDirectories.front();
        regionDirectories.pop_front();

        int index = -1;
        cISC4Region* region = nullptr;
        const std::uint32_t count = GetRegionCount();
        for (std::uint32_t i = 0; i < count; ++i) {
            cISC4Region* candidate = GetRegion(i);
            if (!candidate) continue;
            cRZString candidateName(RegionDirectoryName(candidate));
            candidateName.MakeLower();
            if (candidateName.IsEqualToStringRef(directory, true)) {
                index = static_cast<int>(i);
                region = candidate;
                break;
            }
        }

        if (region) {
            // A region directory that holds no city files, or cities but no region.ini, is rebuilt from scratch.
            cRZString path(msRegionDirectory);
            path.AppendString(RegionDirectoryName(region));
            path.AppendChars("/", 1);
            if (cRZDirectory::DoesDirectoryExist(path)) {
                bool remove = true;
                cRZString citiesPattern(path);
                citiesPattern.AppendChars("*.sc4", 5);
                if (cRZDirectory::DoesAnyEntryExistThatMatchesPattern(citiesPattern)) {
                    cRZString iniPattern(path);
                    iniPattern.AppendChars("region.ini", 10);
                    remove = !cRZDirectory::DoesAnyEntryExistThatMatchesPattern(iniPattern);
                }
                if (remove) {
                    DeleteRegion(static_cast<std::uint32_t>(index));
                    region = nullptr;
                }
            }
        }

        bool created = false;
        if (!region) {
            region = GetRegion(AddNewRegion(directory));
            if (!region) {
                failed = true;
                break;
            }
            created = true;
        }

        // "<region directory>/<region directory name>/"
        cRZString regionPath(msRegionDirectory);
        regionPath.AppendString(RegionDirectoryName(region));
        regionPath.AppendChars("/", 1);

        if (created) {
            // Copy the region's files out of the data directory: "<data>/Regions/<name>/".
            cRZString source;
            h->DataDirectory(source);
            source.AppendChars("Regions", 7);
            source.AppendChars("/", 1);
            source.AppendString(directory);
            source.AppendChars("/", 1);

            cRZDirectory sourceDirectory(source, false, false, false);
            std::list<cRZString> files;
            sourceDirectory.ReadDirectoryFileEntriesIntoStringListRecursive(source, files, true);
            const std::uint32_t sourceLength = source.Strlen();
            for (const cRZString& file : files) {
                const cRZString relative(file.ToChar() + sourceLength);
                cRZString target(regionPath);
                target.AppendString(relative);
                cRZString name, directoryPath;
                cRZDirectory::SplitDirectoryPath(target, name, directoryPath);
                if (!cRZDirectory::DoesDirectoryExist(directoryPath)) cRZDirectory::CreateDirectoryA(directoryPath);
                cRZFile::Copy(file, target, true);
                cRZFile::SetAttribute(target, 2, true);
            }
        }

        region->Shutdown();

        std::list<NationTutorialItem> items;
        h->TutorialItems(items, region);

        cRZString prefix(regionPath);
        prefix.AppendChars("Tutorial-City", 13);
        while (!items.empty()) {
            const NationTutorialItem item = items.front();
            items.pop_front();
            const std::int32_t guid = item.guid;
            const cGZPersistResourceKey key{kRegionTutorialCityRecordType, kRegionTutorialCityRecordGroup,
                                            static_cast<std::uint32_t>(guid)};
            cRZString file;
            file.Sprintf("%s-%08x.sc4", prefix.ToChar(), static_cast<unsigned>(guid));
            if (h->ReadBinaryRecord(key, file) == -1) {
                h->SetTutorialActive(guid, false);
                continue;
            }
            cISC4RegionalCity* city = nullptr;
            if (!h->ReadRegionalCity(file, city) || !city) {
                h->SetTutorialActive(guid, false);
                continue;
            }
            h->SetCitySaveFilePath(city, file);
            int x = 0, z = 0;
            h->CityPosition(city, x, z);
            region->MoveCity(region, city, x, z);
            city->Shutdown();
            city->Release();
            h->SetTutorialActive(guid, true);
        }
    }

    if (!failed && regionScreenWasCurrent) h->CreateRegionScreen();
}

//---------------------------------------------------------------------------------------------------------------------
// CopyRegionFiles — the free helper that lives in this translation unit
//---------------------------------------------------------------------------------------------------------------------

// @ 0x10028b224. `source` is read recursively; every file is copied to `destination` + the path below `source`,
// creating the sub-directories and marking the copy (cRZFile::SetAttribute(path, 2, true)).
void CopyRegionFiles(cRZString& destination, cRZString& source) {
    cRZDirectory sourceDirectory(source, false, false, false);
    std::list<cRZString> files;
    const std::uint32_t sourceLength = source.Strlen();
    sourceDirectory.ReadDirectoryFileEntriesIntoStringListRecursive(source, files, true);
    for (const cRZString& file : files) {
        const cRZString relative(file.ToChar() + sourceLength);
        cRZString target(destination);
        target.AppendString(relative);
        cRZString name, directoryPath;
        cRZDirectory::SplitDirectoryPath(target, name, directoryPath);
        if (!cRZDirectory::DoesDirectoryExist(directoryPath)) cRZDirectory::CreateDirectoryA(directoryPath);
        cRZFile::Copy(file, target, true);
        cRZFile::SetAttribute(target, 2, true);
    }
}
