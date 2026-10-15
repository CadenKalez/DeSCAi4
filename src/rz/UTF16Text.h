// UTF-16 line-breaking helpers of the RZ text layout (0x10001decb..0x100023ea0, 6 functions): Thai character
// classes and levels, "may a line start / end with this character" tables (the union of the Japanese, Chinese,
// Korean and universal kinsoku lists), and the word splitter the font code uses to wrap text.
#pragma once
#include <cstdint>

// Thai character class of a UTF-16 code unit: 0 for anything outside U+0E00..U+0E7F or unclassified,
// 1 consonant, 2 leading vowel, 3 following vowel (and sara am), 4 U+0E45, 5 U+0E24 / U+0E26, 6..8 lower vowels
// and phinthu, 9..11 tone marks / thanthakhat / maitaikhu, 12 U+0E4E, 13..15 upper vowels, 16 digits, 17 symbols.
int UTF16GetThaiCharType(std::uint32_t character);
// Vertical level (0..5) at which a combining Thai mark sits, given the character before it and the one two back.
int UTF16GetThaiCharLevel(std::uint32_t character, std::uint32_t previous, std::uint32_t beforePrevious);
bool UTF16IsAllowedToBeginLine(std::uint32_t character);
bool UTF16IsAllowedToEndLine(std::uint32_t character);
// Length (in code units, at least 1 for non-empty text) of the word starting at `text`.
int UTF16GetWord(const std::uint16_t* text, std::uint32_t count);
bool IsCharWordBreaking(std::uint16_t character);
