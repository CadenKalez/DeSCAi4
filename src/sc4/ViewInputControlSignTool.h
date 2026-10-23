// cSC4ViewInputControlSignTool — the view tool that places and edits signposts (text signs) on the terrain
// (functions 0x1006ca19c..0x1006cb2e0 plus the destructor body at 0x1006cb3c2; class id inherited from
// cSC4ViewInputControl, tool id 0x8b4ce5a0). The object has a second base at +0x40 (cIGZMessageTarget, iid 0xe98b2f57) that
// receives message 0xbe10a20 ("where is the sign being edited on screen?").
// State (+0x48): 0 hovering / idle, 1 pressed on an existing sign, 2 dragging a sign, 3 editing the text.
// Layout: +0x48 state, +0x50 selected signpost (referenced), +0x58/+0x5c screen offset between the sign and the grab
// point, +0x60 current text (cRZString, string at +0x68), +0x88 text before editing (cRZString, string at +0x90), +0xb0
// framework code page, +0xb8 partial multibyte character (cRZString, string at +0xc0), +0xe0 clipboard manager.
// Quirks kept:
//  * Init writes the 64-bit constant 0x6b72fecf8b4ce5a0 at +0x10, i.e. tool id 0x8b4ce5a0 AND cursor id 0x6b72fecf.
//  * A click while editing (state 3) only ends the edit mode; it neither deselects nor removes an empty sign.
//  * OnKeyDown in state 3 returns false for Escape (which restores the saved text but keeps editing) and true for every
//    other key; Ctrl+V (key 0x56 with (mods & 7) == 2) pastes the clipboard text through AppendText.
//  * The sign text is limited to 0x21 glyphs and "\n" characters are dropped; a "\n" still counts as a glyph.
//  * Mouse down on an existing sign plays sound 0x2c16da45; creating a sign and finishing a drag/click play 0x2c16da29.
//  * DoMessage always returns false (it only fills the message parameters).
//  * The y of a new sign is max(terrain altitude, sea level) (maxss: the sea level wins only when strictly greater).
//  * OnMouseMove in the hover state dereferences the renderer without a null check (the original did); the drag path
//    and OnMouseDownL check it.
#pragma once
#include <cstdint>

#include "gz/WinInterfaces.h"
#include "gz/ClipboardManager.h"
#include "gz/cGZMessageServer.h"
#include "rz/cRZString.h"
#include "sc4/Occupant.h"
#include "sc4/OccupantManager.h"
#include "sc4/SignpostOccupant.h"   // the sign interface (cISC4SignpostOccupant) lives with its class
#include "sc4/ViewInputControl.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cIGZMessageTargetLegacy = 0xe98b2f57;
constexpr std::uint32_t kSignToolMessage = 0x0be10a20;   // asks the tool for the screen position of the edited sign
constexpr std::uint32_t kSignToolSoundGroup = 0xea451903;
constexpr std::uint32_t kSignToolSoundPlace = 0x2c16da29;
constexpr std::uint32_t kSignToolSoundGrab = 0x2c16da45;

// cISC4SignpostOccupant (vslots 0x18..0x38 of the sign's first vtable) is declared in sc4/SignpostOccupant.h.

// What the renderer's occupant pick hands back (vslot 0x18 returns the owning object, queried for the signpost).
class IPickedObject : public cIGZUnknown {
public:
    virtual cIGZUnknown* GetOwner() = 0;                      // 0x18
};

// SL::spRenderer
class ISignToolRenderer {
public:
    virtual ~ISignToolRenderer() = default;
    virtual bool PickObject(int x, int y, cISC4OccupantFilter* filter, IPickedObject** out) = 0;   // vslot 0x200
    virtual bool PickTerrain(int x, int y, float out[3], bool flagA, bool flagB) = 0;               // vslot 0x1e8
};

