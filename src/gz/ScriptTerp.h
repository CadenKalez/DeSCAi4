// cGZScriptTerp — the window-script interpreter of the GZ UI layer.
// 30 symbols, 0x10054e6e4..0x10054f7b0 (primary vtable 0x100acb6b0). It runs a UI script through an
// eleven-state machine: the text is tokenised ("<", ">", "=", bare words, quoted strings and the literal
// text between tags) and the token stream is reported to a cIGZScriptTarget, which either interprets the
// tags live (cIGZScriptInterpretingTarget: BeginExecuteTag / ApplyProperty / EndExecuteTag) or collects
// them into control descriptions (cIGZScriptCompilingTarget: ScriptTag with the property list). The
// per-control counterpart that turns such a property list into a live window is cGZLegacyControl
// (src/gz/LegacyControl.{h,cpp}); the service that owns the interpreter is cGZUIScriptService.
//
// Object layout (0xa0 bytes; the offsets are the original's):
//   +0x00 cIGZScriptTerp vptr (iid 0x5a2c1570 — cGZUIScriptService::GetScriptInterpreter keeps this)
//   +0x08 u32 reference count, shared by the interface
//   +0x10 cIGZScriptTarget*              the target, AddRef'd (iid 0x1a2bdeb8)
//   +0x18 cIGZScriptInterpretingTarget*  target->QueryInterface(0x1a2bdeb9), AddRef'd
//   +0x20 cIGZScriptCompilingTarget*     target->QueryInterface(0x1a2bdeba), AddRef'd
//   +0x28 std::vector<cIGZScriptVocab*>  vocabulary stack: begin, +0x30 end, +0x38 capacity
//   +0x40 cIGZVariant*                   execution context (GetContext borrows it; see the quirks)
//   +0x48 int state: 0 idle, 1 text, 2 open tag, 3 parameters, 4 after a property name, 5 after '=',
//         6 skip to '>', 7 after an unknown property name, 8 after '=' of an unknown name, 10 error
//         (9 is never entered)
//   +0x50 const char* script text: the caller's string data (ExecuteScriptString) or the buffer
//         ExecuteScriptFile allocated; it is what ExecuteScript starts parsing from
//   +0x58 const char* cursor into +0x50
//   +0x60 bool a tag is open (BeginExecuteTag fired, ScriptTag not yet)
//   +0x64 u32 current tag id
//   +0x68 bool a property name is pending (the value may still be missing)
//   +0x6c u32 pending property id
//   +0x70 cIGZVariant* pending property value (one counted reference)
//   +0x78 cGZArray<std::pair<u32, cIGZVariant*>> parameters: begin, +0x80 end, +0x88 capacity,
//         +0x98 the iid allocator (0x28 bytes). NOT recovered as a class: modelled as the shared
//         tScriptPropertyArray (std::vector) of LegacyControl.h, like cGZLegacyControl does, so both
//         halves of the script system speak the same property list.
//
// The state machine (ExecuteScript @ 0x10054ecf0), one iteration per token:
//   1 text     type 0/1 -> target->ScriptLiteral; '<' -> 2; '>' / '=' -> 10
//   2 <tag>    a word -> parseOpenTag (id resolved through the vocabulary); ok -> 3, else -> 6;
//              '>' -> 1 (an empty "<>"); anything else -> 10
//   3 params   a word: flush the pending property, resolve the word as a property id; resolved -> 4,
//              otherwise -> 7; '>' -> flush + parseCloseTag -> 1; anything else -> 10
//   4 name     same as 3 for a word; '=' -> 5; '>' -> flush + parseCloseTag -> 1; else -> 10
//   5 = value  a quoted string/literal -> parseParmStrValue -> 3; a word -> parseParmValue -> 3; else -> 10
//   6 skip     everything up to '>' (state 1); a failed/invalid tag name lands here
//   7 unknown  like 3 but without the flush (nothing is pending); '=' -> 8; '>' -> parseCloseTag -> 1
//   8 =        the value of an unknown property is swallowed: word/string -> 3, else -> 10
//   9/10       leave the loop; 10 is the error state ExecuteScript reports as ScriptEnd(false)
//
// Known quirks kept (addresses are the originals):
//  * A script has no closing tags: every '<' must open a tag whose name resolves to an id, so "</x>" and
//    "<unknown>" are silently skipped up to the next '>' (state 6) and the text after them is literal —
//    0x10054eed8 (parseOpenTag failing) and 0x10054eee4.
//  * A property whose name does not resolve (an ordinary word that the vocabulary does not know, and that
//    does not start with a digit) and its "=value" are dropped: state 7/8 only resumes at the next word
//    (0x10054ee63). Only numeric / vocabulary-resolved property ids reach the target.
//  * GetUint32 only accepts a *string* that starts with an ASCII digit (0x10054f717, the ctype _CTYPE_D
//    bit), then parses it with RZConvertToUintPtrA — so "0x10" is 16, "5abc" is 5, and "+5" or "-5" are
//    rejected unless the vocabulary translates them.
//  * GetUint32 on a vocabulary hit requires the translation to be exactly kTypeUint32; a string-typed
//    translation fails the lookup (0x10054f6da).
//  * GetToken releases its output string but does not clear the caller's pointer when the script ends
//    right after an opening quote (0x10054f3b8) — the original never dereferences it; the port nulls it.
//  * ExecuteScriptString does not copy the script: +0x50 keeps a pointer into the caller's string, and
//    ExecuteScriptFile deletes its buffer without clearing +0x50 (0x10054eca9).
//  * ExecuteScriptFile treats cRZFile::Length() == 0xffffffff (an error) as a non-empty file and tries
//    to allocate 4 GB (0x10054ec7a compares the raw int against 0).
//  * ExecuteScriptResource dereferences the persistence service without a null check (0x10054f031); the
//    port returns false instead. It skips the record's own Release when the 0xc56b8f08 QueryInterface
//    fails (0x10054f0d3 jumps straight to CloseDBRecord), which is harmless only because CloseDBRecord
//    releases the caller's reference (cGZPersistResourceManager::CloseDBRecord).
//  * The destructor releases the pending property value and the target but never the context (+0x40 is
//    not touched by 0x10054e818) and drops the parameter array without releasing its variants. Kept: the
//    test asserts the context's reference count survives the interpreter.
//  * SetContext releases the old context before AddRef'ing the new one, so passing the current context
//    again with a count of one frees it (0x10054ea00).
//  * Release destroys the object when the count is 1 *or* 0 (0x10054f789 compares against 2).
//  * PopVocabulary refuses to pop the interpreter's own vocabulary (it needs size >= 2, 0x10054eb56).
#pragma once
#include <cstdint>
#include <vector>

