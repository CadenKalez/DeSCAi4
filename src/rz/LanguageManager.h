// The language service. cRZLanguageManager (service id 0x441e5070, priority -3000000, iid 0x54b7d5; 0x100066 44c..
// 0x1000686ff) knows the 44 game languages (ids 1..0x2c) and 56 countries (ids 1..0x38), maps them to names, ISO
// codes, directory names and defaults, keeps the set of languages that may be switched to, and owns the language
// utility of the current language. cRZLanguageUtility (iid 0x1054b7d5; 0x1000665be..0x10006e4c2) formats dates, times,
// numbers and money and answers script questions for one language / country pair.
//
// Quirks kept from the binary:
//  * GetLanguageLocalName fills the name and then, because of an inverted test, answers "Untranslated" / false for every
//    language that has one; only an empty result (languages without a local name) counts as success.
//  * GetNextAvailableLanguage looks one past the cursor, and GetAllAvailableLanguagesIntoStringList steps the cursor once
//    more itself, so it lists every second language only (UK English, German, ...).
//  * GetCurrencySymbol uses the table for the euro and Czech crown with stray commas inside the multi-byte sequences;
//    MakeNumberString gives an empty string for negative numbers in languages after Vietnamese's neighbours (> 0x14).
//  * The embedded utility returned by GetLanguageUtility is not reference counted for the caller; releasing it would
//    delete a sub-object.
#pragma once
#include <cstdint>
#include <list>
#include <set>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"
#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZIID_cIGZLanguageManager = 0x0054b7d5;
constexpr std::uint32_t kGZIID_cIGZLanguageUtility = 0x1054b7d5;
constexpr std::uint32_t kGZSID_cRZLanguageManager = 0x441e5070;
constexpr std::int32_t kRZLanguageManagerPriority = -3000000;
constexpr std::uint32_t kLanguageCount = 0x2c, kCountryCount = 0x38;
constexpr std::uint32_t kDefaultLanguage = 1, kDefaultCountry = 0x36;   // English, United States

class cIGZLanguageUtility : public cIGZUnknown {
public:
    // How MakeDateString lays a date out.
    enum DateFormat : std::uint32_t {
        kDateNumeric = 0,            // day/month/year (month/day/year in the default layout)
        kDateNumericMonthYear = 1,
        kDateLong = 2,               // "12 March, 2003"
        kDateLongMonthYear = 3,
        kDateAbbreviated = 4,
        kDateAbbreviatedMonthYear = 5,
    };

    virtual bool Init() = 0;                                                                    // [3]
    virtual bool Shutdown() = 0;                                                                // [4]
    virtual bool GetDayName(std::uint8_t day, cIGZString& out) = 0;                             // [5] 1 = Monday
    virtual bool GetAbbrDayName(std::uint8_t day, cIGZString& out) = 0;                         // [6]
    virtual bool GetMonthName(std::uint8_t month, cIGZString& out) = 0;                         // [7]
    virtual bool GetAbbrMonthName(std::uint8_t month, cIGZString& out) = 0;                     // [8]
    virtual bool MakeTimeString(int hour, int minute, int second, cIGZString& out) = 0;         // [9]
    virtual bool MakeDateString(std::uint32_t month, std::uint32_t day, std::uint32_t year, cIGZString& out,
                                DateFormat format) = 0;                                          // [10]
    virtual bool GetCurrencySymbol(cIGZString& out) = 0;                                        // [11]
    virtual bool DoesCurrencySymbolPrecedeAmount() = 0;                                         // [12]
    virtual bool IsSpaceBetweenCurrencySymbolAndAmount() = 0;                                   // [13]
    virtual bool GetThousandSeparator(cIGZString& out) = 0;                                     // [14]
    virtual bool GetDecimalSeparator(cIGZString& out) = 0;                                      // [15]
    virtual bool MakeMoneyString(std::int64_t amount, cIGZString& out, const cIGZString* symbol) = 0;   // [16]
    virtual bool MakeFormattedMoneyString(double amount, cIGZString& out, const cIGZString& format,
                                          const cIGZString* symbol) = 0;                         // [17]
    virtual bool MakeNumberString(std::int64_t value, cIGZString& out) = 0;                     // [18]
    virtual bool MakeFormattedNumberString(double value, cIGZString& out, const cIGZString& format) = 0;   // [19]
    virtual void ConvertToLowerCase(const cIGZString& in, cIGZString& out) = 0;                 // [20]
    virtual void ConvertToUpperCase(const cIGZString& in, cIGZString& out) = 0;                 // [21]
    virtual int CompareStrings(const cIGZString& a, const cIGZString& b, bool caseSensitive) = 0;   // [22]
    virtual std::int64_t GetCharacterSetFromLanguage() = 0;                                     // [23]
    virtual bool DoesLanguageUseCharacterSet(int codePage) = 0;                                 // [24]
    virtual std::int64_t GetLanguageRoadDrivingSide() = 0;                                      // [25] 0 = left, 1 = right
    virtual bool GetMeasurementSystem() = 0;                                                    // [26] true = metric
    virtual bool AreFirstAndLastNamesReversed() = 0;                                            // [27]
    virtual bool DoesLanguageUseInputMethodEditing() = 0;                                       // [28]
    virtual bool DoesLanguageUseMultiByteCharacters() = 0;                                      // [29]
};

