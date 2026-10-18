#include "rz/LanguageManager.h"

#include <clocale>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "rz/RZStringUtils.h"
#include "rz/StringConversion.h"
#include "rz/TextEncoding.h"

namespace {

struct NameRow {
    std::uint8_t language;
    const char* names[12];
};
#include "rz/LanguageTables.inc"

const char* FindName(const NameRow* rows, std::size_t count, std::uint32_t language, unsigned number, unsigned maxNumber) {
    if (number < 1 || number > maxNumber) return nullptr;
    for (std::size_t i = 0; i < count; ++i)
        if (rows[i].language == language) return rows[i].names[number - 1];
    return nullptr;
}

// Every name getter ends the same way: an empty result becomes "Untranslated" and the call fails.
bool FinishName(cIGZString& out) {
    bool ok = true;
    if (out.Strlen() == 0) {
        out.AssignFromCString("Untranslated");
        ok = false;
    }
    ConvertUTF8StringToDestinationFormat(out);
    return ok;
}

// The currency symbols with the stray commas the original data has inside its euro and Czech-crown strings.
const char kEuroSymbol[] = "\xe2,\x82,\xac";
const char kCrownSymbol[] = "K\xc4\x8d{";

// Digits with the language's thousand separator between groups of three.
std::string GroupDigits(cRZLanguageUtility& utility, const cRZString& digits) {
    cRZString separator(",");
    utility.GetThousandSeparator(separator);
    std::string grouped;
    int remaining = static_cast<int>(digits.Strlen());
    const int total = remaining;
    for (; remaining > 3; ) {
        int take = remaining % 3;
        if (take == 0) take = 3;
        grouped += digits.Mid(static_cast<std::size_t>(total - remaining), static_cast<std::size_t>(take)).str();
        grouped += separator.str();
        remaining -= take;
    }
    grouped += digits.Right(static_cast<std::size_t>(remaining)).str();
    return grouped;
}

}  // namespace

// --- cRZLanguageUtility ------------------------------------------------------------------------------------

cRZLanguageUtility::cRZLanguageUtility() = default;

cRZLanguageUtility::cRZLanguageUtility(std::uint32_t language) : refCount_(1), language_(language) {}