#include "gz/LegacyControl.h"   // tScriptProperty / tScriptPropertyArray (the shared cGZArray pair list)
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "gz/cIGZVariant.h"

class cGZScriptTerp;

constexpr std::uint32_t kGZIID_cIGZScriptTerp = 0x5a2c1570;
constexpr std::uint32_t kGZIID_cIGZScriptTarget = 0x1a2bdeb8;
constexpr std::uint32_t kGZIID_cIGZScriptInterpretingTarget = 0x1a2bdeb9;
constexpr std::uint32_t kGZIID_cIGZScriptCompilingTarget = 0x1a2bdeba;
constexpr std::uint32_t kGZIID_cIGZScriptVocab = 0x1a2bdebb;

// ---------------------------------------------------------------------------------------------------------
// cGZScriptVocab (13 symbols, 0x10054ddb4..0x10054e6d6) is the unrecovered keyword table: an
// associative, case-insensitive cRZString -> cIGZVariant* map (l_stricmp @ 0x10054ddb0). Only the calls the
// interpreter makes are modelled, in the original's slot order (vtable 0x100acb640):
class IScriptVocab : public cIGZUnknown {
public:
    // [3] 0x18: stores `value` under `name` (taking a reference) and returns whether it replaced an entry.
    virtual bool AddTranslation(const cIGZString& name, cIGZVariant* value) = 0;
    // [4] 0x20: same for a uint32 value (cGZScriptVocab wraps it in a cRZVariant).
    virtual bool AddTranslation(const cIGZString& name, std::uint32_t value) = 0;
    // [5] 0x28: removes the entry and releases its value; true when there was one.
    virtual bool RemoveTranslation(const cIGZString& name) = 0;
    // [6] 0x30: true and a COUNTED reference in *out when found (the caller releases it); *out is left
    // alone when the name is unknown.
    virtual bool GetTranslation(const cIGZString& name, cIGZVariant** out) = 0;
    // [7] 0x38: releases every value.
    virtual void Clear() = 0;
    // [8],[9] 0x40/0x48 the concrete class' destructors (cGZScriptVocab is not an interface).
    virtual ~IScriptVocab() = default;
};