class cRZLanguageUtility : public cIGZLanguageUtility {
public:
    cRZLanguageUtility();                                  // 1 reference-less, English / United States, no space glyph set up
    explicit cRZLanguageUtility(std::uint32_t language);   // GetNewLanguageUtility's object: one reference
    virtual ~cRZLanguageUtility() = default;               // [30] [31] @ 0x10006665a / 0x100068748

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10006e484
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x10006e4a6
    std::uint32_t Release() override;                              // @ 0x10006e4b4
    bool Init() override;                                          // @ 0x1000665be
    bool Shutdown() override { return true; }                      // @ 0x1000666fe
    bool GetDayName(std::uint8_t day, cIGZString& out) override;               // @ 0x100068ace
    bool GetAbbrDayName(std::uint8_t day, cIGZString& out) override;           // @ 0x1000694fc
    bool GetMonthName(std::uint8_t month, cIGZString& out) override;           // @ 0x100069f70
    bool GetAbbrMonthName(std::uint8_t month, cIGZString& out) override;       // @ 0x10006aec4
    bool MakeTimeString(int hour, int minute, int second, cIGZString& out) override;   // @ 0x10006d01e
    bool MakeDateString(std::uint32_t month, std::uint32_t day, std::uint32_t year, cIGZString& out,
                        DateFormat format) override;                           // @ 0x10006d398
    bool GetCurrencySymbol(cIGZString& out) override;                          // @ 0x10006ba94
    bool DoesCurrencySymbolPrecedeAmount() override;                           // @ 0x10006bc64
    bool IsSpaceBetweenCurrencySymbolAndAmount() override;                     // @ 0x10006bc88
    bool GetThousandSeparator(cIGZString& out) override;                       // @ 0x10006bcac
    bool GetDecimalSeparator(cIGZString& out) override;                        // @ 0x10006bd4a
    bool MakeMoneyString(std::int64_t amount, cIGZString& out, const cIGZString* symbol) override;   // @ 0x10006bdac
    bool MakeFormattedMoneyString(double amount, cIGZString& out, const cIGZString& format,
                                  const cIGZString* symbol) override;          // @ 0x10006c50a
    bool MakeNumberString(std::int64_t value, cIGZString& out) override;       // @ 0x10006cb56
    bool MakeFormattedNumberString(double value, cIGZString& out, const cIGZString& format) override;   // @ 0x10006cfda
    void ConvertToLowerCase(const cIGZString& in, cIGZString& out) override;   // @ 0x10006e338
    void ConvertToUpperCase(const cIGZString& in, cIGZString& out) override;   // @ 0x10006e3d2
    int CompareStrings(const cIGZString& a, const cIGZString& b, bool caseSensitive) override;   // @ 0x10006751a
    std::int64_t GetCharacterSetFromLanguage() override;                       // @ 0x100068778
    bool DoesLanguageUseCharacterSet(int codePage) override;                   // @ 0x100068946
    std::int64_t GetLanguageRoadDrivingSide() override;                        // @ 0x10006e46c
    bool GetMeasurementSystem() override;                                      // @ 0x100068a3c
    bool AreFirstAndLastNamesReversed() override;                              // @ 0x100068a4c
    bool DoesLanguageUseInputMethodEditing() override;                         // @ 0x100068a68
    bool DoesLanguageUseMultiByteCharacters() override;                        // @ 0x100068a84
    virtual void SetLanguage(std::uint32_t language) { language_ = language; }   // [32] @ 0x100066646
    virtual void SetCountry(std::uint32_t country) { country_ = country; }       // [33] @ 0x100066650

