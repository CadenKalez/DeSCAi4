// cSC4Nation — the country the player is in: the regions the game knows (cSC4Region objects), which one is
// active, the tutorial-region bookkeeping and the economy curves that the demand simulator, the budget and the
// advisors read. 29 functions, 0x1002887ac..0x10028b541, plus the free helper CopyRegionFiles @ 0x10028b224
// (it lives in the same translation unit, right before the small getters).
//
// Interfaces (three vtable groups): +0 cISC4Nation (iid 0x89d2c366, 23 slots: the class's own
// QueryInterface/AddRef/Release/Init/Shutdown/the region accessors/GetEconomyModifier/the destructors/
// Write/Read/GetGZCLSID/DoMessage), +8 cIGZMessageTarget2 (iid 0x452294aa), +0x10 cIGZSerializable
// (iid 0xe4fda3d4). QueryInterface answers 1 (cIGZUnknown), 0x89d2c366 (cISC4Nation, the object itself) and
// 0x452294aa (the +8 message-target view) only — the serializable view is *not* answered (0x1002888ae).
//
// Original layout (0x70 bytes; the ctor at 0x1002887ac writes exactly these):
//   +0x00 vptr of the primary group      +0x08 vptr of the message target   +0x10 vptr of the serializable view
//   +0x18 bool        initialised byte (Init sets it, Shutdown clears it)
//   +0x1c uint32      refcount (AddRef at 0x1002888fe; the thunks use +0x14 / +0xc)
//   +0x20 std::vector<cRZAutoRefCount<cISC4Region>>   begin +0x20, end +0x28, capacity +0x30
//   +0x38 int32       index of the active region, -1 when none (Init sets it before it knows one)
//   +0x40 std::map<uint32_t, cSC4ResponseCurve*>  the economy curves keyed by demand type
//                     (libc++ tree: begin node +0x40, root +0x48, size +0x50)
//   +0x58 std::vector<float>  tunables (begin +0x58, end +0x60, capacity +0x68); ReadSimulatorTunableValues
//                     resizes it to three entries and then overwrites it from property 0x4a1f6300
// The three statics are the directory strings the class keeps between instances: cSC4Nation::msRegionDirectory
// (0x100b24840), msRegionDownloadDirectory (0x100b24868) and mbStaticInitialized (0x100b24838).
//
// Quirks kept (each one is asserted by scripts/tests/sc4_nation_test.cpp):
//  * QueryInterface does not answer for the serializable view 0xe4fda3d4 (0x1002888ae): the +0x10 subobject is
//    reachable only through the class itself.
//  * Release shuts the object down when the count drops below two and then deletes it through the deleting
//    destructor (0x100288928); the destructor itself does not call Shutdown.
//  * Shutdown reads mRegions[mActiveRegionIndex] without checking that the index is valid (0x1002897af), so an
//    instance that never found an active region indexes mRegions[-1]. The port guards the read and leaves the
//    vector alone in that case.
//  * CopyNewUserRegions copies the data directory's "Regions" folder into the user region directory *before*
//    checking the language folder, and only returns LoadRegions() — a failure of either copy is ignored
//    (0x100289616).
//  * ReadSimulatorTunableValues runs the tunable-vector read once per curve, inside the loop (0x100289318).
//  * GetEconomyModifier falls back to the curve with key 0 when the requested demand type is missing, and reads
//    through the map's end node (an out-of-bounds pointer) when the map is empty (0x10028b19d). The port returns
//    the tunable sum alone in that case.
//  * SetActiveRegion and DeleteRegion index the region vector with the value they are given without a bounds
//    check (0x100289aa2 / 0x100289d73), and SetActiveRegion is a no-op when the index is already active.
//  * DeleteRegion first makes the region it is about to delete active (0x100289d6a), then deletes it, then picks
//    the previous or the next region as the new active one.
//  * ResetTutorialCities returns without doing anything once a city has been established (SL::spCity non-null,
//    0x100289fd7) or when the application is not up yet, and it recreates the region screen only when it was the
//    current screen for the whole run (the error path skips CreateRegionScreen).
//  * StaticInit keeps its "already initialised" flag even when there is no application, so a later call does not
//    fill the directory strings (0x1002889f7).
//
// Everything the original reached through SL:: / through a class that is not recovered goes through
// INationHost (one method per original call, the slot or address in the comment).
#pragma once
#include <cstdint>
#include <cstddef>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"
#include "sc4/Region.h"
#include "sc4/cSC4ResponseCurve.h"

