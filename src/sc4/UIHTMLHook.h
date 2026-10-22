// cSC4UIHTMLHook (0x1001f1c7c..0x1001f22a8, iid 0xc49b192c at +0x10) — the in-game HTML viewer's document source for
// "sc4://" links: documents are resources of type 0x856ddbac read straight from the resource manager.
//
// URL forms (after the "sc4://" prefix, '/'-separated, empty parts ignored):
//   html/<instance>           group 0x0a133fc0       image/<instance>   group 0xca133ecb      (the first part, case-folded)
//   <part>/<group>/<instance> the first part is skipped, the group is a hex number
//   any other single name     group 0
// The instance is the hex number in the remainder when it has at least 8 characters, else 0.
// Layout: +0 cRZRefCount, +0x10 the interface view, +0x18 the resource manager (reference held).
#pragma once
#include <cstdint>
#include <string>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZPersistResourceManager.h"
#include "rz/cRZRefCount.h"
#include "rz/cRZString.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4UIHTMLHook = 0xc49b192c;
constexpr std::uint32_t kHTMLResourceType = 0x856ddbac, kHTMLGroupHtml = 0x0a133fc0, kHTMLGroupImage = 0xca133ecb;

// What the hook reads of a cRZURL.
class IHTMLURL {
public:
    virtual ~IHTMLURL() = default;
    virtual const cRZString& GetFullURL() = 0;
    virtual const cRZString& GetScheme() = 0;
};

class cSC4UIHTMLHook : public cRZRefCount {
public:
    // The resource manager the original fetches from the framework in Init (iid 0x656b8efc).
    explicit cSC4UIHTMLHook(cIGZPersistResourceManager* resourceManager = nullptr);
    ~cSC4UIHTMLHook() override;

    bool QueryInterface(std::uint32_t iid, void** out);   // 1 / 0xc49b192c -> the interface view (this), referenced
    bool Init();                                           // takes (and refcounts) the resource manager
    bool Shutdown();
    bool TestURL(IHTMLURL& url);                           // scheme "sc4" (any case)
    bool LoadDocument(IHTMLURL& url, std::string& out);
    bool LoadDocument(const cGZPersistResourceKey& key, std::string& out);
    // The key a "sc4://" URL names; false when it is not one.
    static bool KeyFromURL(const std::string& fullUrl, cGZPersistResourceKey& key);

    void SetResourceManager(cIGZPersistResourceManager* manager);
private:
    cIGZPersistResourceManager* resourceManager_ = nullptr;   // +0x18
    cIGZPersistResourceManager* pending_ = nullptr;           // what Init will take
};

}  // namespace sc4
