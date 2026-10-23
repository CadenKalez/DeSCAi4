// cSC4PropOccupantAutomataController — a prop occupant that owns an automata controller created from a template named by
// its exemplar (class id / interface 0x2a4017a0, also 0x8a401788 for QueryInterface; 20 distinct functions at
// 0x1001624c0..0x100163532, most of the 64 symbols are multiple-inheritance thunks). It derives from cSC4PropOccupant (at
// +0x8; the occupant sub-object is at +0x18) and adds: +0xa8 the controller (counted), +0xb0 the controller template name
// (cRZString), +0xd0 int 0, +0xd8 "controller active" flag. Properties read in Init: 0x27812810 (3 floats, the prop's box size,
// default 16 x 16 x 16), 0xc98204b8 (controller template name; falls back to property 0x20, the exemplar name).
// A state other than 0 selects the template "<name>_<state>". Stream record: u16 version 3, the prop record, bool active,
// template name.
// Quirks kept: RestartController re-creates the controller from the manager every time and re-applies the old active flag;
// Init only looks at the properties when the occupant is not initialised yet; the manager is assumed present when a new
// controller is created (null in the original crashes); SetOrientation clamps to 0..3.
// Partial: the original SetState writes the state byte without posting the "prop state changed" message; cSC4PropOccupant's
// state_ is private so we run its SetState (which posts the message while initialised) - see PROPCTRL-1.
#pragma once
#include "sc4/AutomataController.h"
#include "sc4/PropOccupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4PropOccupantAutomataController = 0x2a4017a0;
constexpr std::uint32_t kGZIID_cISC4PropOccupantAutomataController = 0x8a401788;
constexpr std::uint32_t kPropertyPropBoxSize = 0x27812810;
constexpr std::uint32_t kPropertyControllerTemplate = 0xc98204b8;
constexpr std::uint32_t kPropertyExemplarName = 0x20;
constexpr std::uint8_t kOccupantFlagBit2 = 0x2;   // occupant flag tested before the controller is activated by Init [meaning inferred]

// SL::spCity vslot 0x178 (the automata controller manager).
class IPropControllerHost {
public:
    virtual ~IPropControllerHost() = default;
    virtual bool HasControllerManager() = 0;
    // manager vslot 0x28(occupant, templateName, 0): a controller for the occupant (not counted for us; we AddRef it).
    virtual cSC4AutomataController* CreateController(cISC4Occupant* occupant, const cIGZString& templateName) = 0;
    virtual void RemoveController(cSC4AutomataController* controller) = 0;   // manager vslot 0x40
};
IPropControllerHost* PropControllerHost();
void SetPropControllerHost(IPropControllerHost* host);

class cISC4PropOccupantAutomataController : public cIGZUnknown {
public:
    virtual cISC4Occupant* AsOccupant() = 0;                           // 0x18
    virtual cSC4PropOccupant* AsPropOccupant() = 0;                    // 0x20
    virtual void SetControllerTemplate(const char* name) = 0;          // 0x28
    virtual void ActivateController(bool active) = 0;                  // 0x30
};

class cSC4PropOccupantAutomataController : public cSC4PropOccupant, public cISC4PropOccupantAutomataController {
public:
    cSC4PropOccupantAutomataController() = default;                    // @ 0x1001624c0
    ~cSC4PropOccupantAutomataController() override;                    // @ 0x1001625a8 (deleting 0x1001626aa [free], thunks 0x100162680/8e/9c, 0x1001626e8/f6, 0x100162704)

    bool QueryInterface(std::uint32_t iid, void** out) override;       // @ 0x100162bda (7 thunks 0x100162c14..0x100162c5a)
    std::uint32_t AddRef() override { return cSC4PropOccupant::AddRef(); }     // @ 0x100162b1e (thunks 0x100162b2c..0x100162b6e)
    std::uint32_t Release() override { return cSC4PropOccupant::Release(); }   // @ 0x100162b7c (thunks 0x100162b8a..0x100162bcc)

    bool Init() override;                                              // @ 0x100162712 (thunks 0x100162910, 0x100162922)
    bool Shutdown() override;                                          // @ 0x100162934 (thunks 0x1001629b0, 0x1001629c2)
    bool Write(cIGZOStream& stream) const override;                    // @ 0x1001629d4 (thunks 0x100162a36..0x100162a60)
    bool Read(cIGZIStream& stream) override;                           // @ 0x100162a6e (thunks 0x100162ae6..0x100162b10)
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4PropOccupantAutomataController; }   // @ 0x100163504 (thunks 0x10016351a/26/32)
    void SetPosition(const float* xyz) override { cSC4PropOccupant::SetPosition(xyz); }   // @ 0x100162c68 (thunks 0x100162c7c, 0x100162c86)

    // the cISC4PropOccupant slots 0x50 / 0x58 of the interface at +0x8 are replaced by these
    bool SetOrientation(std::int32_t orientation) { return cSC4PropOccupant::SetOrientation(orientation); }   // @ 0x100162c94 (thunk 0x100162cb0): clamps to 0..3
    void SetState(std::uint32_t state);                                // @ 0x100162ccc (thunk 0x100162eb4)
    void RestartController();                                          // @ 0x100162cea

    cISC4Occupant* AsOccupant() override { return static_cast<cISC4Occupant*>(this); }              // @ 0x1001634f0 (thunk 0x100163510)
    cSC4PropOccupant* AsPropOccupant() override { return this; }                                    // @ 0x1001634fa
    void SetControllerTemplate(const char* name) override;             // @ 0x100162ed6
    void ActivateController(bool active) override;                     // @ 0x10016302a

    cSC4AutomataController* Controller() const { return controller_; }
    const cRZString& ControllerTemplateName() const { return templateName_; }
    bool ControllerActive() const { return active_; }
    std::int32_t Field0xD0() const { return field_d0_; }

private:
    cSC4AutomataController* controller_ = nullptr;   // +0xa8
    cRZString templateName_;                         // +0xb0
    std::int32_t field_d0_ = 0;                      // +0xd0
    bool active_ = false;                            // +0xd8
};

}  // namespace sc4
