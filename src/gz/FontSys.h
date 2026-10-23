// cGZFontSys — the font system service (class vtable 0x100a8fb60, secondary cIGZSystemService vtable 0x100a8fc78;
// 50 functions at 0x100282440..0x10028732a). It scans font files, keeps a registry of installed families and of the
// cGZFont objects handed out, resolves substitutions ("Arial,Helvetica") and acquires fonts by name/type, size and style.
//
// Object layout of the original: +0x10 initialised, +0x14 refcount, +0x18 service id (default 0x20673960), +0x1c default
// antialiasing, +0x20/+0x24 default unantialiased size range, +0x28 default code page, +0x30 default renderer,
// +0x38 set of lowercased installed family names, +0x50 map<int, generic font name>, +0x68 map<file path, FontFixedInfo>,
// +0x80 map<name, substitutes>, +0x98 file filters ("*.tt?" "*.pfb" "*.mxf" "*.otf"), +0xb0 map<name, MinMax>.
//
// Quirks kept:
//  FONTSYS-1  Release() with a count below 2 shuts the service down and deletes it (0x10028340c); a freshly built
//             service has count 0, so a single Release of an un-AddRef'd object destroys it.
//  FONTSYS-2  AddAllFontsFromDirectory counts a file whose name contains ".PFB" (upper case) twice (0x1002844f2).
//  FONTSYS-3  RemoveFontByFile is a stub returning false; RemoveFontByName returns false even when it removed something.
//  FONTSYS-4  SetDefaultUnantialiasedSizeRange never updates an existing per-font entry (0x100285c58).
//  FONTSYS-5  Per-font size range keys are NOT lower-cased, unlike every other name table (0x100285c58, 0x1002867d8).
//  FONTSYS-6  The default size range turns antialiasing mode 1 on INSIDE [min, max]; outside it the default is used,
//             unless the font's own range contains the size (0x1002867d8).
//  FONTSYS-7  EnumerateFontSubstitutions loops to `count` in the original and reads past the substitute list when
//             count is larger than it; this version stops at the list end (deviation).
//  FONTSYS-8  Reusing a font in FontAcquire(name,...) returns the first entry whose size AND style flags match exactly;
//             the style flags are those passed to the acquire call, not the font's current ones.
//  FONTSYS-10 FontAcquire(path,...) looks the per-font size range up with the font's name BEFORE Init (0x1002867d8), which for
//             a freshly created font is empty; ranges registered under real family names are therefore never found.
//  FONTSYS-9  FontRelease only drops the registry entry when the font's Release() reports exactly one holder left.
#pragma once
#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gz/Font.h"
#include "gz/FontServices.h"
#include "gz/cIGZString.h"
#include "gz/cIGZSystemService.h"
#include "rz/cRZString.h"

constexpr std::uint32_t kGZIID_cIGZFontSys = 0x00673960;
constexpr std::uint32_t kGZCLSID_cGZFont = 0x1aa404ac;
constexpr std::uint32_t kGZFontSysDefaultServiceID = 0x20673960;

class cIGZFontSys : public cIGZUnknown {
public:
    struct FontInfo {          // 0x88 bytes
        char name[0x80];
        std::int32_t style;    // +0x80
        std::int32_t type;     // +0x84
    };
    virtual bool Init() = 0;                                                                   // [3]
    virtual bool Shutdown() = 0;                                                               // [4]
    virtual bool SetDefaultCodePage(int codePage) = 0;                                         // [5]
    virtual void SetDefaultFontRenderer(cIGZFontRenderer* renderer) = 0;                       // [6]
    virtual void SetDefaultFontAntialiasing(int mode) = 0;                                     // [7]
    virtual void SetDefaultUnantialiasedSizeRange(int min, int max, const cIGZString* name) = 0;   // [8]
    virtual cIGZFont* FontAcquire(const cIGZString& names, int type, std::uint32_t size, int style, bool reuse) = 0;   // [9]
    virtual cIGZFont* FontAcquire(const cIGZString& path, std::uint32_t size, int style, bool reuse) = 0;              // [10]
    virtual int FontRelease(cIGZFont* font) = 0;                                               // [11]
    virtual bool AddFont(const cIGZString& path) = 0;                                          // [12]
    virtual int AddAllFontsFromDirectory(const cIGZString& directory) = 0;                     // [13]
    virtual bool AddFont(cIGZFont* font) = 0;                                                  // [14]
    virtual bool RemoveFontByFile(const cIGZString& path) = 0;                                 // [15]
    virtual bool RemoveFontByName(const cIGZString& name) = 0;                                 // [16]
    virtual std::uint32_t EnumerateFontInfo(FontInfo* out, std::uint32_t count) = 0;           // [17]
    virtual std::uint32_t EnumerateManagedFonts(cIGZFont** out, std::uint32_t count) = 0;      // [18]
    virtual bool AddSubstitution(const cIGZString& from, const cIGZString& to) = 0;            // [19]
    virtual std::uint32_t EnumerateFontSubstitutions(const cIGZString& name, cIGZString** out, std::uint32_t count) = 0;   // [20]
    virtual bool IsFontAvailable(const cIGZString& name, std::uint32_t size, int style) = 0;   // [21]
    virtual bool IsSubstitutionAvailable(const cIGZString& name, cIGZString& substitute) = 0;  // [22]
    virtual cIGZFont* FontAcquire(int type, std::uint32_t size, int style, std::uint32_t color, bool reuse) = 0;   // [23]
};