bool cRZLanguageUtility::QueryInterface(std::uint32_t iid, void** out) {   // 0x10006e484
    if (iid != 1 && iid != kGZIID_cIGZLanguageUtility) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cRZLanguageUtility::Release() {   // 0x10006e4b4
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cRZLanguageUtility::Init() {   // 0x1000665be
    space_.AssignFromCString("\xc2\xa0");
    ConvertUTF8StringToDestinationFormat(space_);
    return true;
}

bool cRZLanguageUtility::GetDayName(std::uint8_t day, cIGZString& out) {   // 0x100068ace
    if (const char* name = FindName(kDayRows, sizeof(kDayRows) / sizeof(kDayRows[0]), language_, day, 7)) out.AssignFromCString(name);
    return FinishName(out);
}

bool cRZLanguageUtility::GetAbbrDayName(std::uint8_t day, cIGZString& out) {   // 0x1000694fc
    if (const char* name = FindName(kAbbrDayRows, sizeof(kAbbrDayRows) / sizeof(kAbbrDayRows[0]), language_, day, 7)) out.AssignFromCString(name);
    return FinishName(out);
}

bool cRZLanguageUtility::GetMonthName(std::uint8_t month, cIGZString& out) {   // 0x100069f70
    if (const char* name = FindName(kMonthRows, sizeof(kMonthRows) / sizeof(kMonthRows[0]), language_, month, 12)) out.AssignFromCString(name);
    return FinishName(out);
}

bool cRZLanguageUtility::GetAbbrMonthName(std::uint8_t month, cIGZString& out) {   // 0x10006aec4
    if (const char* name = FindName(kAbbrMonthRows, sizeof(kAbbrMonthRows) / sizeof(kAbbrMonthRows[0]), language_, month, 12)) out.AssignFromCString(name);
    return FinishName(out);
}

bool cRZLanguageUtility::MakeTimeString(int hour, int minute, int second, cIGZString& out) {   // 0x10006d01e
    // Out-of-range parts wrap; a negative second drops the seconds and a negative minute then drops the minutes.
    if (hour >= 24) hour %= 24;
    if (minute >= 60) minute %= 60;
    if (second >= 60) second %= 60;
    const unsigned h = static_cast<unsigned>(hour), m = static_cast<unsigned>(minute), s = static_cast<unsigned>(second);
    enum Layout { kTwelveHour, kHHMM, kDotted, kPlain, kDottedPadded };
    Layout layout = kPlain;
    switch (language_) {
        case 1: layout = kTwelveHour; break;
        case 2: case 3: case 4: case 10: case 0x10: layout = kHHMM; break;
        case 5: layout = kDotted; break;
        case 6: case 7: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: layout = kPlain; break;
        case 8: case 9: layout = kDottedPadded; break;
        case 0x1c: layout = kDotted; break;
        case 0x1d: layout = kHHMM; break;
        case 0x24: case 0x27: case 0x28: layout = kDottedPadded; break;
        case 0x2a: layout = kTwelveHour; break;
        default: layout = kPlain; break;
    }
    cRZString text;
    switch (layout) {
        case kTwelveHour: {
            const char* suffix = hour < 12 ? "am" : "pm";
            unsigned h12;
            if (hour == 0) h12 = 12;
            else h12 = hour < 13 ? h : h - 12;
            if (second >= 0) text.Sprintf("%u:%.2u:%.2u %s", h12, m, s, suffix);
            else if (minute >= 0) text.Sprintf("%u:%.2u %s", h12, m, suffix);
            else text.Sprintf("%u %s", h12, suffix);
            break;
        }
        case kHHMM:
            if (second >= 0) text.Sprintf("%.2u:%.2u:%.2u", h, m, s);
            else if (minute >= 0) text.Sprintf("%.2u:%.2u", h, m);
            else text.Sprintf("%.2u:00", h);
            break;
        case kDotted:
            if (second >= 0) text.Sprintf("%u.%.2u.%.2u", h, m, s);
            else if (minute >= 0) text.Sprintf("%u.%.2u", h, m);
            else text.Sprintf("%u:00", h);
            break;
        case kPlain:
            if (second >= 0) text.Sprintf("%u:%.2u:%.2u", h, m, s);
            else if (minute >= 0) text.Sprintf("%u:%.2u", h, m);
            else text.Sprintf("%u:00", h);
            break;
        case kDottedPadded:
            if (second >= 0) text.Sprintf("%.2u.%.2u.%.2u", h, m, s);
            else if (minute >= 0) text.Sprintf("%.2u.%.2u", h, m);
            else text.Sprintf("%.2u:00", h);
            break;
    }
    out.AssignFromCString(text.ToChar());
    return true;
}

bool cRZLanguageUtility::MakeDateString(std::uint32_t month, std::uint32_t day, std::uint32_t year, cIGZString& out,
                                        DateFormat format) {   // 0x10006d398
    cRZString monthName, abbrName;
    if (format > 1) {
        GetMonthName(static_cast<std::uint8_t>(month), monthName);
        GetAbbrMonthName(static_cast<std::uint8_t>(month), abbrName);
    }
    cRZString yearText;
    if (year < 100) yearText.Sprintf("%02u", year);
    else if (year < 1000) yearText.Sprintf("%03u", year);
    else if (year < 10000) yearText.Sprintf("%04u", year);
    else yearText.Sprintf("%05u", year);
    const char* y = yearText.ToChar();
    const char* mn = monthName.ToChar();
    const char* ab = abbrName.ToChar();
    cRZString text;
    enum Order { kDayFirst, kMonthFirst, kYearFirst };
    switch (language_) {
        case 2: case 3: case 5: case 6: case 0xd: case 0x13: case 0x23:
        case 4: case 0xa: case 0xb: case 0x10:
        case 7: case 8: case 9: case 0xe: {
            const char* numeric = "%u/%u/%s";
            const char* monthYear = "%u/%s";
            if (language_ == 4 || language_ == 0xa || language_ == 0xb || language_ == 0x10) { numeric = "%u.%u.%s"; monthYear = "%u.%s"; }
            else if (language_ == 7 || language_ == 8 || language_ == 9 || language_ == 0xe) { numeric = "%u-%u-%s"; monthYear = "%u-%s"; }
            switch (format) {
                case kDateNumeric: text.Sprintf(numeric, day, month, y); break;
                case kDateNumericMonthYear: text.Sprintf(monthYear, month, y); break;
                case kDateLong: text.Sprintf("%u %s, %s", day, mn, y); break;
                case kDateLongMonthYear: text.Sprintf("%s %s", mn, y); break;
                case kDateAbbreviated: text.Sprintf("%u %s., %s", day, ab, y); break;
                case kDateAbbreviatedMonthYear: text.Sprintf("%s. %s", ab, y); break;
                default: return false;
            }
            break;
        }
        case 0xf:
            switch (format) {
                case kDateNumeric: text.Sprintf("%s/%u/%u", y, month, day); break;
                case kDateNumericMonthYear: text.Sprintf("%s/%u", y, month); break;
                case kDateLong: text.Sprintf("%s.%s.%u", y, mn, day); break;
                case kDateLongMonthYear: text.Sprintf("%s.%s", y, mn); break;
                case kDateAbbreviated: text.Sprintf("%s.%s.%u", y, ab, day); break;
                case kDateAbbreviatedMonthYear: text.Sprintf("%s.%s", y, ab); break;
                default: return false;
            }
            break;
        case 0x11: case 0x12: case 0x14:
            switch (format) {
                case kDateNumeric: text.Sprintf("%s-%u-%u", y, month, day); break;
                case kDateNumericMonthYear: text.Sprintf("%s-%u", y, month); break;
                case kDateLong: text.Sprintf("%s %s %u", y, mn, day); break;
                case kDateLongMonthYear: text.Sprintf("%s %s", y, mn); break;
                case kDateAbbreviated: text.Sprintf("%s %s %u", y, ab, day); break;
                case kDateAbbreviatedMonthYear: text.Sprintf("%s %s", y, ab); break;
                default: return false;
            }
            break;
        default:   // month first, the US layout (also languages 0xffffffff, 0 and 1)
            switch (format) {
                case kDateNumeric: text.Sprintf("%u/%u/%s", month, day, y); break;
                case kDateNumericMonthYear: text.Sprintf("%u/%s", month, y); break;
                case kDateLong: text.Sprintf("%s %u, %s", mn, day, y); break;
                case kDateLongMonthYear: text.Sprintf("%s %s", mn, y); break;
                case kDateAbbreviated: text.Sprintf("%s. %u, %s", ab, day, y); break;
                case kDateAbbreviatedMonthYear: text.Sprintf("%s. %s", ab, y); break;
                default: return false;
            }
            break;
    }
    out.AssignFromCString(text.ToChar());
    return true;
}

bool cRZLanguageUtility::GetCurrencySymbol(cIGZString& out) {   // 0x10006ba94
    const char* symbol = "$";
    bool known = true;
    if (localCurrencySymbols_) {
        known = true;
        switch (language_) {
            case 1: case 0x11: case 0x12: symbol = "$"; break;
            case 2: symbol = "\xc2\xa3"; break;
            case 3: case 4: case 5: case 6: case 7: case 8: case 10: case 0xb: case 0xe: case 0x1b: case 0x26:
                symbol = kEuroSymbol; break;
            case 9: case 0x1d: symbol = "kr"; break;
            case 0xc: symbol = "\xe2\x82\xaa"; break;
            case 0xd: symbol = "\xd0\xb1"; break;
            case 0xf: symbol = "\xc2\xa5"; break;
            case 0x10: symbol = "\xc5\x82"; break;
            case 0x13: symbol = "\xe0\xb8\xbf"; break;
            case 0x14: symbol = "\\"; break;
            case 0x1a: symbol = kCrownSymbol; break;
            case 0x1c: symbol = "FT"; break;
            case 0x1e: symbol = "Lei"; break;
            case 0x21: symbol = "Lek"; break;
            case 0x22: symbol = "TL"; break;
            case 0x23: symbol = "Esc."; break;
            default: known = false; break;
        }
    }
    if (known) out.AssignFromCString(symbol);
    return FinishName(out);
}

bool cRZLanguageUtility::DoesCurrencySymbolPrecedeAmount() {   // 0x10006bc64
    return language_ - 1U < 0x23 && ((0x6000f4ad3ULL >> ((language_ - 1U) & 0x3f)) & 1);
}

bool cRZLanguageUtility::IsSpaceBetweenCurrencySymbolAndAmount() {   // 0x10006bc88
    if (language_ - 1U < 0x23) return (0x7fff0b5fcULL >> ((language_ - 1U) & 0x3f)) & 1;
    return true;
}

bool cRZLanguageUtility::GetThousandSeparator(cIGZString& out) {   // 0x10006bcac
    if (language_ > 0x23) {
        out.AssignFromCString(",");
        return false;
    }
    if ((0x8000063f8ULL >> (language_ & 0x3f)) & 1) {
        out.AssignFromCString(".");
        return true;
    }
    if ((0x1e9006ULL >> (language_ & 0x3f)) & 1) {
        out.AssignFromCString(",");
        return true;
    }
    if ((0x10c00ULL >> (language_ & 0x3f)) & 1) {
        out.AssignFromChars(space_.Data(), space_.Strlen());
        return true;
    }
    out.AssignFromCString(",");
    return false;
}

bool cRZLanguageUtility::GetDecimalSeparator(cIGZString& out) {   // 0x10006bd4a
    if (language_ < 0x24) {
        if ((0x800016ff8ULL >> (language_ & 0x3f)) & 1) {
            out.AssignFromCString(",");
            return true;
        }
        if ((0x1e9006ULL >> (language_ & 0x3f)) & 1) {
            out.AssignFromCString(".");
            return true;
        }
    }
    out.AssignFromCString(".");
    return false;
}

bool cRZLanguageUtility::MakeMoneyString(std::int64_t amount, cIGZString& out, const cIGZString* symbol) {   // 0x10006bdac
    const std::int64_t magnitude = amount > 0 ? amount : -amount;
    cRZString digits;
    digits.Sprintf("%lld", static_cast<long long>(magnitude));
    const std::string grouped = GroupDigits(*this, digits);
    auto symbolText = [&]() {
        cRZString text;
        if (symbol == nullptr) GetCurrencySymbol(text);
        else text.AssignFromIGZString(*symbol);
        return text.str();
    };
    std::string shown;
    if (!DoesCurrencySymbolPrecedeAmount()) {
        shown = grouped;
        if (IsSpaceBetweenCurrencySymbolAndAmount()) shown += space_.str();
        shown += symbolText();
    } else {
        shown = symbolText();
        if (IsSpaceBetweenCurrencySymbolAndAmount()) shown += space_.str();
        shown += grouped;
    }
    std::string result;
    if (amount >= 0) {
        result = shown;
    } else if (language_ == 10) {
        result = symbolText() + "-" + grouped;
    } else if (language_ == 8 || language_ == 0xc) {
        result = symbolText() + space_.str() + "-" + grouped;
    } else {
        result = "-" + shown;
    }
    out.AssignFromCString(result.c_str());
    return true;
}

bool cRZLanguageUtility::MakeFormattedMoneyString(double amount, cIGZString& out, const cIGZString& format,
                                                  const cIGZString* symbol) {   // 0x10006c50a
    out.Erase(0, 0xffffffffu);
    cRZString prefix, spec, suffix;
    const int length = static_cast<int>(format.Strlen());
    std::size_t pos = format.FindChars("%", 0, true);
    while (pos != static_cast<std::size_t>(-1)) {
        const bool escaped = static_cast<std::uint64_t>(static_cast<std::int64_t>(length - 2)) > pos && format.ToChar()[pos + 1] == '%';
        if (escaped) {   // "%%" is a literal percent sign: skip it
            pos = format.FindChars("%", pos + 2, true);
            continue;
        }
        if (pos != 0) prefix.AssignFromChars(format.ToChar(), static_cast<std::uint32_t>(pos));
        spec.AssignFromCString(format.ToChar() + pos);
        if (spec.Strlen() == 0) return false;
        // Find the conversion character: the first letter of "fegEGcCdiouxXnpsSlLIh" in the specification.
        static const char kConversions[] = "fegEGcCdiouxXnpsSlLIh";
        const char* begin = spec.ToChar();
        const char* end = begin + spec.Strlen();
        const char* found = begin;
        for (; found != end; ++found) {
            std::size_t k = 0;
            while (k < sizeof(kConversions) - 1 && *found != kConversions[k]) ++k;
            if (k < sizeof(kConversions) - 1) break;
        }
        if (found == end) return false;
        const long index = found - begin;
        if (std::memchr("fegEG", *found, 6) == nullptr) return false;
        suffix.AssignFromCString(spec.ToChar() + index + 1);
        spec.Erase(static_cast<std::uint32_t>(index + 1), 0xffffffffu);
        for (std::size_t p = 0; (p = prefix.FindChars("%%", p + 1, true)) != static_cast<std::size_t>(-1);) prefix.Erase(static_cast<std::uint32_t>(p), 1);
        for (std::size_t p = 0; (p = suffix.FindChars("%%", p + 1, true)) != static_cast<std::size_t>(-1);) suffix.Erase(static_cast<std::uint32_t>(p), 1);

        cRZString number;
        number.Sprintf(spec.ToChar(), amount > 0.0 ? amount : -amount);
        cRZString separator;
        GetThousandSeparator(separator);
        if (separator.Strlen() != 0) {
            // Group the integer part: the leading run of digits and padding spaces.
            std::size_t digits = 0;
            const std::size_t size = number.Strlen();
            while (digits < size) {
                const unsigned char c = static_cast<unsigned char>(number.ToChar()[digits]);
                if (!(c < 0x40 && ((1ULL << (c & 0x3f)) & 0x3ff000100000000ULL))) break;
                ++digits;
            }
            int at = static_cast<int>(digits) - 3;
            while (at > 0) {
                if (number.ToChar()[at - 1] == ' ') break;
                number.InsertString(static_cast<std::uint32_t>(at), separator);
                at -= 3;
            }
        }
        cRZString money;
        if (!MakeMoneyString(amount >= 0.0 ? 1 : -1, money, symbol)) return false;
        out.AppendString(prefix);
        const std::size_t one = money.FindChars("1", 0, true);
        money.ReplaceRangeWithChars(one, number.ToChar(), 1);
        out.AppendString(money);
        out.AppendString(suffix);
        return true;
    }
    return false;
}

bool cRZLanguageUtility::MakeNumberString(std::int64_t value, cIGZString& out) {   // 0x10006cb56
    const std::int64_t magnitude = value > 0 ? value : -value;
    cRZString digits;
    digits.Sprintf("%lld", static_cast<long long>(magnitude));
    const std::string grouped = GroupDigits(*this, digits);
    std::string result;
    if (value >= 0) {
        result = grouped;
    } else if (language_ <= 0x14) {
        if ((0x1fb7feU >> (language_ & 0x1f)) & 1) result = "-" + grouped;   // minus sign first
        else if (language_ == 0xb) result = grouped + "-";                    // Finnish: trailing minus
    }   // later languages get an empty string
    out.AssignFromCString(result.c_str());
    return true;
}

bool cRZLanguageUtility::MakeFormattedNumberString(double value, cIGZString& out, const cIGZString& format) {   // 0x10006cfda
    out.Sprintf(format.ToChar(), value);
    return true;
}

void cRZLanguageUtility::ConvertToLowerCase(const cIGZString& in, cIGZString& out) {   // 0x10006e338
    cRZString work;
    ConvertStringEncoding(in, work, -1);
    std::string text = work.str();
    RZStrlwrAImp(text.data());
    work.AssignFromCString(text.c_str());
    ConvertStringEncoding(work, -1, out);
}

void cRZLanguageUtility::ConvertToUpperCase(const cIGZString& in, cIGZString& out) {   // 0x10006e3d2
    cRZString work;
    ConvertStringEncoding(in, work, -1);
    std::string text = work.str();
    RZStruprAImp(text.data());
    work.AssignFromCString(text.c_str());
    ConvertStringEncoding(work, -1, out);
}

int cRZLanguageUtility::CompareStrings(const cIGZString& a, const cIGZString& b, bool caseSensitive) {   // 0x10006751a
    return CompareStringsStatic(a, b, caseSensitive);
}

int cRZLanguageUtility::CompareStringsStatic(const cIGZString& a, const cIGZString& b, bool caseSensitive) {
    cRZString left, right;
    ConvertStringEncoding(a, left, -1);
    ConvertStringEncoding(b, right, -1);
    return caseSensitive ? std::strcmp(left.ToChar(), right.ToChar()) : strcasecmp(left.ToChar(), right.ToChar());
}

std::int64_t cRZLanguageUtility::GetCharacterSetFromLanguage() {   // 0x100068778
    // First code page that the language uses; Western (1252) when none claims it.
    static const int kOrder[] = {0x4e4, 0x4e2, 0x36a, 0x3a4, 0x3a8, 0x3b6, 0x3b5, 0x4e3, 0x4e3, 0x4e5, 0x4e6, 0x4e7, 0x4e8, 0x4e9};
    for (int codePage : kOrder)
        if (DoesLanguageUseCharacterSet(codePage)) return codePage;
    return (DoesLanguageUseCharacterSet(0x4e9) ? 1 : 0) * 5 + 0x4e4;
}

bool cRZLanguageUtility::DoesLanguageUseCharacterSet(int codePage) {   // 0x100068946
    if (codePage < 0x3b5) {
        if (codePage == 0x36a) return language_ == 0x13;
        if (codePage == 0x3a4) return language_ == 0xf;
        if (codePage == 0x3a8) return language_ == 0x11;
        return false;
    }
    switch (codePage) {
        case 0x4e2: {
            const std::uint32_t k = language_ - 0xd;
            return k <= 0x14 && ((0x1aa009U >> k) & 1);
        }
        case 0x4e3: {
            const std::uint32_t k = language_ - 0xd;
            return k <= 0xb && ((0xc01U >> k) & 1);
        }
        case 0x4e4: {
            const std::uint32_t k = language_ - 1U;
            return k <= 0x29 && ((0x3fc780027ffULL >> (k & 0x3f)) & 1);
        }
        case 0x4e5: return language_ == 0x1b;
        case 0x4e6: return language_ == 0x22;
        case 0x4e7: return language_ == 0xc;
        case 0x4e8: return language_ == 0x16;
        case 0x3b5: return language_ == 0x14;
        case 0x3b6: return language_ == 0x12;
        default: return false;
    }
}

std::int64_t cRZLanguageUtility::GetLanguageRoadDrivingSide() {   // 0x10006e46c
    return language_ != 2 && language_ != 0xf;   // the UK and Japan drive on the left
}

bool cRZLanguageUtility::GetMeasurementSystem() { return country_ != kDefaultCountry; }   // 0x100068a3c

bool cRZLanguageUtility::AreFirstAndLastNamesReversed() {   // 0x100068a4c
    return language_ - 0xfU < 6 && ((0x2dU >> ((language_ - 0xfU) & 0x1f)) & 1);
}

bool cRZLanguageUtility::DoesLanguageUseInputMethodEditing() {   // 0x100068a68
    return language_ - 0xfU < 6 && ((0x2dU >> ((language_ - 0xfU) & 0x1f)) & 1);
}

bool cRZLanguageUtility::DoesLanguageUseMultiByteCharacters() {   // 0x100068a84
    const int page = GetCurrentFrameworkCodePage();
    if (page == 0 || page == 0x10) return false;
    if (page == 8) return language_ - 1U > 1;
    return language_ - 0xfU < 6 && ((0x3dU >> ((language_ - 0xfU) & 0x1f)) & 1);
}

// --- cRZLanguageManager ------------------------------------------------------------------------------------

cRZLanguageManager::cRZLanguageManager() : cRZSystemService(kGZSID_cRZLanguageManager, kRZLanguageManagerPriority) {   // 0x100066462
    utility_.Init();
}

bool cRZLanguageManager::QueryInterface(std::uint32_t iid, void** out) {   // 0x100066776
    if (iid == kGZIID_cIGZSystemService) *out = static_cast<cIGZSystemService*>(this);
    else if (iid == kGZIID_cIGZLanguageManager) *out = static_cast<cIGZLanguageManager*>(this);
    else return false;
    AddRef();
    return true;
}

bool cRZLanguageManager::AddAvailableLanguage(std::uint32_t language) {   // 0x100066874
    if (language < kLanguageCount + 1) available_.insert(static_cast<int>(language));
    return true;
}

bool cRZLanguageManager::RemoveAvailableLanguage(std::uint32_t language) {   // 0x10006691a
    available_.erase(static_cast<int>(language));
    return true;
}

bool cRZLanguageManager::SetCurrentLanguage(std::uint32_t language) {   // 0x100066944
    if (language == 0) language = 1;
    if (language == language_) return true;
    if (CanSwitchToLanguage(language)) {
        language_ = language;
        utility_.SetLanguage(language);
        cRZString locale;
        if (GetLanguageRuntimeLibraryName(locale, language)) {
            std::setlocale(LC_ALL, locale.ToChar());
            return true;
        }
    }
    return false;
}

bool cRZLanguageManager::CanSwitchToLanguage(std::uint32_t language) {   // 0x100066a26
    return available_.empty() || available_.count(static_cast<int>(language)) != 0;
}

void cRZLanguageManager::GetAllAvailableLanguagesIntoStringList(std::list<cRZString>& out) {   // 0x100066a72
    out.clear();
    std::uint32_t language = 1;
    do {
        if (GetNextAvailableLanguage(language)) {
            cRZString name;
            GetLanguageEnglishName(name, language);
            out.push_back(name);
        }
        ++language;
    } while (language < kLanguageCount + 1);
}

void cRZLanguageManager::GetAllAvailableLanguagesIntoIntList(std::list<std::uint32_t>& out) {   // 0x100066b40
    out.clear();
    for (int language : available_) out.push_back(static_cast<std::uint32_t>(language));
}

bool cRZLanguageManager::GetLanguageRuntimeLibraryName(cIGZString& out, std::uint32_t language) {   // 0x100066c3c
    if (language == 0) language = language_;
    return GetISO639LanguageCodeFromLanguageID(out, language);
}

bool cRZLanguageManager::GetLanguageEnglishNameFromLanguageID(cIGZString& out, std::uint32_t language) {   // 0x100066c54
    return GetLanguageEnglishName(out, language);
}

bool cRZLanguageManager::GetLanguageLocalNameFromLanguageID(cIGZString& out, std::uint32_t language) {   // 0x100066c66
    return GetLanguageLocalName(out, language);
}

bool cRZLanguageManager::GetNextAvailableLanguage(std::uint32_t& language) {   // 0x100066c78
    for (;;) {
        const std::uint32_t before = language;
        language = before + 1;
        if (before > kLanguageCount) return false;
        if (CanSwitchToLanguage(language)) return true;
    }
}

bool cRZLanguageManager::GetLanguageDirectoryName(cIGZString& out, std::uint32_t language) {   // 0x100066caa
    if (language == 0) language = language_;
    const char* name = language < kLanguageCount + 1 ? kLanguageDirectoryOverrides[language] : nullptr;
    if (name != nullptr) out.AssignFromCString(name);
    else GetLanguageEnglishName(out, language);
    return out.Strlen() != 0;
}

bool cRZLanguageManager::GetLanguageEnglishName(cIGZString& out, std::uint32_t language) {   // 0x100066e6c
    if (language == 0) language = language_;
    const char* name = language >= 1 && language < kLanguageCount + 1 ? kLanguageEnglishNames[language] : nullptr;
    if (name == nullptr) {
        out.AssignFromCString("");
        return false;
    }
    out.AssignFromCString(name);
    return true;
}

bool cRZLanguageManager::GetLanguageLocalName(cIGZString& out, std::uint32_t language) {   // 0x1000671d8
    if (language == 0) language = language_;
    switch (language) {
        case 1: case 2: out.AssignFromCString("English"); break;
        case 3: out.AssignFromCString("Fran\xc3\xa7" "ais"); break;
        case 4: out.AssignFromCString("Deutsch"); break;
        case 5: out.AssignFromCString("Italiano"); break;
        case 6: out.AssignFromCString("Espa\xc3\xb1" "ol"); break;
        case 7: out.AssignFromCString("Nederlands"); break;
        case 8: out.AssignFromCString("Dansk"); break;
        case 9: out.AssignFromCString("Svensk"); break;
        case 10: case 0xc: case 0xd: case 0xf: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
            out.AssignFromCString("Untranslated"); break;
        case 0xb: out.AssignFromCString("Suomi"); break;
        case 0xe: case 0x23: out.AssignFromCString("Portugues"); break;
        default: break;
    }
    // The test is inverted in the original: a filled-in name is replaced by "Untranslated" and reported as failure.
    bool ok = true;
    if (out.Strlen() != 0) {
        out.AssignFromCString("Untranslated");
        ok = false;
    }
    ConvertUTF8StringToDestinationFormat(out);
    return ok;
}

namespace {

// Walks ids 1..last and returns the first whose name (from `getName`) equals `wanted` ignoring case.
template <class GetName>
bool FindIdByName(cIGZString& wanted, std::uint32_t& id, std::uint32_t last, std::uint32_t fallback, GetName getName) {
    cRZString candidate;
    std::uint32_t i = 1;
    do {
        if (getName(candidate, i) && cRZLanguageUtility::CompareStringsStatic(wanted, candidate, false) == 0) {
            id = i;
            return true;
        }
        ++i;
    } while (i < last);
    id = fallback;
    return false;
}

}  // namespace

bool cRZLanguageManager::GetLanguageIDFromLanguageEnglishName(cIGZString& name, std::uint32_t& language) {   // 0x100067458
    return FindIdByName(name, language, kLanguageCount + 1, 1, [&](cIGZString& out, std::uint32_t i) { return GetLanguageEnglishName(out, i); });
}

bool cRZLanguageManager::GetLanguageIDFromLanguageLocalName(cIGZString& name, std::uint32_t& language) {   // 0x10006761e
    return FindIdByName(name, language, kLanguageCount + 1, 1, [&](cIGZString& out, std::uint32_t i) { return GetLanguageLocalName(out, i); });
}

std::uint32_t cRZLanguageManager::GetLanguageIDAlias(std::uint32_t language) {   // 0x1000676e0
    if (language == 0) language = language_;
    switch (language) {
        case 0x24: case 0x25: case 0x26: return 3;   // Swiss / Canadian / Belgian French
        case 0x27: return 4;                         // Swiss German
        case 0x28: return 5;                         // Swiss Italian
        case 0x29: return 7;                         // Belgian Dutch
        case 0x2a: return 6;                         // Mexican Spanish
        default:
            if (language == 2) return 1;
            if (language - 0x17 < 3) return 0xd;     // Bulgarian, Cyrillic, Ukrainian -> Russian
            return language;
    }
}

bool cRZLanguageManager::GetISO639LanguageCodeFromLanguageID(cIGZString& out, std::uint32_t language) {   // 0x100067822
    if (language == 0) language = language_;
    const char* code = language >= 1 && language < kLanguageCount + 1 ? kISO639Codes[language] : nullptr;
    if (code == nullptr) {
        out.AssignFromCString("");
        return false;
    }
    out.AssignFromCString(code);
    return true;
}

bool cRZLanguageManager::GetISO639LanguageIDFromLanguageCode(cIGZString& code, std::uint32_t& language) {   // 0x100067b08
    return FindIdByName(code, language, kLanguageCount + 1, 1, [&](cIGZString& out, std::uint32_t i) { return GetISO639LanguageCodeFromLanguageID(out, i); });
}

bool cRZLanguageManager::GetISO3166CountryCodeFromCountryID(cIGZString& out, std::uint32_t country) {   // 0x100067bca
    if (country == 0) country = country_;
    const char* code = country >= 1 && country < kCountryCount + 1 ? kISO3166Codes[country] : nullptr;
    if (code == nullptr) {
        out.AssignFromCString("");
        return false;
    }
    out.AssignFromCString(code);
    return true;
}

bool cRZLanguageManager::GetISO3166CountryIDFromCountryCode(cIGZString& code, std::uint32_t& country) {   // 0x100068010
    return FindIdByName(code, country, kCountryCount + 2, kDefaultCountry, [&](cIGZString& out, std::uint32_t i) { return GetISO3166CountryCodeFromCountryID(out, i); });
}

bool cRZLanguageManager::SetCurrentCountry(std::uint32_t country) {   // 0x1000680dc
    if (country == 0) country = kDefaultCountry;
    country_ = country;
    utility_.SetCountry(country);
    return true;
}

std::uint32_t cRZLanguageManager::GetPrimaryLanguageForCountry(std::uint32_t country) {   // 0x1000680f4
    if (country == 0) country = country_;
    return country - 2 < 0x37 ? kPrimaryLanguageForCountry[country - 2] : 0;
}

std::uint32_t cRZLanguageManager::GetDefaultCountryForLanguage(std::uint32_t language) {   // 0x10006811a
    if (language == 0) language = language_;
    return language - 2 < 0x2b ? kDefaultCountryForLanguage[language - 2] : kDefaultCountry;
}

bool cRZLanguageManager::GetCountryEnglishName(cIGZString& out, std::uint32_t country) {   // 0x100068158
    if (country == 0) country = country_;
    const char* name = country >= 1 && country < kCountryCount + 1 ? kCountryEnglishNames[country] : nullptr;
    if (name == nullptr) {
        out.AssignFromCString("UnitedStates");
        return false;
    }
    out.AssignFromCString(name);
    return true;
}

bool cRZLanguageManager::GetCountryLocalName(cIGZString& out, std::uint32_t country) {   // 0x1000685a8
    return GetCountryEnglishName(out, country);
}

bool cRZLanguageManager::GetCountryIDFromCountryEnglishName(cIGZString& name, std::uint32_t& country) {   // 0x1000685ba
    return FindIdByName(name, country, kCountryCount + 2, kDefaultCountry, [&](cIGZString& out, std::uint32_t i) { return GetCountryEnglishName(out, i); });
}

bool cRZLanguageManager::GetCountryIDFromCountryLocalName(cIGZString& name, std::uint32_t& country) {   // 0x10006867c
    return FindIdByName(name, country, kCountryCount + 2, kDefaultCountry, [&](cIGZString& out, std::uint32_t i) { return GetCountryLocalName(out, i); });
}

cIGZLanguageUtility* cRZLanguageManager::GetLanguageUtility(std::uint32_t language) {   // 0x100067760
    if (language == 0) language = language_;
    return language == language_ ? &utility_ : nullptr;
}

cIGZLanguageUtility* cRZLanguageManager::GetNewLanguageUtility(std::uint32_t language) {   // 0x10006777a
    return new cRZLanguageUtility(language);
}