// ---------------------------------------------------------------------------------------------------------
// The three target interfaces. cGZUIScriptService implements all of them, so its sub-vtables show the exact
// method order (0x100acb438 / 0x100acb478 / 0x100acb4b8); none of them declares a destructor (cIGZUnknown's
// is protected and non-virtual), which is why the methods start at [3] and stop after three entries.
class cIGZScriptTarget : public cIGZUnknown {
public:
    virtual bool ScriptBegin(cGZScriptTerp* terp) = 0;                         // [3] 0x18
    virtual bool ScriptEnd(cGZScriptTerp* terp, bool success) = 0;             // [4] 0x20
    virtual bool ScriptLiteral(cGZScriptTerp* terp, cIGZString* text) = 0;     // [5] 0x28
};

class cIGZScriptInterpretingTarget : public cIGZUnknown {
public:
    virtual bool BeginExecuteTag(cGZScriptTerp* terp, std::uint32_t tag) = 0;                    // [3] 0x18
    virtual bool EndExecuteTag(cGZScriptTerp* terp, std::uint32_t tag) = 0;                      // [4] 0x20
    virtual bool ApplyProperty(cGZScriptTerp* terp, std::uint32_t id, cIGZVariant* value) = 0;   // [5] 0x28
};

class cIGZScriptCompilingTarget : public cIGZUnknown {
public:
    // The parameter list is the interpreter's property array; its variants stay owned by the interpreter.
    virtual bool ScriptTag(cGZScriptTerp* terp, std::uint32_t tag, tScriptPropertyArray& parameters) = 0;  // [3] 0x18
};

// cIGZScriptTerp (iid 0x5a2c1570): the interface the class implements. The destructors 0x78/0x80 follow
// these thirteen methods, as in the original's vtable.
class cIGZScriptTerp : public cIGZUnknown {
public:
    virtual bool SetScriptTarget(cIGZScriptTarget* target) = 0;                       // [3] 0x18
    virtual bool SetContext(cIGZVariant* context) = 0;                                // [4] 0x20
    virtual cIGZVariant* GetContext() = 0;                                            // [5] 0x28
    virtual bool ExecuteScriptString(const cIGZString& script) = 0;                   // [6] 0x30
    virtual bool ExecuteScriptFile(const cIGZString& path) = 0;                       // [7] 0x38
    virtual bool ExecuteScriptResource(const cGZPersistResourceKey& key) = 0;         // [8] 0x40
    virtual bool AddToken(const cIGZString& name, cIGZVariant* value) = 0;            // [9] 0x48
    virtual bool AddToken(const cIGZString& name, std::uint32_t value) = 0;           // [10] 0x50
    virtual bool RemoveToken(const cIGZString& name) = 0;                             // [11] 0x58
    virtual bool RemoveAllTokens() = 0;                                              // [12] 0x60
    virtual bool PushVocabulary(IScriptVocab* vocab) = 0;                             // [13] 0x68
    virtual bool PopVocabulary() = 0;                                                // [14] 0x70
};

// Everything the interpreter needs from its unrecovered neighbours. One method per original call; the
// comment gives the original address.
class IScriptTerpHost {
public:
    virtual ~IScriptTerpHost() = default;
    // The constructor's own vocabulary: `operator new(0x28)` + cGZScriptVocab::cGZScriptVocab
    // (0x10054ddf4), with the creation reference (count 1). The original always allocates one; without a
    // host the port's vocabulary stack stays empty.
    virtual IScriptVocab* CreateVocabulary() = 0;
};

IScriptTerpHost* ScriptTerpHost();
void SetScriptTerpHost(IScriptTerpHost* host);

// ---------------------------------------------------------------------------------------------------------
class cGZScriptTerp : public cIGZScriptTerp {
public:
    // tagRZVariant types the token stream is made of (cGZScriptTerp::eTType in the original; the values are
    // what GetToken stores through its eTType& argument).
    enum eTType : std::uint32_t {
        kTTypeLiteral = 0,   // literal text between tags, or a quoted string, or the value after '='
        kTTypeToken = 1,     // a bare word (tag name, property name, number, unquoted value)
        kTTypeOpen = 2,      // '<'
        kTTypeClose = 3,     // '>'
        kTTypeEquals = 4,    // '='
    };

