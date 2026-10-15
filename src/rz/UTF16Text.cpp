#include "rz/UTF16Text.h"

#include <unordered_set>

namespace {

// kinsoku tables of the original (pJapaneseUnicodeBeginTable ... pUniversalUnicodeEndTable)
constexpr std::uint16_t kJapaneseBegin[0x49] = {
    0x00b0, 0x2019, 0x201d, 0x2030, 0x2032, 0x2033, 0x2103, 0x3001, 0x3002, 0x3005, 0x3009, 0x300b,
    0x300d, 0x300f, 0x3011, 0x3015, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085,
    0x3087, 0x308e, 0x309b, 0x309c, 0x309d, 0x309e, 0x30a1, 0x30a3, 0x30a5, 0x30a7, 0x30a9, 0x30c3,
    0x30e3, 0x30e5, 0x30e7, 0x30ee, 0x30f5, 0x30f6, 0x30fb, 0x30fc, 0x30fd, 0x30fe, 0xff01, 0xff05,
    0xff09, 0xff0c, 0xff0e, 0xff1a, 0xff1b, 0xff1f, 0xff3d, 0xff5d, 0xff61, 0xff63, 0xff64, 0xff65,
    0xff67, 0xff68, 0xff69, 0xff6a, 0xff6b, 0xff6c, 0xff6d, 0xff6e, 0xff6f, 0xff70, 0xff9e, 0xff9f,
    0xffe0};
constexpr std::uint16_t kSimplifiedChineseBegin[0x21] = {
    0x3001, 0x3002, 0x00b7, 0x02c9, 0x02c7, 0x00a8, 0x3003, 0x3005, 0x2016, 0x2026, 0x2019, 0x201d,
    0x3015, 0x3009, 0x300b, 0x300d, 0x300f, 0x3017, 0x3011, 0x2236, 0xff01, 0xff02, 0xff07, 0xff09,
    0xff0c, 0xff0e, 0xff1a, 0xff1b, 0xff1f, 0xff3d, 0xff40, 0xff5c, 0x2015};
constexpr std::uint16_t kTraditionalChineseBegin[0x90] = {
    0xff5e, 0xfe4b, 0xfe34, 0xfe6b, 0xff12, 0xfe5f, 0xff13, 0xff04, 0xfe69, 0x222b, 0xfe6a, 0xff15,
    0xfe3f, 0xfe3d, 0x039b, 0x2566, 0x252c, 0x2565, 0xff16, 0xfe60, 0xff17, 0x203b, 0x2606, 0xff18,
    0xff08, 0xfe35, 0x25cb, 0x25cf, 0xff19, 0xff09, 0xfe36, 0xff10, 0x02cd, 0xfe62, 0x2640, 0x2642,
    0x2295, 0x00b1, 0x253c, 0x256a, 0x256c, 0x256a, 0x256b, 0xfe31, 0xfe33, 0x2502, 0x2551, 0x2595,
    0xff01, 0xff20, 0xff03, 0xff04, 0xff05, 0x2191, 0xff06, 0xff0a, 0xfe59, 0xfe5a, 0xff3f, 0xff0b,
    0x2225, 0xff5b, 0xfe37, 0xfe5b, 0xff5d, 0xfe38, 0xfe5c, 0x201c, 0x201d, 0x301d, 0x3003, 0xff1b,
    0xfe54, 0x3124, 0xff0c, 0xfe50, 0x3001, 0xfe51, 0x311d, 0xff1c, 0xfe64, 0x3008, 0x2266, 0x2190,
    0x2220, 0x300a, 0xfe3d, 0xfe3f, 0x2196, 0x25e3, 0x25e4, 0x0394, 0x250c, 0x256d, 0x2554, 0x2552,
    0x2553, 0x256d, 0x221f, 0x2199, 0x2514, 0x2570, 0x255a, 0x2558, 0x2559, 0x2570, 0xff0e, 0x3002,
    0xfe52, 0x2027, 0x00b7, 0x3121, 0x00f7, 0x2252, 0x2235, 0x2234, 0x2299, 0x2026, 0x2025, 0xff1e,
    0xfe65, 0x3009, 0x2267, 0x2192, 0x300b, 0xfe3e, 0x2518, 0x256f, 0x255d, 0x255b, 0x255c, 0x2198,
    0x2197, 0xfe40, 0x22bf, 0x2524, 0x2561, 0x2563, 0x2561, 0x2562, 0x25e2, 0x25e5, 0xff1f, 0xfe56};
constexpr std::uint16_t kKoreanBegin[0x17] = {
    0x3001, 0x3002, 0x00b7, 0x2025, 0x2026, 0x00a8, 0x3003, 0x2019, 0x201d, 0x3015, 0x3009, 0x300b,
    0x300d, 0x300f, 0x3011, 0xff09, 0xff3d, 0xff5d, 0xbca0, 0xc2a4, 0xc774, 0xd2b8, 0xd3f0};
constexpr std::uint16_t kUniversalBegin[10] = {0x0021, 0x0025, 0x0029, 0x002c, 0x002e, 0x003a, 0x003b, 0x003f, 0x005d, 0x007d};

constexpr std::uint16_t kJapaneseEnd[15] = {0x2018, 0x201c, 0x3008, 0x300a, 0x300c, 0x300e, 0x3010, 0x3014, 0xff04, 0xff08, 0xff3b, 0xff5b,
    0xff62, 0xffe1, 0xffe5};
constexpr std::uint16_t kSimplifiedChineseEnd[11] = {0x2018, 0x201c, 0x3014, 0x3008, 0x300a, 0x300c, 0x300e, 0x3016, 0x3010, 0xff08, 0xff3a};
constexpr std::uint16_t kTraditionalChineseEnd[6] = {0xfe5b, 0xfe5d, 0x2018, 0x201c, 0x301d, 0x2035};
constexpr std::uint16_t kKoreanEnd[11] = {0x2018, 0x201c, 0x3014, 0x3008, 0x300a, 0x300c, 0x300e, 0x3010, 0xff08, 0xff3b, 0xff5b};
constexpr std::uint16_t kUniversalEnd[7] = {0x24, 0x28, 0x5b, 0x5c, 0x7b, 0xa3, 0xa5};

// characters that break a word (>= 0x100 only; below 0x100 the rule is arithmetic)
constexpr std::uint16_t kBreakingCharacters[] = {
    0x82,   0x20,   0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200a, 0x200b,
    0x3000, 0x2d,   0x2010, 0x2012, 0x2013, 0x2014, 0x2015, 0x203e, 0x301c, 0xfe31, 0xfe32, 0xfe58};

template <std::size_t N>
void Insert(std::unordered_set<std::uint16_t>& set, const std::uint16_t (&table)[N]) {
    for (std::uint16_t c : table) set.insert(c);
}

const std::unordered_set<std::uint16_t>& IllegalBeginLineSet() {
    static const std::unordered_set<std::uint16_t> set = [] {
        std::unordered_set<std::uint16_t> s;
        Insert(s, kJapaneseBegin);
        Insert(s, kSimplifiedChineseBegin);
        Insert(s, kTraditionalChineseBegin);
        Insert(s, kKoreanBegin);
        Insert(s, kUniversalBegin);
        return s;
    }();
    return set;
}

const std::unordered_set<std::uint16_t>& IllegalEndLineSet() {
    static const std::unordered_set<std::uint16_t> set = [] {
        std::unordered_set<std::uint16_t> s;
        Insert(s, kJapaneseEnd);
        Insert(s, kSimplifiedChineseEnd);
        Insert(s, kTraditionalChineseEnd);
        Insert(s, kKoreanEnd);
        Insert(s, kUniversalEnd);
        return s;
    }();
    return set;
}

// the Thai class map is built from these ranges / singles (UTF16GetThaiCharType::thaiCharTypeMap)
int ThaiClass(std::uint16_t c) {
    if (c >= 0xe01 && c <= 0xe23) return 1;
    if (c == 0xe25) return 1;
    if (c >= 0xe27 && c <= 0xe2e) return 1;
    if (c >= 0xe40 && c <= 0xe44) return 2;
    if (c == 0xe30 || c == 0xe32 || c == 0xe33) return 3;
    if (c == 0xe45) return 4;
    if (c == 0xe24 || c == 0xe26) return 5;
    if (c == 0xe38) return 6;
    if (c == 0xe39) return 7;
    if (c == 0xe3a) return 8;
    if (c >= 0xe48 && c <= 0xe4b) return 9;
    if (c == 0xe4c || c == 0xe4d) return 10;
    if (c == 0xe47) return 11;
    if (c == 0xe4e) return 12;
    if (c == 0xe34) return 13;
    if (c == 0xe31 || c == 0xe36) return 14;
    if (c == 0xe35 || c == 0xe37) return 15;
    if (c >= 0xe50 && c <= 0xe59) return 16;
    if (c == 0xe2f || c == 0xe3f || c == 0xe46 || c == 0xe4f || c == 0xe5a || c == 0xe5b) return 17;
    return 0;
}

bool IsLineBreakCharacter(std::uint16_t c) {
    if (static_cast<std::int16_t>(c) < 0x2028) return c == 10 || c == 0xd || c == 0x85;
    return ((static_cast<std::int32_t>(static_cast<std::int16_t>(c)) - 0x2028) & 0xffff) < 2;
}

bool IsIdeographic(std::uint32_t c) {
    return c - 0xfe30 < 0x20 || c - 0x2e80 <= 0xb180 || c - 0xf900 < 0x200 || c - 0xff10 < 0xdf;
}

}  // namespace

