// cSLString — a string that loads itself from the string resources (type 0x2026960b) by group / instance, and
// cADVString — the advisor flavour that reads its key from script text such as "text@0x6a231ea4" (cSLString 14 functions at
// 0x10012123a..0x100121774, cADVString 8 at 0x1003faf80..0x1004452d0).
//
// Loading looks the string up under group + the current language's offset first and falls back to the plain group when
// that lookup fails; a failed load empties the string. A successful load optionally runs the string detokenizer over
// the text and then turns every bullet (U+2022) into a zero-width space (U+200B). Quirk kept: the language-offset retry
// only happens when the offset is non-zero and the first lookup answered something other than exactly 1.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZString.h"
#include "sc4/StringDetokenizer.h"

namespace sc4 {

constexpr std::uint32_t kResourceTypeSLString = 0x2026960b;
constexpr std::uint32_t kAdvisorTextGroup = 0x6a231ea4;

// What the strings need from the running game: the language offset (language manager vslot 0x38) and the global string
// detokenizer (SL::spStringDetokenizer).
class ISLStringHost {
public:
    virtual ~ISLStringHost() = default;
    virtual int LanguageOffset() = 0;
    virtual cSCStringDetokenizer* Detokenizer() = 0;
};
ISLStringHost* SLStringHost();
void SetSLStringHost(ISLStringHost* host);

class cSLString : public cRZString {
public:
    cSLString() = default;                                                        // @ 0x10012123a
    cSLString(const cGZPersistResourceKey& key, bool convertTokens);              // @ 0x100121286
    cSLString(std::uint32_t group, std::uint32_t instance, bool convertTokens);   // @ 0x1001215e4
    ~cSLString() override = default;                                              // @ 0x100121738
    cSLString& operator=(const cSLString& other);                                 // @ 0x100121664
    cIGZString& AssignFromIGZString(const cIGZString& other) override { return cRZString::AssignFromIGZString(other); }   // @ 0x100121774

    bool Load(bool convertTokens, bool htmlClean);                                // @ 0x1001212f6
    void GetKey(cGZPersistResourceKey& out) const { out = key_; }                 // @ 0x1001216a0
    void SetKey(const cGZPersistResourceKey& key) { key_ = key; }                 // @ 0x1001216b8
    bool ConvertTokens(cSCStringDetokenizer* detokenizer, bool htmlClean);        // @ 0x1001216d0 (always answers false)

protected:
    cGZPersistResourceKey key_{kResourceTypeSLString, 0, 0};   // +0x24..+0x2c
};

class cADVString final : public cSLString {
public:
    explicit cADVString(const char* text);                                        // @ 0x1003faf80
    ~cADVString() override = default;                                             // @ 0x1003fc0c8
    cIGZString& AssignFromIGZString(const cIGZString& other) override { return cRZString::AssignFromIGZString(other); }   // @ 0x1003fc0e4
    // Reads the instance out of "text@0x%08x", "text@%08x", "text@%u" or "text@%i" (tried in that order); the group is
    // always the advisor text group. Unparsable text leaves instance 0.
    void FromScriptText(const char* text);                                        // @ 0x1003fb090
    void FromScriptText(const char* text, unsigned int);                          // @ 0x1003fb086
    static cGZPersistResourceKey KeyFromScriptText(const char* text);             // @ 0x1003fafc6 (init)
};

}  // namespace sc4
