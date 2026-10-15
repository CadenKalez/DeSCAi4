#include <cstdio>
#include <string>

#include "rz/UTF16Text.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    // Thai character classes
    CHECK(UTF16GetThaiCharType('a') == 0 && UTF16GetThaiCharType(0xe00) == 0 && UTF16GetThaiCharType(0xe7f) == 0);
    CHECK(UTF16GetThaiCharType(0xe01) == 1 && UTF16GetThaiCharType(0xe2e) == 1 && UTF16GetThaiCharType(0xe25) == 1);
    CHECK(UTF16GetThaiCharType(0xe24) == 5 && UTF16GetThaiCharType(0xe26) == 5);
    CHECK(UTF16GetThaiCharType(0xe40) == 2 && UTF16GetThaiCharType(0xe44) == 2 && UTF16GetThaiCharType(0xe45) == 4);
    CHECK(UTF16GetThaiCharType(0xe30) == 3 && UTF16GetThaiCharType(0xe33) == 3);
    CHECK(UTF16GetThaiCharType(0xe38) == 6 && UTF16GetThaiCharType(0xe39) == 7 && UTF16GetThaiCharType(0xe3a) == 8);
    CHECK(UTF16GetThaiCharType(0xe48) == 9 && UTF16GetThaiCharType(0xe4b) == 9 && UTF16GetThaiCharType(0xe4c) == 10);
    CHECK(UTF16GetThaiCharType(0xe34) == 13 && UTF16GetThaiCharType(0xe31) == 14 && UTF16GetThaiCharType(0xe35) == 15);
    CHECK(UTF16GetThaiCharType(0xe50) == 16 && UTF16GetThaiCharType(0xe59) == 16 && UTF16GetThaiCharType(0xe5b) == 17);
    CHECK(UTF16GetThaiCharType(0xe3b) == 0 && UTF16GetThaiCharType(0xe5c) == 0);

    // levels
    CHECK(UTF16GetThaiCharLevel(0xe01, 0, 0) == 4 && UTF16GetThaiCharLevel('a', 0, 0) == 4);
    CHECK(UTF16GetThaiCharLevel(0xe33, 0xe48, 0) == 4 && UTF16GetThaiCharLevel(0xe33, 0xe01, 0) == 4 &&
          UTF16GetThaiCharLevel(0xe33, 'a', 0) == 0);
    CHECK(UTF16GetThaiCharLevel(0xe38, 0xe01, 0) == 5 && UTF16GetThaiCharLevel(0xe38, 'a', 0) == 0);
    CHECK(UTF16GetThaiCharLevel(0xe34, 0xe01, 0) == 3 && UTF16GetThaiCharLevel(0xe34, 'a', 0) == 0);
    CHECK(UTF16GetThaiCharLevel(0xe48, 0xe34, 0) == 2);           // tone mark above an upper vowel
    CHECK(UTF16GetThaiCharLevel(0xe48, 0xe01, 0) == 3);           // consonant: table value
    CHECK(UTF16GetThaiCharLevel(0xe48, 0xe01, 0xe33) == 2);       // sara am before the consonant: stays high
    CHECK(UTF16GetThaiCharLevel(0xe4c, 'a', 0) == 0 && UTF16GetThaiCharLevel(0xe47, 0xe01, 0) == 3);
    CHECK(UTF16GetThaiCharLevel(0xe3e, 0, 0) == 4);               // unclassified: like a consonant

    // kinsoku
    CHECK(!UTF16IsAllowedToBeginLine(0x3001) && !UTF16IsAllowedToBeginLine(0xb0) && !UTF16IsAllowedToBeginLine(0xf7));
    CHECK(UTF16IsAllowedToBeginLine('a') && UTF16IsAllowedToBeginLine(0x4e00) && UTF16IsAllowedToBeginLine(0x21) && !UTF16IsAllowedToBeginLine(0x3002));   // Latin-1 uses its own rule
    CHECK(!UTF16IsAllowedToEndLine(0x24) && !UTF16IsAllowedToEndLine(0x28) && !UTF16IsAllowedToEndLine(0xa5));
    CHECK(!UTF16IsAllowedToEndLine(0x201c) && !UTF16IsAllowedToEndLine(0x3008) && UTF16IsAllowedToEndLine('a'));
    CHECK(UTF16IsAllowedToEndLine(0x4e00) && !UTF16IsAllowedToEndLine(0xff08));

    // word breaking characters
    CHECK(IsCharWordBreaking(' ') && IsCharWordBreaking('-') && IsCharWordBreaking(0x2003) && IsCharWordBreaking(0x3000));
    CHECK(!IsCharWordBreaking('a') && !IsCharWordBreaking('\n') && !IsCharWordBreaking('\r') && IsCharWordBreaking('\t'));
    CHECK(!IsCharWordBreaking(0x4e00) && IsCharWordBreaking(0xfe31));

    // words
    auto word = [](const char16_t* s, std::uint32_t n) { return UTF16GetWord(reinterpret_cast<const std::uint16_t*>(s), n); };
    CHECK(word(u"", 0) == 0);
    CHECK(word(u"a", 1) == 1);
    CHECK(word(u"abc def", 7) == 4);                         // the word plus the space that follows is one unit
    CHECK(word(u"a\nb", 3) == 1);                            // a line break ends the word
    CHECK(word(u"\nb", 2) == 1);
    CHECK(word(u"ๆก", 2) == 1);                    // maiyamok ends a word by itself
    CHECK(word(u"กๆข", 3) == 2);
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
