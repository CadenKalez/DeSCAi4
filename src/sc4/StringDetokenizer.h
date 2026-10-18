// cSCStringDetokenizer (19 functions at 0x10005d5b4..0x10005e9c8, iid 0x8a2c3240) replaces "#Token#" markers in a
// string with values, and cSCStringDetokenizerDefault (5 functions, 0x10005e5c2..0x10005ea54) adds the game's own tokens
// (#City#, #Mayor#, #Year#, #Population#, #Region#).
//
// Matching rules kept from the original: token names are case-insensitive (strncasecmp over the shorter length, then
// the shorter one is smaller); "##" stands for a literal '#' (it is parked as a NUL while the string is split and put
// back at the end); a marker that is never closed stays in the text; a token nothing can resolve is copied through
// unchanged (and the whole call answers false); in HTML-clean mode every replacement has < > & " escaped.
// Replacement lookup order: fixed values, then the named method, then every "unknown token" method (ordered by
// function address, as the original set is).
#pragma once
#include <cstdint>
#include <map>
#include <set>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZString.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cSCStringDetokenizer = 0x8a2c3240;

using DetokenizeMethod = bool (*)(const cIGZString& token, cIGZString& out, void* context);

class cSCStringDetokenizer : public cIGZUnknown {
public:
    cSCStringDetokenizer() = default;
    virtual ~cSCStringDetokenizer();                                           // @ 0x10005d61a

    bool QueryInterface(std::uint32_t iid, void** out) override;               // @ 0x10005d696
    std::uint32_t AddRef() override { return ++refCount_; }                    // @ 0x10005d6b8
    std::uint32_t Release() override;                                          // @ 0x10005d6c6

    virtual bool Init() { return true; }                                       // @ 0x10005e7f0
    virtual bool Shutdown() { return true; }                                   // @ 0x10005e9c8
    virtual void SetTokenReplacementValue(const cIGZString& token, const cIGZString& value);   // @ 0x10005d6e2
    virtual void SetTokenReplacementMethod(const cIGZString& token, DetokenizeMethod method, void* context);   // @ 0x10005d962
    virtual void AddUnknownTokenReplacementMethod(DetokenizeMethod method, void* context, bool add);   // @ 0x10005db18
    virtual bool Detokenize(const cIGZString& in, cIGZString& out) { return PerformDetokenize(in, out); }   // @ 0x10005dbd6
    virtual bool Detokenize(const cGZPersistResourceKey& key, cIGZString& out);   // @ 0x10005dbe4
    virtual void SetHTMLCleanMode(bool on) { htmlClean_ = on; }                // @ 0x10005e5ae
    virtual bool GetHTMLCleanMode() const { return htmlClean_; }               // @ 0x10005e5b8
    virtual bool PerformDetokenize(const cIGZString& in, cIGZString& out);     // @ 0x10005df3a
    virtual bool ConvertToken(const cRZString& token, cRZString& out);         // @ 0x10005de2e

    static void MakeHTMLClean(cIGZString& text);                               // @ 0x10005dc72

    struct CaseInsensitiveStringCompare {                                      // @ 0x10005d530
        bool operator()(const cRZString& a, const cRZString& b) const;
    };
    struct tMethodDef {
        DetokenizeMethod method;
        void* context;
    };
    struct MethodLess {
        bool operator()(const tMethodDef& a, const tMethodDef& b) const {
            return std::less<void*>()(reinterpret_cast<void*>(a.method), reinterpret_cast<void*>(b.method));
        }
    };

private:
    std::uint32_t refCount_ = 0;                                                        // +8
    std::map<cRZString, cRZString, CaseInsensitiveStringCompare> values_;               // +0x10
    std::map<cRZString, tMethodDef, CaseInsensitiveStringCompare> methods_;             // +0x28
    std::set<tMethodDef, MethodLess> unknownMethods_;                                   // +0x40
    bool htmlClean_ = false;                                                            // +0x58
};

// What the default tokens read from the running game.
class IDetokenizerHost {
public:
    virtual ~IDetokenizerHost() = default;
    virtual bool CityName(cIGZString& out) = 0;                    // city vslot 0x60
    virtual bool MayorName(cIGZString& out) = 0;                   // city vslot 0x80
    virtual bool GetYear(int& year) = 0;                           // simulator vslot 0x38 (false without a simulator)
    virtual bool GetPopulation(int& population) = 0;               // residential simulator vslot 0x80
    virtual void FormatNumber(long value, cIGZString& out) = 0;    // language manager vslot 0x90
    virtual bool GetRegionName(cIGZString& out) = 0;               // SC4 vslot 0x110 -> region -> name
};
IDetokenizerHost* DetokenizerHost();
void SetDetokenizerHost(IDetokenizerHost* host);

class cSCStringDetokenizerDefault final : public cSCStringDetokenizer {
public:
    ~cSCStringDetokenizerDefault() override = default;                         // @ 0x10005e9d0
    bool Init() override;                                                      // @ 0x10005e5c2
    bool DefaultReplacementFunction(const cIGZString& token, cIGZString& out); // @ 0x10005e812
    static bool DefaultReplacementFunctionStatic(const cIGZString& token, cIGZString& out, void* self);   // @ 0x10005e7f8

private:
    cRZString city_, mayor_, year_, population_, region_;                      // +0x60, +0x88, +0xb0, +0xd8, +0x100
};

}  // namespace sc4
