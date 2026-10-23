// cGZWinIMEProxy — the input-method (IME) proxy service (service id 0x2ac45449, priority -1000000, class id 0x2ac4516d,
// interface iid 0x2ac44ae9; 27 functions / 44 symbols with the thunks, 0x1001f7822..0x1001f8c6c, primary vtable
// 0x100a84af8). It is the bridge between the IME server and the two floating IME windows: it listens to the four IME
// messages on the message server and creates / destroys the composition window (cGZWinIMEComposition) and the
// candidate list window (cGZWinIMECandidateList, recovered in src/gz/WinIMECandidateList.h) as children of the window
// manager's main window. StartCandidateList still builds its window through the IIMEProxyHost seam, so that one
// function stays recovered-partial.
//
// Interfaces (vtable groups): +0 cIGZWinIMEProxy (iid 0x2ac44ae9; also accepts iid 1), +8 cIGZMessageTarget
// (iid 0xe98b2f57, the message-server view), +0x10 cIGZSystemService and +0x18 cRZUnknown (both from the
// cRZSystemService base at +0x10). QueryInterface answers 0x2ac44ae9/1 at +0, 0xe98b2f57 at +8 and 0x287fb697 through
// the system service; everything else is refused.
//
// Layout (binary): +0x24 service id 0x2ac45449 (inside cRZSystemService), +0x34 the proxy's own reference count,
// +0x38 a second copy of the service id (nothing reads it), +0x3c initialised, +0x40 tick counter, +0x48 message server,
// +0x50 / +0x54 a composition position override (0x7fffffff = none), +0x58 focus probe suppressed (never written by
// this class), +0x60 IME server, +0x68 window manager, +0x70 the focus window seen on the previous tick (borrowed),
// +0x78 composition window enabled (starts true), +0x80 composition window, +0x88 composition font, +0x90 candidate list
// window enabled (starts true), +0x98 candidate list window, +0xa0 candidate list font.
//
// Quirks kept (see the test):
//  * Release() destroys the object whenever the count is below 2, so a freshly built (count 0) or once-AddRef'd proxy
//    is deleted by the first Release (0x1001f7b94).
//  * The +0x38 field is a duplicate of the service id; nothing in the class reads it (the store at 0x1001f78a9).
//  * SetupDefaultFonts acquires TWO fonts (one per window) with the same name / size 0x12 / style 0 / reuse, and only
//    for a window whose font is still missing (0x1001f8197 / 0x1001f81c6).
//  * Shutdown releases the message server, IME server, window manager and both fonts but NOT the two floating windows;
//    they are only released by the destructor (0x1001f7ef8 vs 0x1001f7912).
//  * OnTick tests +0x58 before it probes the focus window; the class never sets that byte (0x1001f888c).
//  * OnTick sends 0x0be10a20 {p1 = 0, p2 = 2, p3 = 2} and then tests p1 again; a listener that answers (p1 != 0) makes
//    it keep p2 / p3 as a composition position override, otherwise composition *enabled* is turned off (0x1001f894f).
//  * StartComposition / StartCandidateList release any existing window before creating one (0x1001f831e / 0x1001f8604)
//    after an early return that makes that release unreachable.
//
// Seams: the framework services are looked up through cRZSysServPtr (the resolver stands in for
// RZGetFramework()->GetSystemService, 0x10002ab30); the framework itself registers the service for ticks
// (RZGetFramework()->AddToTick / RemoveFromTick, vslots 0x48 / 0x50); the windows are built through
// GZCOM()->CreateInstance (vslot 0x18). The fallback construction the binary inlines in StartCandidateList (the
// 0x208-byte cGZWinIMECandidateList object with a cGZWinGen at +8, constructor 0x1001f8abc) goes through
// IIMEProxyHost; the class itself is recovered, so replacing that call with `new cGZWinIMECandidateList(winMgr.get())`
// the way StartComposition builds cGZWinIMEComposition finishes StartCandidateList and removes the seam.
//
// Deviations (the binary dereferences without a check, this version guards): the window manager in
// StartComposition / StartCandidateList / EndComposition / EndCandidateList, the focus window and the cached message
// server in OnTick. The recovered cGZWinTextEdit needs a window manager at construction, so the manual composition-window
// fallback passes the manager the binary fetched just above for the main window (the binary's
// cGZWinIMEComposition constructor 0x1001f8a04 takes no argument).
#pragma once
#include <cstdint>