// The services behind SL::spRenderer / spTerrain / spPropManager / spPropDeveloper / spViewUtilities / spSS and the
// framework system services.
class ISignToolHost {
public:
    virtual ~ISignToolHost() = default;
    virtual ISignToolRenderer* Renderer() = 0;
    virtual float TerrainAltitude(float x, float z) = 0;              // terrain vslot 0x60
    virtual float SeaLevel() = 0;                                     // terrain vslot 0xe8
    virtual void AddProp(cISC4Occupant* occupant) = 0;                // prop manager vslot 0xa0
    virtual void RemoveProp(cISC4Occupant* occupant) = 0;             // prop manager vslot 0xe8
    virtual void SetPropDeveloperMode(int mode) = 0;                  // prop developer vslot 0x40
    virtual cISC4SignpostOccupant* CreateSignpost() = 0;              // new cSC4SignpostOccupant (0x628 bytes) with a reference count of 0; the tool adds the first reference
    virtual void ProjectToViewport(float point[3]) = 0;               // SC4ViewUtilities::ProjectPointToViewportCoordinates2D
    virtual void PlaySound(std::uint32_t group, std::uint32_t id) = 0;   // sound system vslot 0x50
    virtual cIGZMessageServer* MessageServer() = 0;                   // framework service 0x64063387 (may be null)
    virtual cIGZClipboardManager* ClipboardManager() = 0;             // framework service 0x27ebfffd (may be null)
};
ISignToolHost* SignToolHost();
void SetSignToolHost(ISignToolHost* host);

class cSC4ViewInputControlSignTool : public cSC4ViewInputControl, public cIGZMessageTarget {
public:
    enum State : int { kIdle = 0, kPressed = 1, kDragging = 2, kEditing = 3 };

    cSC4ViewInputControlSignTool();                             // @ 0x1006ca19c
    ~cSC4ViewInputControlSignTool() override;                   // @ 0x1006cb3c2 (+ 0x1006cb2d6, 0x1006cb2e0)

    // both bases are cIGZUnknown: one override serves both (thunks 0x1006ca5a0 / 0x1006ca5d0 / 0x1006ca5e8)
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1006ca57e
    std::uint32_t AddRef() override { return cSC4ViewInputControl::AddRef(); }       // @ 0x1006ca5c6
    std::uint32_t Release() override { return cSC4ViewInputControl::Release(); }     // @ 0x1006ca5de

    bool Init() override;                                       // @ 0x1006ca23c
    bool Shutdown() override;                                   // @ 0x1006ca37c
    bool OnCharacter(char c) override;                          // @ 0x1006cadec
    bool OnKeyDown(std::uint32_t key, std::uint32_t mods) override;        // @ 0x1006cb16e
    bool OnMouseDownL(int x, int y, std::uint32_t mods) override;          // @ 0x1006ca5f6
    bool OnMouseUpL(int x, int y, std::uint32_t mods) override;            // @ 0x1006ca9a0
    bool OnMouseMove(int x, int y, std::uint32_t mods) override;           // @ 0x1006caab6
    bool DoMessage(cGZMessage& message) override;               // @ 0x1006cacb4 (thunk 0x1006cadda)

    void SetSelection(cISC4SignpostOccupant* sign);             // @ 0x1006ca466
    void AppendText(const cIGZString& text);                    // @ 0x1006cb06c

    State GetState() const { return static_cast<State>(state_); }
    cISC4SignpostOccupant* Selection() const { return selection_; }
    const cRZString& Text() const { return text_; }
    const cRZString& SavedText() const { return savedText_; }
    int CodePage() const { return codePage_; }
    void SetCodePageForTest(int codePage) { codePage_ = codePage; }

private:
    int state_ = kIdle;                          // +0x48
    cISC4SignpostOccupant* selection_ = nullptr;   // +0x50
    int dragOffsetX_ = 0, dragOffsetY_ = 0;      // +0x58, +0x5c
    cRZString text_;                             // +0x60
    cRZString savedText_;                        // +0x88
    int codePage_ = 0;                           // +0xb0
    cRZString pendingChar_;                      // +0xb8
    cIGZClipboardManager* clipboard_ = nullptr;  // +0xe0
};

}  // namespace sc4
