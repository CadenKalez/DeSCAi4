// cGZScriptableControlSet — the set of controls a UI script builds (18 functions at 0x10054c0fa..0x10054c68c plus
// compiler aliases and thunks, vtable 0x100acb210). The UI script service creates one per script (GZCOM class
// 0x5a354595), inserts every control it makes, hands the set to the caller and, for CreateWindowFromScript, takes the
// window of the control with the requested id out of it.
//
// Vtable groups: +0 cIGZScriptableControlSet (iid 0x1a35608a and 1), +8 cIGZRenderControlSet (iid 0xfa35615f: Render /
// ControlEvent), then +0x10 the cGZArray<cIGZScriptableControl*> (begin, end, capacity), +0x30 its allocator and +0x40
// the reference count. The set holds one reference on every control it contains.
//
// cGZArray is not recovered as a class (see CLASS-cGZArray in notes/todo.csv): the port keeps the controls in a
// std::vector, which has the same order and growth-independent behaviour the set relies on; AsGZArray answers the
// vector's address where the original answered the array subobject at +0x10.
//
// Quirks kept: Render only draws controls that answer iid 0x5a356159 (cIGZControl); ControlEvent walks the controls
// from the last to the first, hit-tests each against its area (inclusive on every edge) and stops at the first that
// handles the event, but always answers false; ThroughClass keeps asking every control (the interface query and the
// release happen for all of them) even after one refused, and answers whether none did.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/LegacyControl.h"
#include "gz/UIScriptService.h"

// The +8 view: what the draw loop calls (cIGZRenderControlSet).
class cIGZRenderControlSet : public cIGZUnknown {
public:
    virtual bool Render(cIGZGDriver* driver) = 0;                         // [3] 0x18
    virtual bool ControlEvent(cRZPoint& point, std::uint32_t event) = 0;   // [4] 0x20
protected:
    ~cIGZRenderControlSet() = default;
};
constexpr std::uint32_t kGZIID_cIGZRenderControlSet = 0xfa35615f;
constexpr std::uint32_t kGZIID_cIGZControl = 0x5a356159;

class cGZScriptableControlSet : public cIGZScriptableControlSet, public cIGZRenderControlSet {
public:
    cGZScriptableControlSet();                  // @ 0x10054c0fa
    virtual ~cGZScriptableControlSet();         // @ 0x10054c15a (+0x10054c24c / 0x10054c256 aliases)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10054c284 (+ thunk)
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x10054c2c6
    std::uint32_t Release() override;                              // @ 0x10054c2e2
    void* AsGZArray() override { return &controls_; }              // @ 0x10054c31e
    bool Insert(cIGZScriptableControl* control) override;          // @ 0x10054c328
    cIGZScriptableControl* GetByID(std::uint32_t id) override;     // @ 0x10054c37e
    cIGZScriptableControl* GetByIndex(std::uint32_t index) override;   // @ 0x10054c3f6
    cIGZWin* GetByIDAsIGZWin(std::uint32_t id) override;           // @ 0x10054c41a
    cIGZWin* GetByIndexAsIGZWin(std::uint32_t index) override;     // @ 0x10054c46c
    void SetOwnControls(bool own) override;                        // @ 0x10054c4be
    std::uint32_t GetSize() override { return std::uint32_t(controls_.size()); }   // @ 0x10054c520
    bool ThroughClass(std::uint32_t classId, cIGZVariant* value) override;   // @ 0x10054c532
    bool Render(cIGZGDriver* driver) override;                     // @ 0x10054c5e4
    bool ControlEvent(cRZPoint& point, std::uint32_t event) override;   // @ 0x10054c68c

private:
    // The original's vtable thunks for the +8 view share these; AddRef / Release / QueryInterface above serve both.
    std::vector<cIGZScriptableControl*> controls_;   // +0x10
    std::uint32_t refCount_ = 0;                     // +0x40
};