#include "gz/Font.h"
#include "gz/WinInterfaces.h"
#include "gz/WinIMEComposition.h"
#include "gz/WinIMECandidateList.h"
#include "gz/WinIMEServer.h"
#include "gz/cGZMessageServer.h"
#include "rz/cRZSystemService.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinIMEProxy = 0x2ac44ae9;
constexpr std::uint32_t kGZCLSID_cGZWinIMEProxy = 0x2ac4516d;      // AppStartup's createAndRegister
constexpr std::uint32_t kGZSID_cGZWinIMEProxy = 0x2ac45449;
constexpr std::int32_t kGZWinIMEProxyPriority = -1000000;
// OnTick's position probe to the message server (p1 = 0, p2 = p3 = 2).
constexpr std::uint32_t kGZMsgIMEProxyPositionProbe = 0x0be10a20;
constexpr std::uint32_t kGZIMEProxyNoPosition = 0x7fffffff;

// The IME proxy interface (iid 0x2ac44ae9, primary vtable group 0 @ 0x100a84af8).
class cIGZWinIMEProxy : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                            // [3]  +0x18
    virtual bool Shutdown() = 0;                                                        // [4]  +0x20
    virtual bool GetCompositionWinEnabled() const = 0;                                  // [5]  +0x28
    virtual void SetCompositionWinEnabled(bool enabled) = 0;                            // [6]  +0x30
    virtual cIGZWinIMEComposition* GetCompositionWin() const = 0;                       // [7]  +0x38
    virtual cIGZFont* GetCompositionFont() const = 0;                                   // [8]  +0x40
    virtual void SetCompositionFont(cIGZFont* font) = 0;                                // [9]  +0x48
    virtual bool GetCandidateListWinEnabled() const = 0;                                // [10] +0x50
    virtual void SetCandidateListWinEnabled(bool enabled) = 0;                          // [11] +0x58
    virtual cIGZWinIMECandidateList* GetCandidateListWin() const = 0;                   // [12] +0x60
    virtual cIGZFont* GetCandidateListFont() const = 0;                                 // [13] +0x68
    virtual void SetCandidateListFont(cIGZFont* font) = 0;                              // [14] +0x70
    // [15] / [16]: the complete / deleting destructor entries in the binary vtable.
    virtual ~cIGZWinIMEProxy() = default;
    virtual bool DoMessage(cGZMessage& message) = 0;                                    // [17] +0x88
    virtual bool OnTick(std::uint32_t elapsedMs) = 0;                                   // [18] +0x90
};

// The candidate list construction seam: the proxy's fallback path (0x1001f8634..0x1001f86d9) allocates the 0x208-byte
// cGZWinIMECandidateList, constructs the cGZWinGen and AddRefs it; this host returns the interface with that one
// reference. The class is recovered (src/gz/WinIMECandidateList.h) but StartCandidateList is left partial (it was
// written before the class existed); constructing it directly there removes the seam.
class IIMEProxyHost {
public:
    virtual ~IIMEProxyHost() = default;
    virtual cIGZWinIMECandidateList* CreateCandidateListWindow(cIGZWinMgr* mgr) = 0;
};
IIMEProxyHost* IMEProxyHost();
void SetIMEProxyHost(IIMEProxyHost* host);

class cGZWinIMEProxy final : public cIGZWinIMEProxy, public cIGZMessageTarget, public cRZSystemService {
public:
    cGZWinIMEProxy();                                        // @ 0x1001f7822
    ~cGZWinIMEProxy() override;                              // @ 0x1001f7912

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1001f7af8
    std::uint32_t AddRef() override;                               // @ 0x1001f7b5c
    // Quirk: a count below 2 destroys the object (0x1001f7b94).
    std::uint32_t Release() override;