class cISC4RegionalCity;
class cISC4PropertyHolder;   // sc4::cISCPropertyHolder, see sc4/SCProperty.h
class cIGZDate;

constexpr std::uint32_t kGZCLSID_cSC4Nation = 0xc9dc0477;    // GetGZCLSID @ 0x10028b52a
constexpr std::uint32_t kGZIID_cISC4Nation = 0x89d2c366;     // the iid QueryInterface answers for the object itself

namespace sc4 {
class cISCPropertyHolder;
}

// The nation's economy tuning: one exemplar of type 0x6534284a whose properties hold the demand-type list, one
// response curve per demand type and the per-difficulty tunable values (ReadSimulatorTunableValues @ 0x1002891b2).
constexpr std::uint32_t kNationTuningType = 0x6534284a;
constexpr std::uint32_t kNationTuningGroup = 0xe7e2c2db;
constexpr std::uint32_t kNationTuningInstance = 0x4a1f6115;
constexpr std::uint32_t kNationTuningDemandTypes = 0x4a1f629f;    // u32 array: the curve keys, in curve-property order
constexpr std::uint32_t kNationTuningCurveBase = 0x4a1f62a0;      // + index: interleaved x,y floats of curve i
constexpr std::uint32_t kNationTuningTunables = 0x4a1f6300;       // float array indexed by the city difficulty level

// The region name the active-region preference stores: 0x40 bytes inside the app-owned part of the preference
// blob (SC4AppPreferences + 0x4a4 is 0x9ac bytes long, so +0xebc is one of the fields cSC4App's constructor sets;
// AppPreferences.h does not name it). GetIndexOfCurrentRegionSettingInPreferences reads it whole.
constexpr std::size_t kRegionNamePreferenceOffset = 0xebc;
constexpr std::size_t kRegionNamePreferenceLength = 0x40;

// The messages the nation subscribes to (Init @ 0x100288ef2 adds them, Shutdown @ 0x100289778 removes them):
constexpr std::uint32_t kNationMessage1 = 0x26d31ec1;
constexpr std::uint32_t kNationMessage2 = 0x26d31ec2;
constexpr std::uint32_t kNationResetTutorialCitiesMessage = 0xac3b3fa6;   // DoMessage @ 0x100289a2e

// "Downloads" — the sub-directory of the region directory that LoadRegions skips (kRegionDownloadDirName
// @ 0x100a1be40); StaticInit appends it to the region directory to build the download directory.
constexpr const char* kRegionDownloadDirName = "Downloads";

// The serialized layout of the nation records: a u16 version, nothing else (Write @ 0x10028988c /
// Read @ 0x1002898e4; the regions are rebuilt by Init, not read back).
constexpr std::uint16_t kNationStreamVersion = 1;

// The screen whose presence makes ResetTutorialCities destroy and recreate it: the region screen's window id
// (cSC4WinRegionScreen::Init sets it through vslot 0x200, see WinRegionScreen.h).
constexpr std::uint32_t kRegionScreenWindowId = 0xea659793;

// One row of the tutorial registry's per-region list (cSC4TutorialRegistry::cItem, 0x100643228 hands them out):
// the tutorial number at +0x10, the rest is the registry's own data.
struct NationTutorialItem {
    std::uint32_t unknown[4] = {0, 0, 0, 0};   // +0x00
    std::int32_t guid = -1;                    // +0x10 the tutorial number GetItems' callers read
};

// Everything cSC4Nation reaches outside itself. Each method mirrors one original call; the ones whose class is
// not recovered (the tutorial registry, the application window's screens, the path-based save helpers) are seams
// to be hooked up there.
class INationHost {
public:
    virtual ~INationHost() = default;

    // --- the application (SL::spSC4, cISC4App iid 0x26ce01c0) ---------------------------------------------
    virtual bool ApplicationPresent() = 0;                              // SL::spSC4 != null
    virtual void RegionsDirectory(cIGZString& out) = 0;                 // SL::spSC4 vslot 0x1d0 ([58] GetRegionsDirectory)
    virtual std::uint8_t* Preferences() = 0;                            // SL::spSC4 vslot 0x130 ([38] GetPreferences)
    virtual bool IsRunFirstTimeAfterInstall() = 0;                      // SL::spSC4 vslot 0x180 ([48])
    // SL::spFeatureManager vslot 0x38 (Init @ 0x100288bbe); ResetTutorialCities reaches the same value through
    // cSC4App's own vslot 0x1a0 (0x10028a053). It is the index GetTutorialRegionDefaultDirName takes.
    virtual int TutorialLanguageIndex() = 0;