int UTF16GetThaiCharType(std::uint32_t character) {
    if ((character & 0xffffff80u) != 0xe00) return 0;
    return ThaiClass(static_cast<std::uint16_t>(character));
}

int UTF16GetThaiCharLevel(std::uint32_t character, std::uint32_t previous, std::uint32_t beforePrevious) {
    static const int kMarkLevels[7] = {3, 0, 0, 0, 0, 3, 3};    // DAT_100a15300
    const std::uint32_t type = static_cast<std::uint32_t>(UTF16GetThaiCharType(character));
    switch (type) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 0x10: case 0x11: {
            int level = 4;
            if (character == 0xe33) {
                const std::uint32_t before = static_cast<std::uint32_t>(UTF16GetThaiCharType(previous));
                level = ((before | 8) == 9) ? 4 : 0;
            }
            return level;
        }
        case 6: case 7: case 8:
            return UTF16GetThaiCharType(previous) == 1 ? 5 : 0;
        case 9: case 10: case 0xb: {
            const std::uint32_t before = static_cast<std::uint32_t>(UTF16GetThaiCharType(previous));
            if (type == 9) {
                int level = 2;
                if (before - 0xd > 2 && (beforePrevious != 0xe33 || before != 1)) {
                    level = 0;
                    if (before - 1 < 7) level = kMarkLevels[before - 1];
                }
                return level;
            }
            int level = 3;
            if ((type | 1) == 0xb && (before > 7 || ((0xc2u >> (before & 0x1f)) & 1) == 0))
                level = (before - 0xd < 3) ? 2 : 0;
            return level;
        }
        case 0xc: case 0xd: case 0xe: case 0xf:
            return UTF16GetThaiCharType(previous) == 1 ? 3 : 0;
        default:
            return 1;
    }
}