// Everything the service takes from the engine.
class IFontSysHost : public IFontHost {
public:
    // cRZFile(path).Open(1, 2, 1) + Read(buffer, count) (GetFontSourceTypeFromFileName); false when either fails.
    virtual bool ReadFileHeader(const cIGZString& path, void* buffer, std::uint32_t count) = 0;
    // cRZDirectory(dir, false, false, false) + SetNewDirectoryEntryFilter(filter, false) + ReadDirectoryFileEntriesIntoStringList.
    virtual void ListDirectoryFiles(const cIGZString& directory, const cIGZString& filter, std::list<cRZString>& out) = 0;
    // GZCOM()->GetClassObject(0x1aa404ac, 0x10673960): an AddRef'd font, or null when the class is not registered.
    virtual cIGZFont* CreateFontObject() = 0;
    // GetCurrentFrameworkCodePage (0x10001ad26)
    virtual int GetCurrentFrameworkCodePage() = 0;
    // The original logs a font that is still held by somebody else when the service shuts down.
    virtual void LogLiveFontAtShutdown(const cIGZString& path) { (void)path; }
};

class cGZFontSys : public cIGZFontSys, public cIGZSystemService {
public:
    struct FontEntry {          // list node payload: font at node+0x10, style flags at node+0x18
        cIGZFont* font = nullptr;
        std::uint32_t flags = 0;
    };
    struct FontFixedInfo {      // map value
        std::list<FontEntry> entries;
        cRZString name;                 // family name (node+0x60)
        std::uint32_t style = 0;        // node+0x88
        std::uint32_t type = 0;         // node+0x8c (1 proportional, 2 fixed)
        std::uint32_t sourceType = 0;   // node+0x90 (0 unknown, 1 TrueType, 2 Type 1, 4 collection/OpenType)
    };
    struct MinMax { std::uint32_t min = 0, max = 0; };
    struct StringLess {
        bool operator()(const cRZString& a, const cRZString& b) const { return std::strcmp(a.ToChar(), b.ToChar()) < 0; }
    };

    explicit cGZFontSys(IFontSysHost* host);             // 0x100282440
    virtual ~cGZFontSys();                               // 0x100282bf6 / 0x100282cc8 / 0x100282cd2

    // cIGZUnknown (shared by both interfaces; the cIGZSystemService copies are the thunks at 0x1002833e2 ...)
    bool QueryInterface(std::uint32_t iid, void** out) override;     // 0x1002833ae
    std::uint32_t AddRef() override;                                 // 0x1002833f0
    std::uint32_t Release() override;                                // 0x10028340c