    // --- the tutorial registry (cSC4TutorialRegistry, not recovered) --------------------------------------
    virtual void TutorialRegionDirectoryNames(std::list<cRZString>& out) = 0;             // @ 0x1006431b8
    virtual cRZString TutorialRegionDefaultDirName(int languageIndex) = 0;                // @ 0x1006437ac
    virtual void TutorialItems(std::list<NationTutorialItem>& out, cISC4Region* region) = 0;   // @ 0x100643228
    virtual void SetTutorialActive(std::int32_t guid, bool active) = 0;                   // @ 0x1006433ee

    // --- the application window (cSC4App +0x88, cWinSC4App) -----------------------------------------------
    // currentScreen_ (+0x178) vslot 0x1f8 == kRegionScreenWindowId.
    virtual bool IsRegionScreenCurrent() = 0;
    virtual void DestroyCurrentScreen() = 0;                            // cWinSC4App::DestroyCurrentScreen @ 0x1006ccaa0
    virtual void CreateRegionScreen() = 0;                              // cWinSC4App::CreateRegionScreen @ 0x1006cc934

    // --- the simulator and the city (SL::spSimulator / SL::spCity) -----------------------------------------
    virtual void SimStartDate(cIGZDate& out) = 0;                       // simulator vslot 0x28 (1/1/2000)
    virtual cIGZDate* SimDate() = 0;                                    // simulator vslot 0x30 (borrowed)
    virtual bool CityPresent() = 0;                                     // SL::spCity != null
    virtual int CityDifficultyLevel() = 0;                              // city vslot 0xc0

    // --- resources / messages -----------------------------------------------------------------------------
    // SL::spRM vslot 0x18 with iid 0xa52160f5; one reference, ReleaseExemplar hands it back.
    virtual sc4::cISCPropertyHolder* LoadExemplar(const cGZPersistResourceKey& key) = 0;
    virtual void ReleaseExemplar(sc4::cISCPropertyHolder* holder) = 0;
    virtual void AddNotification(cIGZMessageTarget2* target, std::uint32_t message) = 0;      // SL::spMS2 vslot 0x28
    virtual void RemoveNotification(cIGZMessageTarget2* target, std::uint32_t message) = 0;   // SL::spMS2 vslot 0x30

    // --- files and the city records -----------------------------------------------------------------------
    virtual void DataDirectory(cIGZString& out) = 0;                    // SL::DataDirectory @ 0x100040506
    // RZ::LM() vslot 0x78 (GetLanguageDirectoryName) with language 0.
    virtual bool LanguageDirectoryName(cIGZString& out, std::uint32_t language) = 0;
    // SC4ReadBinaryRecord(cGZPersistResourceKey const&, cIGZString const&) @ 0x1001e93c7: -1 on failure.
    virtual int ReadBinaryRecord(const cGZPersistResourceKey& key, const cIGZString& path) = 0;
    // SC4ReadRegionalCity(cIGZString const&, cISC4RegionalCity*&) @ 0x1001e95c3; the city comes back with one reference.
    virtual bool ReadRegionalCity(const cIGZString& path, cISC4RegionalCity*& out) = 0;

    // --- cISC4RegionalCity virtuals the partial interface does not declare (original vslot) -----------------
    virtual void CityPosition(cISC4RegionalCity* city, int& x, int& z) = 0;                    // 0x28
    virtual void SetCitySaveFilePath(cISC4RegionalCity* city, const cIGZString& path) = 0;     // 0x100
    virtual std::int32_t CityTutorialGUID(cISC4RegionalCity* city) = 0;                        // 0x208
};
INationHost* NationHost();
void SetNationHost(INationHost* host);