    // CompareStrings does not look at the object, and the manager's name look-ups call it that way.
    static int CompareStringsStatic(const cIGZString& a, const cIGZString& b, bool caseSensitive);

    std::uint32_t Language() const { return language_; }
    std::uint32_t Country() const { return country_; }
    // The byte at +0x14: when clear every currency symbol is "$"; nothing recovered sets it.
    void SetLocalCurrencySymbols(bool on) { localCurrencySymbols_ = on; }

private:
    std::uint32_t refCount_ = 0;                  // +8
    std::uint32_t language_ = kDefaultLanguage;   // +0xc
    std::uint32_t country_ = kDefaultCountry;     // +0x10
    bool localCurrencySymbols_ = false;           // +0x14
    cRZString space_{"\xc2\xa0"};                 // +0x18: the no-break space put between a currency symbol and the amount
};

class cIGZLanguageManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                    // [3]
    virtual bool Shutdown() = 0;                                                                // [4]
    virtual bool AddAvailableLanguage(std::uint32_t language) = 0;                              // [5]
    virtual bool RemoveAvailableLanguage(std::uint32_t language) = 0;                           // [6]
    virtual std::uint32_t GetCurrentLanguage() = 0;                                             // [7]
    virtual bool SetCurrentLanguage(std::uint32_t language) = 0;                                // [8]
    virtual bool CanSwitchToLanguage(std::uint32_t language) = 0;                               // [9]
    virtual std::uint32_t GetCurrentSystemLanguage() = 0;                                       // [10]
    virtual std::uint32_t GetSystemLanguageFromLanguage(std::uint32_t language) = 0;            // [11]
    virtual std::uint32_t GetLanguageFromSystemLanguage(std::uint32_t systemLanguage) = 0;      // [12]
    virtual bool GetLanguageRuntimeLibraryName(cIGZString& out, std::uint32_t language) = 0;    // [13]
    virtual bool GetNextAvailableLanguage(std::uint32_t& language) = 0;                         // [14]
    virtual bool GetLanguageDirectoryName(cIGZString& out, std::uint32_t language) = 0;         // [15]
    virtual bool GetLanguageEnglishName(cIGZString& out, std::uint32_t language) = 0;           // [16]
    virtual bool GetLanguageLocalName(cIGZString& out, std::uint32_t language) = 0;             // [17]
    virtual bool GetLanguageIDFromLanguageEnglishName(cIGZString& name, std::uint32_t& language) = 0;   // [18]
    virtual bool GetLanguageIDFromLanguageLocalName(cIGZString& name, std::uint32_t& language) = 0;     // [19]
    virtual bool GetLanguageEnglishNameFromLanguageID(cIGZString& out, std::uint32_t language) = 0;     // [20]
    virtual bool GetLanguageLocalNameFromLanguageID(cIGZString& out, std::uint32_t language) = 0;       // [21]
    virtual std::uint32_t GetLanguageIDAlias(std::uint32_t language) = 0;                       // [22]
    virtual bool GetISO639LanguageCodeFromLanguageID(cIGZString& out, std::uint32_t language) = 0;      // [23]
    virtual bool GetISO639LanguageIDFromLanguageCode(cIGZString& code, std::uint32_t& language) = 0;    // [24]
    virtual bool GetISO3166CountryCodeFromCountryID(cIGZString& out, std::uint32_t country) = 0;        // [25]
    virtual bool GetISO3166CountryIDFromCountryCode(cIGZString& code, std::uint32_t& country) = 0;      // [26]
    virtual std::uint32_t GetCurrentCountry() = 0;                                              // [27]
    virtual bool SetCurrentCountry(std::uint32_t country) = 0;                                  // [28]
    virtual std::uint32_t GetPrimaryLanguageForCountry(std::uint32_t country) = 0;              // [29]
    virtual std::uint32_t GetDefaultCountryForLanguage(std::uint32_t language) = 0;             // [30]
    virtual std::uint32_t GetCurrentSystemCountry() = 0;                                        // [31]
    virtual bool CanSwitchToCountry(int country) = 0;                                           // [32]
    virtual bool GetCountryEnglishName(cIGZString& out, std::uint32_t country) = 0;             // [33]
    virtual bool GetCountryLocalName(cIGZString& out, std::uint32_t country) = 0;               // [34]
    virtual bool GetCountryIDFromCountryEnglishName(cIGZString& name, std::uint32_t& country) = 0;      // [35]
    virtual bool GetCountryIDFromCountryLocalName(cIGZString& name, std::uint32_t& country) = 0;        // [36]
    virtual cIGZLanguageUtility* GetLanguageUtility(std::uint32_t language) = 0;                // [37]
    virtual cIGZLanguageUtility* GetNewLanguageUtility(std::uint32_t language) = 0;             // [38]
    virtual float GetExchangeRateForLocale(std::uint32_t locale) = 0;                           // [39]
    virtual void SetExchangeRateForLocale(float rate, std::uint32_t locale) = 0;                // [40]
    virtual bool ConvertMoneyFromBaseToLocale(int amount, int& out, std::uint32_t locale) = 0;  // [41]
    virtual bool ConvertMoneyFromBaseToLocale(std::int64_t amount, std::int64_t& out, std::uint32_t locale) = 0;   // [42]
    virtual bool ConvertMoneyFromLocaleToLocale(int amount, std::uint32_t from, int& out, std::uint32_t to) = 0;   // [43]
    virtual bool ConvertMoneyFromLocaleToLocale(std::int64_t amount, std::uint32_t from, std::int64_t& out,
                                                std::uint32_t to) = 0;                          // [44]
};

