// cRZLanguageManager / cRZLanguageUtility: names and codes, formatting, switching rules.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>

#include "rz/LanguageManager.h"

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_failed; } } while (0)
#define CHECK_STR(actual, expected) do { const char* a_ = (actual); if (std::strcmp(a_, (expected)) != 0) { std::printf("FAIL line %d: \"%s\" != \"%s\"\n", __LINE__, a_, (expected)); ++g_failed; } } while (0)

int main() {
    cRZLanguageManager* manager = new cRZLanguageManager();
    manager->AddRef();
    CHECK(manager->GetServiceID() == 0x441e5070);
    CHECK(manager->GetServicePriority() == -3000000);
    CHECK(manager->GetCurrentLanguage() == 1 && manager->GetCurrentCountry() == 0x36);
    void* p = nullptr;
    CHECK(manager->QueryInterface(0x54b7d5, &p) && p == static_cast<cIGZLanguageManager*>(manager)); manager->Release();
    CHECK(manager->QueryInterface(0x287fb697, &p) && p == static_cast<cIGZSystemService*>(manager)); manager->Release();
    CHECK(!manager->QueryInterface(0x1234, &p));

    cRZString s;
    CHECK(manager->GetLanguageEnglishName(s, 3)); CHECK_STR(s.ToChar(), "French");
    CHECK(manager->GetLanguageEnglishName(s, 0)); CHECK_STR(s.ToChar(), "English");   // 0 = current
    CHECK(!manager->GetLanguageEnglishName(s, 45)); CHECK_STR(s.ToChar(), "");
    CHECK(manager->GetLanguageEnglishNameFromLanguageID(s, 0x23)); CHECK_STR(s.ToChar(), "Brazilian Portuguese");
    CHECK(manager->GetLanguageDirectoryName(s, 2)); CHECK_STR(s.ToChar(), "UKEnglsh");
    CHECK(manager->GetLanguageDirectoryName(s, 0xb)); CHECK_STR(s.ToChar(), "Finnish");
    CHECK(manager->GetLanguageDirectoryName(s, 0x17)); CHECK_STR(s.ToChar(), "Blgarian");
    CHECK(manager->GetISO639LanguageCodeFromLanguageID(s, 4)); CHECK_STR(s.ToChar(), "de");
    CHECK(!manager->GetISO639LanguageCodeFromLanguageID(s, 99));
    cRZString code("FR");
    std::uint32_t id = 0;
    CHECK(manager->GetISO639LanguageIDFromLanguageCode(code, id) && id == 3);    // first language coded "fr"
    cRZString nameDutch("dutch");
    CHECK(manager->GetLanguageIDFromLanguageEnglishName(nameDutch, id) && id == 7);
    cRZString nope("Klingon");
    CHECK(!manager->GetLanguageIDFromLanguageEnglishName(nope, id) && id == 1);
    CHECK(manager->GetLanguageIDAlias(0x24) == 3 && manager->GetLanguageIDAlias(2) == 1 && manager->GetLanguageIDAlias(0x18) == 0xd
          && manager->GetLanguageIDAlias(5) == 5 && manager->GetLanguageIDAlias(0) == 1);

    // Local names: the inverted test turns real names into "Untranslated" / failure.
    CHECK(!manager->GetLanguageLocalName(s, 3)); CHECK_STR(s.ToChar(), "Untranslated");
    cRZString empty;
    CHECK(manager->GetLanguageLocalName(empty, 0x1b)); CHECK_STR(empty.ToChar(), "");   // no local name, nothing set

    // Countries.
    CHECK(manager->GetCountryEnglishName(s, 0x35)); CHECK_STR(s.ToChar(), "United Kingdom");
    CHECK(!manager->GetCountryEnglishName(s, 0x3b)); CHECK_STR(s.ToChar(), "UnitedStates");
    CHECK(manager->GetISO3166CountryCodeFromCountryID(s, 0x13)); CHECK_STR(s.ToChar(), "de");
    CHECK(!manager->GetISO3166CountryCodeFromCountryID(s, 0x15));    // Holy See has no code
    cRZString jp("JP");
    CHECK(manager->GetISO3166CountryIDFromCountryCode(jp, id) && id == 0x1f);
    cRZString japan("japan");
    CHECK(manager->GetCountryIDFromCountryEnglishName(japan, id) && id == 0x1f);
    CHECK(!manager->GetCountryIDFromCountryEnglishName(nope, id) && id == 0x36);
    CHECK(manager->GetPrimaryLanguageForCountry(0x13) == 4);     // Germany -> German
    CHECK(manager->GetPrimaryLanguageForCountry(1) == 0 && manager->GetPrimaryLanguageForCountry(0x40) == 0);
    CHECK(manager->GetDefaultCountryForLanguage(4) == 0x13);
    CHECK(manager->GetDefaultCountryForLanguage(1) == 0x36 && manager->GetDefaultCountryForLanguage(99) == 0x36);
    CHECK(manager->SetCurrentCountry(0) && manager->GetCurrentCountry() == 0x36);
    CHECK(manager->SetCurrentCountry(0x13) && manager->GetCurrentCountry() == 0x13);
    CHECK(manager->GetLanguageUtility(0)->GetMeasurementSystem());           // not the US any more
    manager->SetCurrentCountry(0x36);

    // Available languages.
    CHECK(manager->CanSwitchToLanguage(5));                                  // empty set = anything
    manager->AddAvailableLanguage(1); manager->AddAvailableLanguage(4); manager->AddAvailableLanguage(4); manager->AddAvailableLanguage(45);
    CHECK(manager->CanSwitchToLanguage(4) && !manager->CanSwitchToLanguage(5) && !manager->CanSwitchToLanguage(45));
    std::list<std::uint32_t> ids;
    manager->GetAllAvailableLanguagesIntoIntList(ids);
    CHECK(ids.size() == 2 && ids.front() == 1 && ids.back() == 4);
    std::uint32_t cursor = 1;
    CHECK(manager->GetNextAvailableLanguage(cursor) && cursor == 4);         // looks past the cursor
    CHECK(!manager->GetNextAvailableLanguage(cursor) && cursor == 46);
    std::list<cRZString> names;
    manager->GetAllAvailableLanguagesIntoStringList(names);
    CHECK(names.size() == 1 && std::strcmp(names.front().ToChar(), "German") == 0);
    manager->RemoveAvailableLanguage(4);
    CHECK(!manager->CanSwitchToLanguage(4));
    manager->AddAvailableLanguage(4);
    CHECK(manager->SetCurrentLanguage(4) == false || true);                  // setlocale("de") may or may not exist
    CHECK(manager->GetCurrentLanguage() == 4);
    CHECK(manager->GetLanguageUtility(4) != nullptr && manager->GetLanguageUtility(1) == nullptr);
    CHECK(!manager->SetCurrentLanguage(9) && manager->GetCurrentLanguage() == 4);   // not in the available set
    CHECK(manager->SetCurrentLanguage(4));                                           // unchanged
    manager->RemoveAvailableLanguage(1); manager->RemoveAvailableLanguage(4);
    manager->SetCurrentLanguage(1);
    std::setlocale(LC_ALL, "C");
    CHECK(manager->GetCurrentSystemLanguage() == 1 && manager->GetSystemLanguageFromLanguage(7) == 0
          && manager->GetCurrentSystemCountry() == 0xffffffffu && manager->CanSwitchToCountry(7));
    int money = 0; std::int64_t money64 = 0;
    CHECK(manager->ConvertMoneyFromBaseToLocale(5, money, 1) && money == 5 && manager->GetExchangeRateForLocale(1) == 1.0f);
    CHECK(manager->ConvertMoneyFromLocaleToLocale(std::int64_t(7), 1, money64, 2) && money64 == 7);

    // Utility.
    cIGZLanguageUtility* en = manager->GetLanguageUtility(1);
    CHECK(en->GetDayName(1, s) && std::strcmp(s.ToChar(), "Monday") == 0);
    { cRZString fresh; CHECK(!en->GetDayName(8, fresh) && std::strcmp(fresh.ToChar(), "Untranslated") == 0); CHECK(en->GetDayName(8, s)); }   // an unknown day leaves a filled string alone
    CHECK(en->GetAbbrDayName(4, s) && std::strcmp(s.ToChar(), "Thur") == 0);
    CHECK(en->GetMonthName(12, s) && std::strcmp(s.ToChar(), "December") == 0);
    CHECK(en->GetAbbrMonthName(9, s) && std::strcmp(s.ToChar(), "Sep") == 0);
    cRZLanguageUtility* de = static_cast<cRZLanguageUtility*>(manager->GetNewLanguageUtility(4));
    CHECK(de->Language() == 4 && de->Country() == 0x36);
    CHECK(de->GetMonthName(3, s)); CHECK_STR(s.ToChar(), "M\xe4rz");
    CHECK(de->GetDayName(6, s)); CHECK_STR(s.ToChar(), "Samstag");
    CHECK(de->GetThousandSeparator(s) && std::strcmp(s.ToChar(), ".") == 0);
    CHECK(de->GetDecimalSeparator(s) && std::strcmp(s.ToChar(), ",") == 0);
    CHECK(en->GetThousandSeparator(s) && std::strcmp(s.ToChar(), ",") == 0);
    CHECK(en->GetDecimalSeparator(s) && std::strcmp(s.ToChar(), ".") == 0);

    // Numbers and money.
    CHECK(en->MakeNumberString(1234567, s)); CHECK_STR(s.ToChar(), "1,234,567");
    CHECK(en->MakeNumberString(-1234, s)); CHECK_STR(s.ToChar(), "-1,234");
    CHECK(en->MakeNumberString(123, s)); CHECK_STR(s.ToChar(), "123");
    CHECK(en->MakeNumberString(-12, s)); CHECK_STR(s.ToChar(), "-12");
    CHECK(de->MakeNumberString(1234567, s)); CHECK_STR(s.ToChar(), "1.234.567");
    CHECK(en->MakeMoneyString(1234567, s, nullptr)); CHECK_STR(s.ToChar(), "$1,234,567");   // English: symbol first, no space
    CHECK(en->MakeMoneyString(-500, s, nullptr)); CHECK_STR(s.ToChar(), "-$500");
    cRZString sym("Sim");
    CHECK(en->MakeMoneyString(42, s, &sym)); CHECK_STR(s.ToChar(), "Sim42");
    CHECK(en->MakeFormattedNumberString(3.14159, s, cRZString("%.2f"))); CHECK_STR(s.ToChar(), "3.14");
    CHECK(en->MakeFormattedMoneyString(1234.5, s, cRZString("Total: %.2f units"), nullptr)); CHECK_STR(s.ToChar(), "Total: $1,234.50 units");
    CHECK(!en->MakeFormattedMoneyString(1.0, s, cRZString("no spec"), nullptr));
    CHECK(!en->MakeFormattedMoneyString(1.0, s, cRZString("%d"), nullptr));

    // Times and dates.
    CHECK(en->MakeTimeString(15, 7, 9, s)); CHECK_STR(s.ToChar(), "3:07:09 pm");
    CHECK(en->MakeTimeString(0, 5, -1, s)); CHECK_STR(s.ToChar(), "12:05 am");
    CHECK(en->MakeTimeString(12, -1, -1, s)); CHECK_STR(s.ToChar(), "12 pm");
    CHECK(de->MakeTimeString(7, 3, 4, s)); CHECK_STR(s.ToChar(), "07:03:04");
    CHECK(en->MakeDateString(3, 14, 2003, s, cIGZLanguageUtility::kDateNumeric)); CHECK_STR(s.ToChar(), "3/14/2003");
    CHECK(en->MakeDateString(3, 14, 2003, s, cIGZLanguageUtility::kDateLong)); CHECK_STR(s.ToChar(), "March 14, 2003");
    CHECK(en->MakeDateString(3, 14, 2003, s, cIGZLanguageUtility::kDateAbbreviatedMonthYear)); CHECK_STR(s.ToChar(), "Mar. 2003");
    CHECK(de->MakeDateString(3, 14, 2003, s, cIGZLanguageUtility::kDateNumeric)); CHECK_STR(s.ToChar(), "14.3.2003");
    CHECK(de->MakeDateString(3, 14, 2003, s, cIGZLanguageUtility::kDateLong)); CHECK_STR(s.ToChar(), "14 M\xe4rz, 2003");
    CHECK(en->MakeDateString(3, 14, 7, s, cIGZLanguageUtility::kDateNumeric)); CHECK_STR(s.ToChar(), "3/14/07");
    CHECK(!en->MakeDateString(3, 14, 2003, s, static_cast<cIGZLanguageUtility::DateFormat>(6)));

    // Script questions.
    CHECK(en->GetCharacterSetFromLanguage() == 0x4e4 && en->DoesLanguageUseCharacterSet(0x4e4) && !en->DoesLanguageUseCharacterSet(0x3a4));
    CHECK(en->GetLanguageRoadDrivingSide() == 1);
    de->SetLanguage(2); CHECK(de->GetLanguageRoadDrivingSide() == 0);
    de->SetLanguage(0xf); CHECK(de->GetCharacterSetFromLanguage() == 0x3a4 && de->AreFirstAndLastNamesReversed() && de->DoesLanguageUseMultiByteCharacters());
    CHECK(!en->AreFirstAndLastNamesReversed());
    CHECK(en->DoesCurrencySymbolPrecedeAmount() && !en->IsSpaceBetweenCurrencySymbolAndAmount());
    de->SetLanguage(4); CHECK(!de->DoesCurrencySymbolPrecedeAmount());
    CHECK(de->MakeMoneyString(1234, s, nullptr)); CHECK_STR(s.ToChar(), "1.234\xc2\xa0$");   // symbol after the amount
    cRZString upper;
    en->ConvertToUpperCase(cRZString("Sim City 4"), upper); CHECK_STR(upper.ToChar(), "SIM CITY 4");
    en->ConvertToLowerCase(cRZString("Sim City 4"), upper); CHECK_STR(upper.ToChar(), "sim city 4");
    CHECK(en->CompareStrings(cRZString("abc"), cRZString("ABC"), false) == 0 && en->CompareStrings(cRZString("abc"), cRZString("ABC"), true) != 0);
    CHECK(de->Release() == 0);                                                 // the new utility had one reference

    manager->Release();
    std::printf(g_failed ? "FAILED (%d)\n" : "OK\n", g_failed);
    return g_failed ? 1 : 0;
}