    cGZScriptTerp();                                        // 0x10054e6e4
    virtual ~cGZScriptTerp();                               // 0x10054e818

    // cIGZUnknown
    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x10054f756
    std::uint32_t AddRef() override;                               // 0x10054f778
    std::uint32_t Release() override;                              // 0x10054f786

    // cIGZScriptTerp
    bool SetScriptTarget(cIGZScriptTarget* target) override;       // 0x10054e964
    bool SetContext(cIGZVariant* context) override;                // 0x10054ea00
    cIGZVariant* GetContext() override;                            // 0x10054f7a6
    bool ExecuteScriptString(const cIGZString& script) override;   // 0x10054efb8
    bool ExecuteScriptFile(const cIGZString& path) override;       // 0x10054ec00
    bool ExecuteScriptResource(const cGZPersistResourceKey& key) override;  // 0x10054efdc
    bool AddToken(const cIGZString& name, cIGZVariant* value) override;     // 0x10054ea3e
    bool AddToken(const cIGZString& name, std::uint32_t value) override;    // 0x10054ea54
    bool RemoveToken(const cIGZString& name) override;                      // 0x10054eabe
    bool RemoveAllTokens() override;                                        // 0x10054ead4
    bool PushVocabulary(IScriptVocab* vocab) override;                      // 0x10054eae8
    bool PopVocabulary() override;                                          // 0x10054eb3e

    // The parser's parts are private in the original (only ExecuteScript calls them); they are public here so
    // the test can check the ones that stand alone (GetUint32, GetLocalTranslation, parseOpenTag,
    // parseCloseTag) without going through a script.
    bool GetToken(cIGZString** out, eTType& type, bool atTopLevel);         // 0x10054f1be
    // Vocabulary lookup over the whole stack, top first; the result is a counted reference.
    bool GetLocalTranslation(const cIGZString& name, cIGZVariant** out);    // 0x10054eb7e
    // Resolves a word to a uint32: the vocabulary first, then the digits of the string itself.
    bool GetUint32(const cIGZString& text, std::uint32_t& out);             // 0x10054f6b2

    // Drives the whole state machine over +0x50. Returns false when the script ended in the error state and
    // refuses to run while another script is in progress.
    bool ExecuteScript();                                                   // 0x10054ecf0
    bool parseOpenTag(cIGZString* text);                                    // 0x10054f3d0
    bool parseFlushLastParm();                                              // 0x10054f418
    bool parseCloseTag();                                                   // 0x10054f4e6
    bool parseParmValue(cIGZString* text);                                  // 0x10054f538
    bool parseParmStrValue(cIGZString* text);                               // 0x10054f5c8
    bool ClearParams();                                                     // 0x10054f634

private:
    // The vocabulary the interpreter pushes and pops (the top of the stack; null only without a host).
    IScriptVocab* CurrentVocabulary() const {
        return vocabularies_.empty() ? nullptr : vocabularies_.back();
    }

    std::uint32_t refCount_ = 0;                       // +0x08
    cIGZScriptTarget* target_ = nullptr;               // +0x10
    cIGZScriptInterpretingTarget* interpreting_ = nullptr;  // +0x18
    cIGZScriptCompilingTarget* compiling_ = nullptr;   // +0x20
    std::vector<IScriptVocab*> vocabularies_;          // +0x28 (the original's std::vector<cIGZScriptVocab*>)
    cIGZVariant* context_ = nullptr;                   // +0x40
    std::int32_t state_ = 0;                           // +0x48
    const char* scriptText_ = nullptr;                 // +0x50
    const char* cursor_ = nullptr;                     // +0x58
    bool inTag_ = false;                               // +0x60
    std::uint32_t tagId_ = 0;                          // +0x64
    bool hasParm_ = false;                             // +0x68
    std::uint32_t parmId_ = 0;                         // +0x6c
    cIGZVariant* parmValue_ = nullptr;                 // +0x70
    tScriptPropertyArray parameters_;                  // +0x78
};