// cISC4Nation — the interface the nation exposes (vtable 0x100a8ff10; slots 3..18, the destructors and the
// secondary-base views follow). The slot offsets in the comments are the original's.
class cISC4Nation : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                    // 0x18
    virtual bool Shutdown() = 0;                                                // 0x20
    virtual cRZString* GetRegionDirectory() = 0;                                // 0x28
    virtual std::uint32_t GetRegionCount() const = 0;                            // 0x30
    virtual int GetActiveRegionIndex() const = 0;                               // 0x38
    virtual cISC4Region* GetActiveRegion() const = 0;                           // 0x40
    virtual void SetActiveRegion(std::uint32_t index) = 0;                      // 0x48
    virtual bool GetIndexOfCurrentRegionSettingInPreferences(int& index) = 0;    // 0x50
    virtual cISC4Region* GetRegion(std::uint32_t index) = 0;                    // 0x58
    virtual void DeleteRegion(std::uint32_t index) = 0;                         // 0x60
    virtual std::uint32_t AddNewRegion(const cIGZString& name) = 0;             // 0x68
    virtual void ResetTutorialCities() = 0;                                     // 0x70
    virtual cRZString* GetRegionDownloadDirectory() = 0;                        // 0x78
    // The original's parameter is cISC4DemandSimulator::DemandType (an int-sized enum the port's demand
    // simulator interface does not declare); the value is the map key, so it is passed through unchanged.
    virtual float GetEconomyModifier(std::uint32_t demandType) = 0;             // 0x80
};

class cSC4Nation : public cISC4Nation, public cIGZMessageTarget2, public cIGZSerializable {
public:
    cSC4Nation();
    virtual ~cSC4Nation();                                      // 0x88 (complete) / 0x90 (deleting)

    // cIGZUnknown
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;

    // cISC4Nation
    bool Init() override;
    bool Shutdown() override;
    cRZString* GetRegionDirectory() override;
    std::uint32_t GetRegionCount() const override;
    int GetActiveRegionIndex() const override;
    cISC4Region* GetActiveRegion() const override;
    void SetActiveRegion(std::uint32_t index) override;
    bool GetIndexOfCurrentRegionSettingInPreferences(int& index) override;
    cISC4Region* GetRegion(std::uint32_t index) override;
    void DeleteRegion(std::uint32_t index) override;
    std::uint32_t AddNewRegion(const cIGZString& name) override;
    void ResetTutorialCities() override;
    cRZString* GetRegionDownloadDirectory() override;
    float GetEconomyModifier(std::uint32_t demandType) override;

    // cIGZSerializable / cIGZMessageTarget2 (the primary group's slots 0x98..0xb0)
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override;
    bool DoMessage(cIGZMessage2* message) override;

    // --- helpers (the original keeps them private; exposed for the test) -----------------------------------
    bool StaticInit();                                          // 0x1002889e0
    void ReadSimulatorTunableValues();                          // 0x1002891b2
    bool LoadRegions();                                         // 0x100289362
    bool CopyNewUserRegions();                                  // 0x100289582

    // The static directory strings (0x100b24840 / 0x100b24868) and the one-shot flag (0x100b24838).
    static inline cRZString msRegionDirectory;
    static inline cRZString msRegionDownloadDirectory;
    static inline bool mbStaticInitialized = false;

    // --- port additions for the test: field offsets, refcount and the curve count --------------------------
    std::size_t regionsFieldOffset() const { return OffsetOf(regions_); }
    std::size_t activeRegionIndexFieldOffset() const { return OffsetOf(activeRegionIndex_); }
    std::size_t economyCurvesFieldOffset() const { return OffsetOf(economyCurves_); }
    std::size_t tunablesFieldOffset() const { return OffsetOf(tunables_); }
    std::size_t initialisedFieldOffset() const { return OffsetOf(initialised_); }
    std::size_t refCountFieldOffset() const { return OffsetOf(refCount_); }
    std::uint32_t RefCount() const { return refCount_; }
    const std::vector<float>& Tunables() const { return tunables_; }
    cSC4ResponseCurve* EconomyCurve(std::uint32_t demandType) const;

private:
    template <class T>
    std::size_t OffsetOf(const T& member) const {
        return static_cast<std::size_t>(reinterpret_cast<const char*>(&member) - reinterpret_cast<const char*>(this));
    }

    bool initialised_ = false;                    // +0x18
    std::uint32_t refCount_ = 0;                  // +0x1c
    std::vector<cRZAutoRefCount<cISC4Region>> regions_;   // +0x20
    std::int32_t activeRegionIndex_ = -1;         // +0x38
    std::map<std::uint32_t, cSC4ResponseCurve*> economyCurves_;   // +0x40
    std::vector<float> tunables_;                 // +0x58
};

// CopyRegionFiles(destination, source) @ 0x10028b224: copies every file under `source` into `destination`,
// recreating the sub-directories and overwriting (the attribute set with cRZFile::SetAttribute(path, 2, true)).
void CopyRegionFiles(cRZString& destination, cRZString& source);
