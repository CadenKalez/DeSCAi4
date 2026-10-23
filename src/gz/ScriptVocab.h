// cGZScriptVocab — the keyword table of the UI script interpreter (12 functions at 0x10054ddf4..0x10054e6d6, vtable
// 0x100acb640, iids 1 and 0x1a2bdebb). A case-insensitive (strcasecmp) map from keyword to cIGZVariant*; the table
// holds one reference on every value. GZCOM class 0xba2e7954 builds one for the interpreter's vocabulary stack and the
// script controls share one (cGZLegacyControl's spLocalVocab).
//
// Layout: +8 reference count (starts at 0), +0x10 the std::map (header +0x18, size +0x20).
// AddTranslation never replaces: it answers true when the name was new, false (and drops the value it made, or leaves a
// caller's variant alone) when it already existed. Release deletes the table when the count is 1 or less.
#pragma once
#include <cstdint>
#include <map>
#include <string>

#include <strings.h>

#include "gz/ScriptTerp.h"

class cGZScriptVocab : public IScriptVocab {
public:
    cGZScriptVocab() = default;     // @ 0x10054ddf4
    ~cGZScriptVocab() override;     // @ 0x10054de1e (+0x10054de7c / 0x10054de86 aliases)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10054e698
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x10054e6ba
    std::uint32_t Release() override;                              // @ 0x10054e6c8
    bool AddTranslation(const cIGZString& name, cIGZVariant* value) override;     // @ 0x10054e156
    bool AddTranslation(const cIGZString& name, std::uint32_t value) override;    // @ 0x10054deb4
    bool RemoveTranslation(const cIGZString& name) override;       // @ 0x10054e3ce
    bool GetTranslation(const cIGZString& name, cIGZVariant** out) override;      // @ 0x10054e4ec
    void Clear() override;                                         // @ 0x10054e60e

    std::size_t Size() const { return table_.size(); }

private:
    struct Less {   // cGZScriptVocab::l_stricmp @ 0x10054ddb0: strcasecmp(a, b) < 0
        bool operator()(const std::string& a, const std::string& b) const { return ::strcasecmp(a.c_str(), b.c_str()) < 0; }
    };
    std::uint32_t refCount_ = 0;                                   // +8
    std::map<std::string, cIGZVariant*, Less> table_;              // +0x10
};