    bool Init() override;                                            // 0x100282d00
    bool Shutdown() override;                                        // 0x1002831d8
    bool SetDefaultCodePage(int codePage) override;                  // 0x100285c42
    void SetDefaultFontRenderer(cIGZFontRenderer* renderer) override;    // 0x100285fd0
    void SetDefaultFontAntialiasing(int mode) override;              // 0x100285c4e
    void SetDefaultUnantialiasedSizeRange(int min, int max, const cIGZString* name) override;   // 0x100285c58
    cIGZFont* FontAcquire(const cIGZString& names, int type, std::uint32_t size, int style, bool reuse) override;   // 0x100286004
    cIGZFont* FontAcquire(const cIGZString& path, std::uint32_t size, int style, bool reuse) override;              // 0x1002867d8
    cIGZFont* FontAcquire(int type, std::uint32_t size, int style, std::uint32_t color, bool reuse) override;       // 0x100286740
    int FontRelease(cIGZFont* font) override;                        // 0x1002870f2
    bool AddFont(const cIGZString& path) override;                   // 0x100283494
    int AddAllFontsFromDirectory(const cIGZString& directory) override;   // 0x1002844f2
    bool AddFont(cIGZFont* font) override;                           // 0x100284884
    bool RemoveFontByFile(const cIGZString& path) override;          // 0x100284e20
    bool RemoveFontByName(const cIGZString& name) override;          // 0x100284e28
    std::uint32_t EnumerateFontInfo(FontInfo* out, std::uint32_t count) override;       // 0x100285010
    std::uint32_t EnumerateManagedFonts(cIGZFont** out, std::uint32_t count) override;  // 0x100285122
    bool AddSubstitution(const cIGZString& from, const cIGZString& to) override;        // 0x1002851a4
    std::uint32_t EnumerateFontSubstitutions(const cIGZString& name, cIGZString** out, std::uint32_t count) override;   // 0x100285670
    bool IsFontAvailable(const cIGZString& name, std::uint32_t size, int style) override;   // 0x1002857f8
    bool IsSubstitutionAvailable(const cIGZString& name, cIGZString& substitute) override;   // 0x100285aaa

    // cIGZSystemService
    std::uint32_t GetServiceID() const override { return serviceId_; }     // 0x1002872b6
    void SetServiceID(std::uint32_t id) override { serviceId_ = id; }      // 0x1002872c0
    std::int32_t GetServicePriority() const override { return 900000; }    // 0x1002872ca
    bool IsServiceRunning() const override { return true; }                // 0x1002872d6
    void SetServiceRunning(bool) override {}                               // 0x1002872de
    bool OnTick(std::uint32_t) override { return true; }                   // 0x1002872ec
    bool OnIdle(std::uint32_t) override { return false; }                  // 0x1002872e4

    bool FontSetName(int genericId, const cIGZString& name);               // 0x100282e9e
    std::uint32_t GetFontSourceTypeFromFileName(const cIGZString& path);   // 0x100284140

    // Inspection used by tests.
    bool IsInitialized() const { return initialized_; }
    const std::map<int, cRZString>& GenericNames() const { return genericNames_; }
    const std::set<cRZString, StringLess>& InstalledNames() const { return installed_; }
    const std::map<cRZString, FontFixedInfo, StringLess>& Fonts() const { return fonts_; }
    const std::vector<cRZString>& FileFilters() const { return filters_; }
    int DefaultAntialiasing() const { return defaultAntialiasing_; }
    int DefaultCodePage() const { return defaultCodePage_; }

private:
    using FixedMap = std::map<cRZString, FontFixedInfo, StringLess>;
    static cRZString Lower(const cIGZString& s);
    void RegisterFont(cIGZFont* font, const cRZString& path, std::uint32_t flags, bool reuseRecord);

    IFontSysHost* host_;
    bool initialized_ = false;                       // +0x10
    std::uint32_t refCount_ = 0;                     // +0x14
    std::uint32_t serviceId_ = kGZFontSysDefaultServiceID;   // +0x18
    int defaultAntialiasing_ = 0;                    // +0x1c
    std::uint32_t defaultMin_ = 0, defaultMax_ = 0;  // +0x20 / +0x24
    int defaultCodePage_ = 0;                        // +0x28
    cIGZFontRenderer* defaultRenderer_ = nullptr;    // +0x30
    std::set<cRZString, StringLess> installed_;      // +0x38
    std::map<int, cRZString> genericNames_;          // +0x50
    FixedMap fonts_;                                 // +0x68
    std::map<cRZString, std::vector<cRZString>, StringLess> substitutions_;   // +0x80
    std::vector<cRZString> filters_;                 // +0x98
    std::map<cRZString, MinMax, StringLess> ranges_; // +0xb0
};