class cRZLanguageManager final : public cIGZLanguageManager, public cRZSystemService {
public:
    cRZLanguageManager();                                  // @ 0x100066462
    ~cRZLanguageManager() override = default;              // [45] [46] @ 0x10006668c

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100066776
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }       // @ 0x100066808
    std::uint32_t Release() override { return cRZSystemService::Release(); }     // @ 0x10006682e
    bool Init() override { return true; }                          // @ 0x100066854
    bool Shutdown() override { return true; }                      // @ 0x100066864
    bool AddAvailableLanguage(std::uint32_t language) override;    // @ 0x100066874
    bool RemoveAvailableLanguage(std::uint32_t language) override; // @ 0x10006691a
    std::uint32_t GetCurrentLanguage() override { return language_; }   // @ 0x10006693a
    bool SetCurrentLanguage(std::uint32_t language) override;      // @ 0x100066944
    bool CanSwitchToLanguage(std::uint32_t language) override;     // @ 0x100066a26
    std::uint32_t GetCurrentSystemLanguage() override { return 1; }   // @ 0x100066a06
    std::uint32_t GetSystemLanguageFromLanguage(std::uint32_t) override { return 0; }   // @ 0x100066a12
    std::uint32_t GetLanguageFromSystemLanguage(std::uint32_t) override { return 1; }   // @ 0x100066a1a
    bool GetLanguageRuntimeLibraryName(cIGZString& out, std::uint32_t language) override;   // @ 0x100066c3c
    bool GetNextAvailableLanguage(std::uint32_t& language) override;   // @ 0x100066c78
    bool GetLanguageDirectoryName(cIGZString& out, std::uint32_t language) override;   // @ 0x100066caa
    bool GetLanguageEnglishName(cIGZString& out, std::uint32_t language) override;     // @ 0x100066e6c
    bool GetLanguageLocalName(cIGZString& out, std::uint32_t language) override;       // @ 0x1000671d8
    bool GetLanguageIDFromLanguageEnglishName(cIGZString& name, std::uint32_t& language) override;   // @ 0x100067458
    bool GetLanguageIDFromLanguageLocalName(cIGZString& name, std::uint32_t& language) override;     // @ 0x10006761e
    bool GetLanguageEnglishNameFromLanguageID(cIGZString& out, std::uint32_t language) override;     // @ 0x100066c54
    bool GetLanguageLocalNameFromLanguageID(cIGZString& out, std::uint32_t language) override;       // @ 0x100066c66
    std::uint32_t GetLanguageIDAlias(std::uint32_t language) override;                 // @ 0x1000676e0
    bool GetISO639LanguageCodeFromLanguageID(cIGZString& out, std::uint32_t language) override;      // @ 0x100067822
    bool GetISO639LanguageIDFromLanguageCode(cIGZString& code, std::uint32_t& language) override;    // @ 0x100067b08
    bool GetISO3166CountryCodeFromCountryID(cIGZString& out, std::uint32_t country) override;        // @ 0x100067bca
    bool GetISO3166CountryIDFromCountryCode(cIGZString& code, std::uint32_t& country) override;      // @ 0x100068010
    std::uint32_t GetCurrentCountry() override { return country_; }    // @ 0x1000680d2
    bool SetCurrentCountry(std::uint32_t country) override;            // @ 0x1000680dc
    std::uint32_t GetPrimaryLanguageForCountry(std::uint32_t country) override;       // @ 0x1000680f4
    std::uint32_t GetDefaultCountryForLanguage(std::uint32_t language) override;      // @ 0x10006811a
    std::uint32_t GetCurrentSystemCountry() override { return 0xffffffffu; }          // @ 0x100068144
    bool CanSwitchToCountry(int) override { return true; }             // @ 0x100068150
    bool GetCountryEnglishName(cIGZString& out, std::uint32_t country) override;      // @ 0x100068158
    bool GetCountryLocalName(cIGZString& out, std::uint32_t country) override;        // @ 0x1000685a8
    bool GetCountryIDFromCountryEnglishName(cIGZString& name, std::uint32_t& country) override;      // @ 0x1000685ba
    bool GetCountryIDFromCountryLocalName(cIGZString& name, std::uint32_t& country) override;        // @ 0x10006867c
    cIGZLanguageUtility* GetLanguageUtility(std::uint32_t language) override;         // @ 0x100067760
    cIGZLanguageUtility* GetNewLanguageUtility(std::uint32_t language) override;      // @ 0x10006777a
    float GetExchangeRateForLocale(std::uint32_t) override { return 1.0f; }           // @ 0x1000677e2
    void SetExchangeRateForLocale(float, std::uint32_t) override {}                   // @ 0x1000677f0
    bool ConvertMoneyFromBaseToLocale(int amount, int& out, std::uint32_t) override { out = amount; return true; }   // @ 0x1000677f6
    bool ConvertMoneyFromBaseToLocale(std::int64_t amount, std::int64_t& out, std::uint32_t) override { out = amount; return true; }   // @ 0x100067800
    bool ConvertMoneyFromLocaleToLocale(int amount, std::uint32_t, int& out, std::uint32_t) override { out = amount; return true; }   // @ 0x10006780c
    bool ConvertMoneyFromLocaleToLocale(std::int64_t amount, std::uint32_t, std::int64_t& out, std::uint32_t) override { out = amount; return true; }   // @ 0x100067816
    // Added by the concrete class (vtable slots 47 / 48).
    virtual void GetAllAvailableLanguagesIntoStringList(std::list<cRZString>& out);   // @ 0x100066a72
    virtual void GetAllAvailableLanguagesIntoIntList(std::list<std::uint32_t>& out);  // @ 0x100066b40

private:
    std::set<int> available_;                  // +0x30 languages that may be switched to (empty = all)
    std::uint32_t language_ = kDefaultLanguage;   // +0x48
    std::uint32_t country_ = kDefaultCountry;     // +0x4c
    cRZLanguageUtility utility_;               // +0x50 the utility of the current language
};