bool UTF16IsAllowedToBeginLine(std::uint32_t c) {
    if (static_cast<std::int8_t>(c >> 8) == 0) {
        // Latin-1: U+00A8, U+00B0, U+00B1, U+00B7 and U+00F7 may not begin a line
        if (c - 0xa8 < 0x10 && ((0x8301u >> (c - 0xa8)) & 1)) return false;
        return c != 0xf7;
    }
    return IllegalBeginLineSet().count(static_cast<std::uint16_t>(c)) == 0;
}

bool UTF16IsAllowedToEndLine(std::uint32_t c) {
    if (static_cast<std::int8_t>(c >> 8) == 0) {
        if (c - 0x24 <= 0x38 && ((0x180000000000011ull >> ((c - 0x24) & 0x3f)) & 1)) return false;
        if (c - 0x7b <= 0x2a && ((0x50000000001ull >> ((c - 0x7b) & 0x3f)) & 1)) return false;
        return true;
    }
    return IllegalEndLineSet().count(static_cast<std::uint16_t>(c)) == 0;
}

bool IsCharWordBreaking(std::uint16_t c) {
    if (c < 0x100) {
        if (c == 0x20 || c == 0x2d) return true;
        return (c != 0xd) != (c == 10 || c > 0x20);
    }
    static const std::unordered_set<std::uint16_t> set(std::begin(kBreakingCharacters), std::end(kBreakingCharacters));
    return set.count(c) != 0;
}

int UTF16GetWord(const std::uint16_t* text, std::uint32_t count) {
    if (count == 0) return 0;
    std::uint16_t c = text[0];
    bool canEnd = UTF16IsAllowedToEndLine(c);
    bool isBreak = IsLineBreakCharacter(c);
    bool wordBreaking = IsCharWordBreaking(c);
    bool ideographic = IsIdeographic(c);
    int length = 1;
    if (c == 0xe46 || count < 2) return length;
    for (std::uint32_t i = 1; i < count; ++i) {
        const std::uint16_t next = text[i];
        const bool canBegin = UTF16IsAllowedToBeginLine(next);
        const bool nextCanEnd = UTF16IsAllowedToEndLine(next);
        const bool nextBreak = IsLineBreakCharacter(next);
        const bool nextWordBreaking = IsCharWordBreaking(next);
        const bool nextIdeographic = IsIdeographic(next);
        if (isBreak || nextBreak) return length;
        if (canEnd && canBegin) {
            // a word-breaking character followed by a non-breaking one ends the word
            if (wordBreaking && !nextWordBreaking) return length;
        }
        ++length;
        if (next == 0xe46) return length;
        isBreak = false;
        canEnd = nextCanEnd;
        ideographic = nextIdeographic;
        wordBreaking = nextWordBreaking;
    }
    (void)ideographic;
    return length;
}