    bool Init() override;                                    // @ 0x1001f7c86
    bool Shutdown() override;                                // @ 0x1001f7ef8
    bool DoMessage(cGZMessage& message) override;            // @ 0x1001f825e
    bool OnTick(std::uint32_t elapsedMs) override;           // @ 0x1001f8832

    bool GetCompositionWinEnabled() const override;          // @ 0x1001f8b44
    void SetCompositionWinEnabled(bool enabled) override;    // @ 0x1001f8b4e
    cIGZWinIMEComposition* GetCompositionWin() const override;   // @ 0x1001f8b64
    cIGZFont* GetCompositionFont() const override;           // @ 0x1001f8b72
    void SetCompositionFont(cIGZFont* font) override;        // @ 0x1001f8b80
    bool GetCandidateListWinEnabled() const override;        // @ 0x1001f8bd4
    void SetCandidateListWinEnabled(bool enabled) override;  // @ 0x1001f8be0
    cIGZWinIMECandidateList* GetCandidateListWin() const override;   // @ 0x1001f8bfc
    cIGZFont* GetCandidateListFont() const override;         // @ 0x1001f8c0a
    void SetCandidateListFont(cIGZFont* font) override;      // @ 0x1001f8c18

    // Internals (public for the test). The binary offsets are the ones in the layout comment above.
    void SetupDefaultFonts();                                // @ 0x1001f80c6
    void StartComposition();                                 // @ 0x1001f82a6
    void EndComposition();                                   // @ 0x1001f84e4
    void StartCandidateList();                               // @ 0x1001f8588
    void EndCandidateList();                                 // @ 0x1001f877c

    bool IsInitialized() const { return initialized_; }                    // +0x3c
    std::uint32_t TickCounter() const { return tickCounter_; }             // +0x40
    cIGZMessageServer* MessageServer() const { return messageServer_; }    // +0x48
    std::uint32_t CompositionPosX() const { return compositionPosX_; }     // +0x50
    std::uint32_t CompositionPosY() const { return compositionPosY_; }     // +0x54
    cIGZWinIMEServer* ImeServer() const { return imeServer_; }             // +0x60
    cIGZWinMgr* Manager() const { return winMgr_; }                        // +0x68
    cIGZWin* PreviousFocus() const { return previousFocus_; }              // +0x70 (borrowed)

private:
    // The compiler packs this reference count into the tail padding of cRZSystemService exactly like the original, so
    // every member below lands on its binary offset (sizeof is 0xa8, the first free byte after cRZSystemService +0x30).
    std::uint32_t refCount_ = 0;                             // +0x34
    std::uint32_t serviceIdCopy_ = kGZSID_cGZWinIMEProxy;    // +0x38; the original stores it twice, nothing reads it
    bool initialized_ = false;                               // +0x3c
    std::uint32_t tickCounter_ = 0;                          // +0x40
    cIGZMessageServer* messageServer_ = nullptr;             // +0x48 (reference held)
    std::uint32_t compositionPosX_ = kGZIMEProxyNoPosition;  // +0x50
    std::uint32_t compositionPosY_ = kGZIMEProxyNoPosition;  // +0x54
    bool focusProbeSuppressed_ = false;                      // +0x58 (never written)
    cIGZWinIMEServer* imeServer_ = nullptr;                  // +0x60 (reference held)
    cIGZWinMgr* winMgr_ = nullptr;                           // +0x68 (reference held)
    cIGZWin* previousFocus_ = nullptr;                       // +0x70 (borrowed)
    bool compositionWinEnabled_ = true;                      // +0x78
    cIGZWinIMEComposition* compositionWin_ = nullptr;        // +0x80 (reference held)
    cIGZFont* compositionFont_ = nullptr;                    // +0x88 (reference held)
    bool candidateListWinEnabled_ = true;                    // +0x90
    cIGZWinIMECandidateList* candidateListWin_ = nullptr;    // +0x98 (reference held)
    cIGZFont* candidateListFont_ = nullptr;                  // +0xa0 (reference held)
};

}  // namespace gz
