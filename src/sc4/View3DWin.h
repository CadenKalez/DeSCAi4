// cSC4View3DWin — the 3D city view window (157 functions + 31 compiler thunks/aliases,
// 0x10077efc2..0x100793b3c). It is the window that owns the city camera, the input-tool stack, the
// edge/keyboard scrolling, the cursor text and cost indicator, and that hands the frame to
// cSC43DRender. It is the second half of the first-boot render path (with cSC43DRender).
//
// Vtable groups (the original object has four vtable pointers): +0 the cISC4View3DWin view (80 slots,
// 0x18..0x278), +8 the cIGZMessageTarget2 view (DoMessage(cIGZMessage2*)), +0x10 the
// cIGZWinMessageFilter view (DoMessage(cIGZWin*, cGZMessage&)), +0x18 the cGZWin / cIGZWin view.
// QueryInterface answers 1 / 0xfa47b3f9 with the view, 0x452294aa with +8, 0xc6ae7085 with +0x10 and
// 0x22ba0121 with +0x18 (0x10078145a).
//
// Layout (the cGZWin subobject is at +0x18, so its members start at +0x168):
//   +0x168 init flag (cGZWin::Init result)       +0x169 deferred-message pump enabled
//   +0x170 simulation clock (vslot 0x48 = frame counter)   +0x178 the cS3DCamera
//   +0x180 cSC4CameraControl (referenced)        +0x188 cSC43DRender (referenced)
//   +0x190 cSC4ModelMaker (referenced)           +0x198 frame-time threshold, seconds
//   +0x1a0 cRZTimer(microseconds)                +0x1b8 focus byte
//   +0x1bc last message's modifier word          +0x1c0 key accelerator (referenced)
//   +0x1c8 u16 flag pair (0x100)                 +0x1ca terrain-query enabled
//   +0x1d0 cRZAutoMsg2Notification (0x18 bytes, 132 message types)
//   +0x1e8 vector<cISC4ViewInputControl*> (the tool stack)  +0x200 the persistent tools
//   +0x218 list<cRZAutoRefCount<cIGZMessage2>> (deferred messages)  +0x228 its count
//   +0x230 window service (referenced)  +0x238 the simulator to pause while the city is hidden
//   +0x248 cRZTimer(milliseconds)        +0x260 sim-hidden pause active  +0x264 2000 ms timeout
//   +0x270..+0x4d0 the GUI windows and panel vectors (AddGUI, not recovered; RemoveGUI is)
//   +0x4d8 the current view filter information (referenced)   +0x4e0 the view level
//   +0x4e4 scroll angle   +0x4e8 scroll speed   +0x4ec scrolling   +0x4f0 stop speed factor (1.0)
//   +0x4f4 stop scaled by speed   +0x4f5 stopping   +0x4f8 stop speed
//   +0x4fc / +0x500 scroll speed x / y (0.2)     +0x504 / +0x508 keyboard scroll x / y
//   +0x50c / +0x510 edge scroll x / y            +0x514 / +0x518 right-button down position
//   +0x51c dragged past the dead zone             +0x520 / +0x524 zoom limits (0 / 5)
//   +0x52c mayor sub-panel                +0x540 cSC4RenderProperties (the scroll snapshot)
//   +0x5a0..+0x5bc cost indicator state   +0x5c0 the cost-indicator bitmap window
//   +0x5c8 the cost-indicator text window +0x5d0 the cursor-text window
//   +0x5d8 list<CursorTextInfo>           +0x5f0 the cursor-text icon image
//   +0x5f8 cSC4CoverageRadiusEffectHelper +0x628 show-mission flag  +0x62c current panel (-1)
//   +0x630 cSC4ProgressScreen             +0x638 "loaded from stream" flag
//   +0x640 the mission status panel       +0x648 a second validation window
//   +0x650 the error-report string        +0x678 unordered_map<u32,u32> global effect triggers
//   +0x698 1.0f                           +0x6a0/+0x6a4 last selected building / lot
//   +0x6a8 / +0x6a9 their valid flags     +0x6aa a byte
//
// Quirks kept (asserted in scripts/tests/sc4_view3dwin_test.cpp):
// * QueryInterface treats the iid as signed (`(int)iid < 1`), so the negative ids 0xc6ae7085 and
//   0xfa47b3f9 take the first branch (0x10078145c); 0xe98b2f57 is refused even though cGZWin answers it.
// * SetZoomLimits clamps against its *argument*, not against the stored +0x524 limit, and only then
//   writes the adjusted value (0x100781e96..0x100781eca).
// * GZOnCharacter bounds the persistent-tool loop by the *current* stack's size (0x100782ab8); with
//   more persistent tools than stacked ones the lower persistent tools never see the character.
// * ProcessKeyDown / ProcessKeyUp OR the persistent tools' answers into the current stack's answer and
//   then mask the result with 0x101 (0x100782c68), so only bits 0 and 8 of the answer survive.
// * SetScrolling sets the "stopping" byte to 0 but leaves the stop speed alone (0x100781b6a); only
//   ScrollStop raises it, and GZPaint consumes it.
// * RecalcScrollVelocity clears the two *edge* scroll inputs (+0x50c / +0x510) when the preference at
//   0xf09 is set (0x100781d5c) but keeps the keyboard ones, lets a keyboard direction override an edge
//   one (0x100781d82), and picks the speed from +0x4fc (edge) or +0x500 (keyboard) depending on whether
//   any keyboard input is present (0x100781dd3). The angle is kScrollDirectionAngle[y+1][x+1].
// * SetCurrentViewInputControl sends the change message (previous, new) *before* the stack is updated
//   (0x1007823ce), so a listener that calls GetCurrentViewInputControl still sees the old tool.
// * RemoveAllPersistentViewInputControls walks the vector backwards through the shrinking end pointer
//   (0x1007828e6) so every tool is released exactly once.
// * PickTerrain with no renderer writes two words of the caller's float out-parameter to 0
//   (0x100782952) instead of the three a float[3] would need.
// * DoMessage(cGZMessage&) types 0x13/0x14 store 0/1 in +0x1b8 and then fall through into the 0xd
//   case, so a mouse move also refreshes the 24-hour clock hover (0x100784d02..0x100784d1e).
// * The accelerator path appends three empty parameters to the command set (0x100784f1e): the key
//   payload is dropped.
// * GZOnMouseDownL / GZOnMouseUpL call the *bottom* stacked tool but ignore its answer (0x100783022 /
//   0x1007833f8 return before the result is looked at); the persistent tools' answers do count.
// * GZOnMouseUpR queries both terrain heights at the picked model's centre and discards both results
//   (0x100783561 / 0x100783574); the new view target is the bounding-box centre.
// * GZOnMouseMove marks the drag as past the dead zone when *either* axis moved 4 pixels, and only
//   writes the edge-scroll inputs when the city router allowed it, the preference is set and the current
//   tool is not self-scrolling; otherwise it clears both inputs (0x10078394x).
// * SetCursorText calls CursorTextChanged twice when both strings are empty: ClearCursorText already
//   ends with it and the tail jump does it again (0x1007913c6). In the update path a null second string
//   erases the *first* string instead of the second (0x10079144a) - an original bug kept on purpose.
// * CursorTextChanged shows the list's *last* node (0x100791580 reads the sentinel's +8 link) and only
//   moves the window when it is already visible; the position comes from the window manager's
//   GetCursorPos (0x100791631).
// * AddWinAnimationRecorder appends "RecordedAnimations/" to the user-data directory (0x100780e02).
// * DoCityEstablished only drops the active tool when one is on the stack (0x100786f8a: GetCurrentViewInputControl
//   first, so RemoveCurrentViewInputControl(false) is never called on an empty stack).
// * UpdateZoomButtons maps CanZoomIn (vslot 0x60) to button 0x4992046a and CanZoomOut (0x68) to 0xe9920494
//   (0x1007872ec..0x100787320), the opposite of what the ids' names suggest.
// The port keeps the host services as one IView3DWinHost method per original call; the object at
// +0x1d0 and the coverage helper at +0x5f8 are seams (their classes are not recovered). Every offset
// in the comments is the original's: the port's base layout matches it exactly up to +0x1d0 (verified:
// cGZWin is 0x150 bytes at +0x18, then +0x168 / +0x170 / +0x180 / +0x188 / +0x190 / +0x198 / +0x1a0 /
// +0x1c0). Past +0x1d0 the port's offsets are *lower* than the original's because the 0x18-byte helper
// became an 8-byte pointer (tools_ is at +0x1d8, not +0x1e8), most of the GUI blocks at +0x270..+0x4d0
// are not modelled (only the two menu windows, the cursor-text block and the scroll state are;
// filterInfo_ is at +0x268, not +0x4d8) and cSC4RenderProperties is 8 bytes shorter (so
// renderProperties_ is at +0x2c0, not +0x540). sizeof(cSC4View3DWin) is 0x558 in the port (the original's
// 0x6b0 is not reachable: the 42 GUI members keep their meaning but take the port's smaller vector/string sizes).
// Compile without FMA contraction: the results follow the original single-precision operation order.
#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include "gz/CommandServer.h"
#include "gz/Win.h"
#include "gz/WinInterfaces.h"
#include "gz/WinText.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"
#include "rz/cRZTimer.h"
#include "sc4/CameraControl.h"
#include "sc4/RenderProperties.h"
#include "sc4/ViewInputControl.h"

class cIGZCommandParameterSet;
class cISC4Occupant;
class cISC4OccupantFilter;
namespace s3d { class cIS3DModelInstance; }

namespace sc4 {

class cSC4View3DWin;

// ---------------------------------------------------------------------------------------------------------------
// Identifiers (the binary's literal pools)
// ---------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kGZIID_cISC4View3DWin = 0xfa47b3f9;          // the view interface (negative as an int)
constexpr std::uint32_t kGZIID_cIGZWinMessageFilter = 0xc6ae7085;    // DoMessage(cIGZWin*, cGZMessage&)
constexpr std::uint32_t kGZIID_cIGZMessageTarget2 = 0x452294aa;      // DoMessage(cIGZMessage2*)
constexpr std::uint32_t kView3DWindowId = 0x9a47b417;                // Init, cGZWin::SetID
constexpr std::uint32_t kView3DViewManagerIid = 0x67c5bede;          // GetViewManager's QueryInterface
constexpr std::uint32_t kView3DCursorId = 0xc16c4e1b;                // Init + the default cursor of RemoveCurrentViewInputControl
constexpr std::uint32_t kView3DAnimationRecorderId = 0xa802b4eb;      // the child window AddWinAnimationRecorder adds

// Messages the view sends / handles (0x1007820ba, 0x10078223e, 0x100782556, 0x100781908, 0x10079299e).
constexpr std::uint32_t kMsgViewFilterInfo = 0x06cdb65a;             // view filter changed, before the swap
constexpr std::uint32_t kMsgViewFilterInfoDone = 0x06cdb65b;         // ... and after
constexpr std::uint32_t kMsgViewLevelInfo = 0xc6cdbf1a;              // view level changed, before
constexpr std::uint32_t kMsgViewLevelInfoDone = 0xc6cdbf1b;          // ... and after
constexpr std::uint32_t kMsgViewInputControlChanged = 0x09c8b8ea;    // SendViewInputControlChangeMessage
constexpr std::uint32_t kMsgViewScrollBegin = 0xcad61e00;            // DoScrollBegin
constexpr std::uint32_t kMsgViewScrollEnd = 0xead61e16;              // DoScrollEnd
constexpr std::uint32_t kMsgViewGlobalEffectTrigger = 0xaa375fe8;    // Assert/RetractGlobalEffectTrigger
constexpr std::uint32_t kMsgViewTerrainDisplayChanged = 0x470bd214;  // RunGlobalTerrainTool cases 5 / 6 (data1 0 / 1)
constexpr std::uint32_t kMsgViewCommand = 0x00000003;                // the command message (cGZMsgCommand)
constexpr std::uint32_t kCmdViewCatalogItem = 0x49d993d4;            // DoMessage case 3's parameter id
constexpr std::uint32_t kMsgViewCheatCode = 0x230e27ac;              // DoMessage: run a cheat
constexpr std::uint32_t kMsgRenderPropertiesChanged = 0xc9f775bb;    // SetRenderProperties (0x100790a20)
constexpr std::uint32_t kMsgViewFocus = 0x00000010;                 // cGZMsgSetFocus
constexpr std::uint32_t kMsgViewMouseMove = 0x0000000d;             // cGZMsgMouseMove
constexpr std::uint32_t kMsgViewMouseEnter = 0x00000013;
constexpr std::uint32_t kMsgViewMouseExit = 0x00000014;
constexpr std::uint32_t kMsgViewHtmlLink = 0xe4a85d91;              // -0x1b57a26f as a u32
constexpr std::uint32_t kMsgViewAdviceLink = 0xea5bdfbc;            // -0x15a42044 as a u32
constexpr std::uint32_t kMsgViewAdviceLink2 = 0x6a720993;
constexpr std::uint32_t kMsgViewAdviceLink3 = 0x6a5bdfcd;
constexpr std::uint32_t kMsgViewKey = 0x0000001b;                   // the key message (cGZMsgKey)
constexpr std::uint32_t kKeyModifierKeyUp = 0x20000000;             // added for a "key down" message with param1 == 4

// Cheat codes registered in Init / removed in Shutdown (0x10077fbe2 / 0x1007809f6) and their strings
// (the cRZString pointers at 0x100b17a90..0x100b17af0, read from the Mach-O's __cstring).
constexpr std::uint32_t kCheatGameOfLife = 0x8973a69c;              // "GOL"
constexpr std::uint32_t kCheatMagnifyView = 0xca7bc5b5;             // "SizeOf"
constexpr std::uint32_t kCheatToggleZots = 0xaa7a2746;              // "TastyZots"
constexpr std::uint32_t kCheatToggleZones = 0x6a7a276e;             // "Zoneria"
constexpr std::uint32_t kCheatSetRenderProperty = 0x88f3d4bb;       // "RenderProp"
constexpr std::uint32_t kCheatSetRenderProperty2 = 0x88f3d4bc;      // "RP"
constexpr std::uint32_t kCheatEnableAnimationStepping = 0x4a972f37; // "WatchMeMove"
constexpr std::uint32_t kCheatTerrainQuery = 0x8ab730ae;            // "TerrainQuery"
constexpr std::uint32_t kCheatAnimationRecorder = 0x6976afa0;       // "Recorder"
constexpr std::uint32_t kCheatJackpot = 0x4a82253e;                 // "You don't deserve it"
constexpr std::uint32_t kCheatLotPlop = 0x4ac096c6;                 // "LotPlop"
constexpr std::uint32_t kCheatBuildingPlop = 0x4ac096ca;            // "BuildingPlop"
constexpr std::uint32_t kCheatTollBooth = 0x000bfd8064;             // "toll"
// The order the cheat manager is told about them (0x10077fdf0..0x1007805e7).
constexpr std::uint32_t kView3DCheatCodes[13] = {
    kCheatGameOfLife, kCheatMagnifyView, kCheatToggleZots, kCheatToggleZones, kCheatSetRenderProperty,
    kCheatSetRenderProperty2, kCheatEnableAnimationStepping, kCheatTerrainQuery, kCheatAnimationRecorder,
    kCheatJackpot, kCheatLotPlop, kCheatBuildingPlop, kCheatTollBooth};
constexpr const char* kView3DCheatStrings[13] = {
    "GOL", "SizeOf", "TastyZots", "Zoneria", "RenderProp", "RP", "WatchMeMove", "TerrainQuery", "Recorder",
    "You don't deserve it", "LotPlop", "BuildingPlop", "toll"};

// ---------------------------------------------------------------------------------------------------------------
// The panel / toolbar ids the refresh functions poke (the binary's literal pools).
// ---------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kPanelZoomInButtonId = 0x4992046a;           // UpdateZoomButtons, vslot 0x60 (CanZoomIn)
constexpr std::uint32_t kPanelZoomOutButtonId = 0xe9920494;          // UpdateZoomButtons, vslot 0x68 (CanZoomOut)
constexpr std::uint32_t kPanelCityNameId = 2;                        // UpdateCityName, the toolbar caption
constexpr std::uint32_t kPanelNewCityButtonId = 0x99887766;          // DoCityEstablished
constexpr std::uint32_t kPanelMayorButtonId = 0x8b96b73e;            // DoCityEstablished
constexpr std::uint32_t kPanelObliterateButtonId = 0x4988bc6a;       // DoCityEstablished / DoCityObliterated
constexpr std::uint32_t kPanelObliterateToggleId = 0x2988bc85;       // DoCityObliterated
constexpr int kPanelBudgetIndex = 4;                                 // DoCityObliterated hides panel 4
// 0x100a2a828 (ids) / 0x100a2a834 (modes): the two visual-time buttons, and the real-time button above them.
constexpr std::uint32_t kPanelRealTimeButtonId = 0xca35cb74;
constexpr std::uint32_t kPanelVisualModeButtonIds[2] = {0xca35cb76, 0xca35cb78};
constexpr int kPanelVisualModeButtonModes[2] = {1, 2};               // 1 = noon, 2 = midnight
// 0x100a2a9c0: the toolbar button each panel index toggles (DoPanelCommandGUI; kept for the next step).
constexpr std::uint32_t kPanelToggleButtonIds[5] = {
    0x2988bc85, 0xc988bc79, 0x4988bc6a, 0x8988bc94, 0x2988bc85};

// The 132 message types the notification helper at +0x1d0 registers (the static array at 0x100a2a550,
// constructor call at 0x10077f4c8 with count 0x84).
constexpr int kView3DMessageTypeCount = 132;
extern const std::uint32_t kView3DMessageTypes[kView3DMessageTypeCount];

// Scrolling. The direction angles are the 3x3 float table at 0x100a2a760, [y+1][x+1] in radians
// (0 = east, counter-clockwise); the table's y axis runs opposite to the screen's.
constexpr float kScrollDirectionAngle[3][3] = {
    {3.92699099f, 4.71238899f, 5.49778748f},    // y = -1: 225 / 270 / 315 degrees
    {3.14159274f, 0.0f, 0.0f},                 // y =  0: 180 /   - / east
    {2.35619450f, 1.57079637f, 0.78539819f}};  // y = +1: 135 /  90 /  45 degrees
constexpr float kScrollSpeedBase = 1.2f;              // _powf base at 0x100a21344
constexpr float kScrollSpeedPerFrame = 25.0f;         // GZPaint's distance multiplier
constexpr float kScrollDefaultSpeed = 0.2f;           // +0x4fc / +0x500 in the constructor
constexpr float kScrollStopFactor = 1.0f;             // +0x4f0 in the constructor

// Render properties used by the scroll snapshot (the index is the property table's index).
constexpr std::uint32_t kPropRenderShadows = 9;            // kRenderShadows (bool)
constexpr std::uint32_t kPropRenderDecals = 10;            // kRenderDecals (bool)
constexpr std::uint32_t kPropRenderParticleEffects = 13;   // kRenderParticleEffects (bool)
constexpr std::uint32_t kPropRenderAutomata = 32;          // kRenderAutomata (bool)
constexpr std::uint32_t kPropNoPartialBackingStoreCopies = 30;   // the flag DoScrollBegin checks
constexpr std::uint32_t kPropRenderModelThreshold = 9;     // kRenderModelThreshold (float)
constexpr std::uint32_t kPropRenderModelThresholdScroll = 10;    // kRenderModelThresholdScroll (float)

// SL::spPreferences offsets this window reads (0x1734-byte blob).
constexpr std::size_t kPrefsEdgeScrollEnabled = 0xf09;    // RecalcScrollVelocity / GZOnMouseMove

// ---------------------------------------------------------------------------------------------------------------
// The two view-information records. cSC4ViewFilterInformation holds one referenced filter object
// (the original's GetCurrentViewFilterInfo copies the pointer out of +0x4d8 and AddRefs it);
// cSC4ViewLevelInformation is a single level word.
// ---------------------------------------------------------------------------------------------------------------
struct cSC4ViewFilterInformation {
    cIGZUnknown* filter = nullptr;      // +0
};
struct cSC4ViewLevelInformation {
    std::uint32_t level = 0;            // +0
};

// ---------------------------------------------------------------------------------------------------------------
// cISC4View3DWin: the view's own interface (vtable slots 0x18..0x1e8 of group +0). The methods the port has
// not recovered yet keep the default body below, so only the recovered ones live in the class. Every slot
// number is the original's; the port calls the named methods, never the slot.
// ---------------------------------------------------------------------------------------------------------------
class cISC4View3DWin : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() { return nullptr; }                                    // 0x18
    virtual void* GetRenderer() { return nullptr; }                                     // 0x20
    virtual cIGZUnknown* GetModelMaker() { return nullptr; }                            // 0x28
    virtual cIGZUnknown* GetViewManager() { return nullptr; }                           // 0x30
    virtual bool ProcessCommand(std::uint32_t, cIGZCommandParameterSet&, cIGZCommandParameterSet&) { return false; }   // 0x38
    virtual bool SetZoom(int) { return false; }                                        // 0x40
    virtual bool ZoomIn() { return false; }                                            // 0x48
    virtual bool ZoomOut() { return false; }                                           // 0x50
    virtual bool SetZoomLimits(int, int) { return false; }                             // 0x58
    virtual bool CanZoomIn() { return false; }                                         // 0x60
    virtual bool CanZoomOut() { return false; }                                        // 0x68
    virtual bool SetRotation(int) { return false; }                                    // 0x70
    virtual bool RotateLeft() { return false; }                                        // 0x78
    virtual bool RotateRight() { return false; }                                       // 0x80
    virtual bool SetZoomAndRotation(int, int) { return false; }                        // 0x88
    virtual void GetCurrentViewFilterInfo(cSC4ViewFilterInformation&) {}               // 0x90
    virtual bool SetCurrentViewFilterInfo(cSC4ViewFilterInformation&) { return false; }    // 0x98
    virtual void GetCurrentViewLevelInfo(cSC4ViewLevelInformation&) {}                 // 0xa0
    virtual void SetCurrentViewLevelInfo(cSC4ViewLevelInformation&) {}                 // 0xa8
    virtual std::uint32_t GetCurrentViewLevel() { return 0; }                          // 0xb0
    virtual void SetCurrentViewLevel(std::uint32_t) {}                                 // 0xb8
    virtual cSC4ViewInputControl* GetCurrentViewInputControl() { return nullptr; }     // 0xc0
    // The stack operation (cISC4View3DWin::ViewInputControlStackOperation): 0 = push, 1 = remove the
    // current tool (0x1007823d2), 2 = remove all of them (0x1007823c2).
    virtual bool SetCurrentViewInputControl(cSC4ViewInputControl*, int) { return false; }   // 0xc8
    virtual bool RemoveCurrentViewInputControl(bool) { return false; }                 // 0xd0
    virtual bool RemoveAllViewInputControls(bool) { return false; }                    // 0xd8
    virtual bool AddPersistentViewInputControl(cSC4ViewInputControl*) { return false; }    // 0xe0
    virtual bool RemovePersistentViewInputControl(cSC4ViewInputControl*) { return false; } // 0xe8
    virtual bool RemoveAllPersistentViewInputControls() { return false; }               // 0xf0
    virtual cIGZWinKeyAccelerator* GetKeyAccelerator() const { return nullptr; }        // 0xf8
    virtual void SetKeyAccelerator(cIGZWinKeyAccelerator*) {}                           // 0x100
    virtual void SetScrolling(bool, float, float) {}                                    // 0x108
    virtual bool ScrollStop() { return false; }                                         // 0x110
    virtual void KillKeyboardScrolling() {}                                             // 0x118
    virtual bool PickTerrain(int, int, float*, bool) { return false; }                  // 0x120
    virtual bool PickOccupant(int, int, cISC4Occupant*&) { return false; }              // 0x128
    virtual void DisplayTerrainPickDebugString(int, int) {}                             // 0x130
    virtual bool GetTerrainQueryEnabled() { return false; }                             // 0x138
    // CursorTextIcon: 0 = none, 1 = the +0x5f0 image (CursorTextChanged checks 1).
    virtual void SetCursorText(std::uint32_t, std::uint32_t, const cIGZString*, const cIGZString*, int) {}   // 0x140
    virtual void ClearCursorText(std::uint32_t) {}                                      // 0x148
    virtual void SetCostIndicator(std::uint32_t) {}                                     // 0x150
    virtual void SetCostIndicator(std::uint32_t, std::uint32_t) {}                      // 0x158
    virtual void ClearCostIndicator(bool) {}                                            // 0x160
    virtual void SetErrorReportString(const char*) {}                                   // 0x168
    virtual bool GetGlobalEffectTriggerState(std::uint32_t) { return false; }            // 0x170
    virtual void AssertGlobalEffectTrigger(std::uint32_t) {}                            // 0x178
    virtual void RetractGlobalEffectTrigger(std::uint32_t) {}                           // 0x180
    virtual cIGZUnknown* GetCoverageHelper() { return nullptr; }                        // 0x188
    virtual void SetCursorTextPosition3D(const cS3DVector3&) {}                          // 0x190
    virtual std::uint32_t GetLastSelectedBuilding() { return 0; }                        // 0x198
    virtual std::uint32_t GetLastSelectedLot() { return 0; }                             // 0x1a0
    virtual void SetLastSelectedBuilding(std::uint32_t) {}                               // 0x1a8
    virtual void SetLastSelectedLot(std::uint32_t) {}                                    // 0x1b0
    virtual void MinimizeUI(bool) {}                                                     // 0x1b8
    virtual void ShowMissionStatusPanel(std::uint32_t) {}                                // 0x1c0
    virtual void HideMissionStatusPanel() {}                                             // 0x1c8
    virtual void UpdateMissionStatusPanel() {}                                           // 0x1d0
    virtual void CloseSecondaryMenu() {}                                                 // 0x1d8
    virtual void CloseTertiaryMenu() {}                                                  // 0x1e0
};

// ---------------------------------------------------------------------------------------------------------------
// Seams for the classes this window reaches that are not recovered (or that the other agent owns).
// One method per original call; the slot / address is in the comment.
// ---------------------------------------------------------------------------------------------------------------
class IRenderer3D : public cIGZUnknown {                // cSC43DRender (0x100aebdb0); the other half of this path
public:
    virtual ~IRenderer3D() = default;
    virtual bool Init() = 0;                            // vslot 0x18
    virtual void Shutdown() = 0;                        // vslot 0x20
    virtual void* GetDrawContext() = 0;                 // vslot 0x50
    virtual void* GetCamera() = 0;                      // vslot 0x70
    virtual void ForceFullRedraw() = 0;                 // vslot 0xa0
    virtual void Draw() = 0;                            // vslot 0xa8
    virtual bool SetZoom(int zoom) = 0;                 // vslot 0x118
    virtual bool ZoomIn() = 0;                          // vslot 0x120
    virtual bool ZoomOut() = 0;                         // vslot 0x128
    virtual bool SetRotation(int rotation) = 0;         // vslot 0x130
    virtual bool RotateLeft() = 0;                      // vslot 0x138
    virtual bool RotateRight() = 0;                     // vslot 0x140
    virtual bool SetZoomAndRotation(int zoom, int rotation) = 0;   // vslot 0x148
    virtual bool PickTerrain(int x, int y, float* out3, bool always, bool arg5) = 0;   // vslot 0x1e8
    virtual bool Pick(int x, int y, cISC4OccupantFilter* filter, s3d::cIS3DModelInstance*& model) = 0;   // vslot 0x200
};
class ICoverageHelper3D : public cIGZUnknown {          // cSC4CoverageRadiusEffectHelper at +0x5f8
public:
    virtual ~ICoverageHelper3D() = default;
    virtual void Init() = 0;                            // @ 0x100770fc8
    virtual void Shutdown() = 0;                        // @ 0x10077137e
};
// The simulator the view pauses while the city is hidden (+0x238): cISC4Simulator vslots 0x50 / 0x68.
class ISimulatorPauseSink : public cIGZUnknown {
public:
    virtual void Pause() = 0;                           // vslot 0x50
    virtual void Resume() = 0;                          // vslot 0x68
};
class INotificationHelper3D {                           // cRZAutoMsg2Notification at +0x1d0
public:
    virtual ~INotificationHelper3D() = default;
    virtual void AddMessageTypes(const std::uint32_t* types, int count) = 0;   // constructor @ 0x1000419bc
    virtual void AddAllNotifications(cIGZMessageTarget2* target) = 0;          // @ 0x100041aae
    virtual void RemoveAllNotifications(cIGZMessageTarget2* target) = 0;       // @ 0x100041b6e
};

// The object SL::spCity vslot 0x290 answers (0x100782eb4, 0x1007836xx, 0x1007837xx): the city's mouse
// router. Its class is not recovered; the view asks it three things and the router may rewrite the point.
class ICityMouseRouter {
public:
    virtual ~ICityMouseRouter() = default;
    virtual bool IsActive() = 0;                                         // vslot 0x40
    virtual bool AllowsEdgeScroll() = 0;                                 // vslot 0x48 (GZOnMouseMove)
    virtual bool ProcessMouse(int& x, int& y, std::uint32_t flags) = 0;  // vslot 0x80
};

// The window at +0x5d0, the cursor-text window (a cGZWin-based control AddGUI creates; not recovered). The
// original calls the window through its own vtable and the cGZWin subobject at +8.
class ICursorTextWindow {
public:
    virtual ~ICursorTextWindow() = default;
    virtual void SetLine1(const cIGZString& text) = 0;      // vslot 0x30
    virtual void SetLine2(const cIGZString& text) = 0;      // vslot 0x40
    virtual void SetIcon(cIGZUnknown* icon) = 0;            // vslot 0x60 (+0x5f0 image or null)
    virtual void SetPosition(int x, int y) = 0;             // vslot 0x78(&{x, y})
    virtual void ShowWindow() = 0;                          // subobject +8 vslot 0x228
    virtual void HideWindow() = 0;                          // subobject +8 vslot 0x230
    virtual bool IsVisible() = 0;                           // subobject +8 vslot 0x238
    virtual void Reset() = 0;                               // subobject +8 vslot 0x2e0
};

// SL::spTerrain (cSTETerrain, recovered by another agent): the slots this window calls.
class ITerrainEditor {
public:
    virtual ~ITerrainEditor() = default;
    virtual float TerrainHeight(float x, float z) = 0;                      // vslot 0x60
    virtual float TerrainHeightAt(float x, float z) = 0;                    // vslot 0xf8
    virtual void SetTerrainType(int type) = 0;                              // vslot 0x148
    virtual int TerrainType() = 0;                                          // vslot 0x150
    virtual void SetToolMode158(std::uint32_t mode) = 0;                    // vslot 0x158 (RunGlobalTerrainTool case 1)
    virtual void SetToolMode160(std::uint32_t mode) = 0;                    // vslot 0x160 (case 2)
    virtual void CancelActiveTool() = 0;                                    // vslot 0x178 (cases 3 / 4)
    virtual void SetActiveToolFlag(bool on) = 0;                            // vslot 0x180 (cases 3 / 4)
    virtual bool TerrainTypeForBrush(int brush, std::uint32_t& out) = 0;    // vslot 0x1d0 (ApplyDefaultTerrainBrush)
};

// ---------------------------------------------------------------------------------------------------------------
// Everything the window reaches outside itself: one method per original call.
// ---------------------------------------------------------------------------------------------------------------
class IView3DWinHost {
public:
    virtual ~IView3DWinHost() = default;

    // --- SL globals
    virtual cIGZMessageServer2* MessageServer2() = 0;                 // SL::spMS2
    virtual std::uint8_t* Preferences() = 0;                          // SL::spPreferences (0x1734 bytes)
    virtual bool CityExists() = 0;                                    // SL::spCity != nullptr
    virtual void PostGameMessage(std::uint32_t type) = 0;             // SC4PostMessage2(type,0,0,0,0,null)
    // --- the notification helper at +0x1d0
    virtual INotificationHelper3D* NotificationHelper() = 0;
    // --- the framework command table (RZGetFramework vslot 0x28 -> service 0xeb903a32/iid 0xcb90253a)
    virtual void RegisterCommandTable() = 0;                          // ... vslot 0x38(&table, 0x68)
    virtual void UnregisterCommandTable() = 0;                        // ... vslot 0x50(&table, 0x68)
    // --- SL::spCCM (the cheat-code manager)
    virtual void AddCheatCodeTarget(cIGZWin* target) = 0;             // vslot 0x28(target, 0)
    virtual void RemoveCheatCodeTarget(cIGZWin* target) = 0;          // vslot 0x30(target, 0)
    virtual bool RegisterCheatCode(std::uint32_t id, const char* text) = 0;   // vslot 0x18
    virtual bool UnregisterCheatCode(std::uint32_t id) = 0;                   // vslot 0x20
    virtual bool CheatCodeManagerPresent() = 0;                       // SL::spCCM != nullptr
    virtual bool LotPlopCheatsEnabled() = 0;                          // SL::spSC4 vslot 0xa8
    // --- key accelerator: exemplar 0x42e3ea4b then GZCOM create 0x42e967be / iid 0xe2c1b3c4
    virtual cIGZWinKeyAccelerator* CreateKeyAccelerator() = 0;
    // --- render properties (cSC4RenderProperties is recovered; these are the two instances)
    virtual cSC4RenderProperties* GlobalRenderProperties() = 0;       // SL::spRenderProperties
    virtual cSC4RenderProperties* AppRenderProperties() = 0;          // SL::spSC4 vslot 0x160
    virtual void SetScrollLightingHighlight() = 0;                    // lighting manager vslot 0x38 (0.1f, kColorYellow)
    virtual void RestoreLighting() = 0;                               // lighting manager vslot 0x40
    // --- limits taken from the app object
    virtual int VirtualZoomLimit() = 0;                               // SL::spSC4 vslot 0x130: null -> 5, else 5 - (byte 0xf10 == 0)
    virtual bool ProgressScreenWanted() = 0;                          // SL::spSC4 vslot 0x138 -> +0x208 != nullptr
    // --- objects Init creates and Shutdown tears down (each stands for one original call block)
    virtual cIGZUnknown* CreateProgressScreen(cIGZWin* parent, const cRZRect& area, bool flag) = 0;   // new cSC4ProgressScreen(area, flag)
    virtual void InitProgressScreen(cIGZUnknown* screen) = 0;         // cSC4ProgressScreen::Init @ 0x10032d23e
    virtual void SetProgressCaption(cIGZUnknown* screen, int id, const cIGZString& text) = 0;   // @ 0x10032d812
    virtual void ShutdownProgressScreen(cIGZUnknown* screen) = 0;     // @ 0x10032d0a8 + Release
    virtual void ShutdownValidationWindow(cIGZUnknown* window) = 0;   // +0x648 vslot 0x20 + Release
    virtual void ShutdownLotConfigurationEditors() = 0;               // +0x270 / +0x278 Shutdown + Release
    virtual void ShutdownModelMaker(cIGZUnknown* modelMaker) = 0;     // +0x190 Shutdown + Release
    virtual void ShutdownCameraObject(cIGZUnknown* camera) = 0;       // +0x178 cS3DCamera::Shutdown + Release
    virtual void ShutdownSimulationClock(cIGZUnknown* clock) = 0;     // +0x170 vslot 0x20 + Release
    // --- the animation recorder (cSC4WinAnimationRecorder, unrecovered)
    virtual bool UserDataDirectory(cIGZString& out) = 0;              // SL::UserDataDirectory @ 0x100040556
    virtual cIGZWin* CreateWinAnimationRecorder() = 0;                // new + ctor @ 0x1003728a4, AddRef'd
    virtual void RecorderSetAnimationOutputDirectory(cIGZWin* recorder, const cIGZString& directory, bool create) = 0;   // @ 0x1003754f2
    // --- SL::spCity / SL::spTerrain, reached by the mouse and tool paths
    virtual ICityMouseRouter* CityMouseRouter() = 0;                  // SL::spCity vslot 0x290
    virtual bool CanInvokeZoningTool() = 0;                           // SL::spCity vslot 0xb0 (0x100783d92)
    virtual ITerrainEditor* TerrainEditor() = 0;                      // SL::spTerrain
    virtual bool GraphicsSystemAllowsSelfScroll() = 0;                // SL::spGS vslot 0x60 (0x1007839a4; meaning not decoded)
    virtual void RunChangeTerrainDialog(int& type) = 0;               // cSTEChangeTerrainUI @ 0x1007268f2 + DoDialog @ 0x10072690a
    // --- the input tools the mouse / tool invokers construct (each is one "new cSC4ViewInputControl*" block)
    virtual cSC4ViewInputControl* CreateZoningTool(std::uint32_t type) = 0;                 // cSC4ViewInputControlZone @ 0x1003c38fc
    virtual cSC4ViewInputControl* CreateTerrainScrubberTool(std::uint32_t type, bool flag) = 0;   // @ 0x100395cac
    virtual cSC4ViewInputControl* CreateLevelTerrainTool() = 0;                             // @ 0x1003b74d4
    virtual cSC4ViewInputControl* CreateCityScrollTool() = 0;                               // cSC4ViewInputControlCityScroll @ 0x1002c13a2
    virtual void CityScrollSetMouseScroll(cSC4ViewInputControl* tool, int x, int y) = 0;    // @ 0x1002c1930
    // --- SL::spSS + SL::spViewUtilities + cSC4AudioSoundManager: the global terrain tool's sound (0x100784a1e)
    virtual void PlayGlobalTerrainEffectSound() = 0;
    // --- the cursor-text window (+0x5d0; AddGUI creates it, so the host owns it)
    virtual ICursorTextWindow* CursorTextWindow() = 0;
    // --- SL::TerrainView() (cSTETerrainView3D, recovered in parallel): the contour display layer
    virtual int AddContourCallback() = 0;              // TerrainView()->Layer(0)->vslot 0x40(0x7f94205)
    virtual void RemoveContourCallback(int handle) = 0;   // TerrainView()->Layer(0)->vslot 0x78(handle)
    // --- SL::spViewUtilities->ProjectPointToViewportCoordinates2D @ 0x100173a1a; false without the global
    virtual bool ProjectPointToViewport(cS3DVector3& point) = 0;
    // --- per frame / per message
    virtual void UpdateCostIndicator(float dt) = 0;                   // @ 0x10078178a (PARTIAL: the cost-indicator windows)
    virtual bool PopupModalDialogsSafe() = 0;                         // SC4GetPopupModalDialogsSafe
    virtual bool PopupModalDialogsEnabled() = 0;                      // SC4GetPopupModalDialogsEnabled
    virtual std::uint32_t SimulationFrameCounter() = 0;               // (+0x170) vslot 0x48
    virtual void FilterUiCommandMessage(cGZMessage& message) = 0;     // nsSC4TutorialSystem @ 0x10006008e
    virtual bool HandleUrl(cIGZString* link) = 0;                     // cSC4HTMLLinkHandler::DoURL @ 0x1001f22b6
    virtual void HandleAdviceLink(cGZMessage& message) = 0;            // @ 0x100785070
    virtual void ProcessCheatCode(std::uint32_t id, const char* cmdLine) = 0;   // @ 0x10078526a (takes a cRZCmdLine)
    virtual void InvokeCatalogItemCommand(std::uint32_t item, std::uint32_t param, bool flag) = 0;   // @ 0x10078410a
    // spCommandServer vslot 0x28 + three empty parameters + vslot 0x98 ExecuteCommand (0x100784f1e).
    virtual void ExecuteAcceleratorCommand(std::uint32_t command) = 0;
    virtual void Update24HourClockHover() = 0;                        // @ 0x1007858a2
    virtual void UpdateCursorTextPositions(int x, int y) = 0;         // @ 0x100783a60
    // Writes the frame counter into the draw context's native colour 1 (draw context +0x1c).
    virtual void SetDrawContextFrameMarker(void* drawContext, std::uint32_t frame) = 0;
    // --- service locator (src/sc4/ServiceLocator.cpp)
    virtual void OnView3DCreate(cISC4View3DWin* view) = 0;            // SL::OnView3DCreate  @ 0x100040fec
    virtual void OnView3DShutdown() = 0;                              // SL::OnView3DShutdown @ 0x10004107c
    // --- the panel refresh functions (UpdateCityName / UpdatePopulation / UpdateDayNightButtons / RemoveGUI)
    virtual void GetCityName(cIGZString& out) = 0;                    // SL::spCity vslot 0x60 (0x100787032)
    virtual int VisualTimeMode() = 0;                                 // SL::sp24HourClock vslot 0x30 (0x10078aa70)
    virtual int CityPopulation() = 0;                                 // SL::spResidentialSim vslot 0x80 (0x100787fca)
    virtual std::int64_t CityFunds() = 0;                             // SL::spCity vslot 0x1a0 -> vslot 0x30 (0x1007887f6)
    virtual void FormatNumber(std::int64_t value, cIGZString& out) = 0;   // RZ::LM() string 0 vslot 0x90 (0x100788046)
    virtual void MakeMoneyString(std::int64_t value, cIGZString& out) = 0;   // SC4MakeMoneyString @ 0x1000392b0
    virtual std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;   // RZ::GS() vslot 0x118
    virtual bool BudgetDialogOpen(cIGZUnknown* dialog) = 0;               // cSC4BudgetUIWinProc vslot 0x50 (0x10078883e)
    virtual void RefreshBudgetDialog(cIGZUnknown* dialog) = 0;            // cSC4BudgetUIWinProc vslot 0x48 (0x100788846)
    // --- the five window procedures RemoveGUI shuts down (each is the original's static Shutdown + Release)
    virtual void ShutdownNewsTickerWinProc(cIGZUnknown* proc) = 0;    // cSC4NewsTickerWinProc::Shutdown @ 0x10031c016
    virtual void ShutdownMySimWinProc(cIGZUnknown* proc) = 0;         // cSC4MySimWinProc::Shutdown     @ 0x100315eb2
    virtual void ShutdownBudgetUIWinProc(cIGZUnknown* proc) = 0;      // cSC4BudgetUIWinProc::Shutdown  @ 0x1002e4872
    virtual void ShutdownGraphsViewWinProc(cIGZUnknown* proc) = 0;    // cSC4GraphsViewWinProc::Shutdown @ 0x1002dfdc0
    virtual void ShutdownBuildingSelectWinProc(cIGZUnknown* proc) = 0;   // cSC4BuildingSelectWinProc::Shutdown @ 0x100759232
    virtual void ShutdownToolTipManager(cIGZUnknown* manager) = 0;       // cSC4ToolTipManager::Shutdown @ 0x1007935a4
};
IView3DWinHost* View3DWinHost();
void SetView3DWinHost(IView3DWinHost* host);

// ---------------------------------------------------------------------------------------------------------------
// cSC4View3DWin
// ---------------------------------------------------------------------------------------------------------------
// One entry of the list at +0x5d8 (SetCursorText / CursorTextChanged / ClearCursorText). The original's
// list node spends +0 / +8 on the links, so the record starts at +0x10: key, order, the two strings and the
// icon. The list is kept in the original's own order (the insert only scans from the back, 0x10079140a).
struct CursorTextInfo {
    std::uint32_t key = 0;          // +0x10
    std::uint32_t order = 0;        // +0x14
    cRZString line1;                // +0x18
    cRZString line2;                // +0x40
    int icon = 0;                   // +0x68 (1 = the +0x5f0 image)
};

class cSC4View3DWin : public cISC4View3DWin, public cIGZMessageTarget2, public cIGZWinMessageFilter, public cGZWin {
public:
    explicit cSC4View3DWin(cIGZWinMgr* mgr);             // @ 0x10077efc2
    ~cSC4View3DWin() override;                          // @ 0x10077f6f6 (+0x10077fb8e alias, +0x10077fba6 deleting, thunks)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10078145a
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }   // @ 0x1007814d6
    std::uint32_t Release() override { return cGZWin::Release(); } // @ 0x10078150a

    bool DoMessage(cGZMessage& message) override;                  // @ 0x100784ca4
    bool DoMessage(cIGZWin* window, cGZMessage& message) override; // @ 0x10078af10 (PARTIAL)
    bool DoMessage(cIGZMessage2* message) override;                // @ 0x100785a5e (PARTIAL)
    bool Init() override;                                          // @ 0x10077fbe2
    bool Shutdown() override;                                      // @ 0x100780992
    bool GZPaint() override;                                       // @ 0x100781590

    // --- cISC4View3DWin (group +0)
    cIGZWin* AsIGZWin() override;                                  // @ 0x10078153e
    void* GetRenderer() override;                                  // @ 0x100781548
    cIGZUnknown* GetModelMaker() override;                         // @ 0x100793ab0
    cIGZUnknown* GetViewManager() override;                        // @ 0x100781556
    bool SetZoom(int zoom) override;                               // @ 0x100781e1a
    bool ZoomIn() override;                                        // @ 0x100781fce
    bool ZoomOut() override;                                       // @ 0x100782014
    bool SetZoomLimits(int minZoom, int maxZoom) override;         // @ 0x100781e64
    bool CanZoomIn() override;                                     // @ 0x100781ef2
    bool CanZoomOut() override;                                    // @ 0x100781f18
    bool SetRotation(int rotation) override;                       // @ 0x100781f3e
    bool RotateLeft() override;                                    // @ 0x100781f6e
    bool RotateRight() override;                                   // @ 0x100781f9e
    bool SetZoomAndRotation(int zoom, int rotation) override;      // @ 0x10078205a
    void GetCurrentViewFilterInfo(cSC4ViewFilterInformation& out) override;   // @ 0x1007820a4
    bool SetCurrentViewFilterInfo(cSC4ViewFilterInformation& info) override;  // @ 0x1007820ba
    void GetCurrentViewLevelInfo(cSC4ViewLevelInformation& out) override;     // @ 0x100782230
    void SetCurrentViewLevelInfo(cSC4ViewLevelInformation& info) override;    // @ 0x10078223e
    std::uint32_t GetCurrentViewLevel() override;                  // @ 0x10078235c
    void SetCurrentViewLevel(std::uint32_t level) override;        // @ 0x100782368
    cSC4ViewInputControl* GetCurrentViewInputControl() override;   // @ 0x100782390
    bool SetCurrentViewInputControl(cSC4ViewInputControl* control, int operation) override;   // @ 0x1007823ae
    bool RemoveCurrentViewInputControl(bool keepCursor) override;  // @ 0x100782660
    bool RemoveAllViewInputControls(bool keepCursor) override;     // @ 0x100782790
    bool AddPersistentViewInputControl(cSC4ViewInputControl* control) override;      // @ 0x1007827b8
    bool RemovePersistentViewInputControl(cSC4ViewInputControl* control) override;   // @ 0x10078283e
    bool RemoveAllPersistentViewInputControls() override;          // @ 0x1007828c6
    cIGZWinKeyAccelerator* GetKeyAccelerator() const override;     // @ 0x100781ab2
    void SetKeyAccelerator(cIGZWinKeyAccelerator* accelerator) override;   // @ 0x100781ac0
    void SetScrolling(bool scrolling, float angle, float speed) override;   // @ 0x100781b06
    bool ScrollStop() override;                                    // @ 0x100781cb4
    void KillKeyboardScrolling() override;                         // @ 0x100781cee
    bool PickTerrain(int x, int y, float* out, bool arg4) override;         // @ 0x10078292c
    bool PickOccupant(int x, int y, cISC4Occupant*& out) override;          // @ 0x100782962
    void DisplayTerrainPickDebugString(int x, int y) override;              // @ 0x100783d74
    bool GetTerrainQueryEnabled() override;                        // @ 0x100783d7a
    void SetErrorReportString(const char* text) override;          // @ 0x100781454
    bool GetGlobalEffectTriggerState(std::uint32_t effectId) override;      // @ 0x100792974
    void AssertGlobalEffectTrigger(std::uint32_t effectId) override;        // @ 0x10079299e
    void RetractGlobalEffectTrigger(std::uint32_t effectId) override;       // @ 0x100792a6c
    cIGZUnknown* GetCoverageHelper() override;                     // @ 0x100793ad2
    std::uint32_t GetLastSelectedBuilding() override;              // @ 0x100793ae0
    std::uint32_t GetLastSelectedLot() override;                   // @ 0x100793afa
    void SetLastSelectedBuilding(std::uint32_t building) override; // @ 0x100793b14
    void SetLastSelectedLot(std::uint32_t lot) override;           // @ 0x100793b28

    // --- cGZWin overrides
    bool GZOnCharacter(char c) override;                           // @ 0x1007829f8
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;   // @ 0x100782b82
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;     // @ 0x100782b92
    bool GZOnMouseWheel(int x, int y, std::uint32_t modifiers, int delta) override;   // @ 0x100783ae2
    bool GZOnMouseExit(cIGZWin* to) override;                       // @ 0x100783c7a
    bool GZOnCommand(std::uint32_t command, std::uint64_t param) override;    // @ 0x10078abdc
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;      // @ 0x100782e9e (thunk 0x100783056)
    bool GZOnMouseDownR(int x, int y, std::uint32_t modifiers) override;      // @ 0x100783068 (thunk 0x100783256)
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;        // @ 0x100783268 (thunk 0x100783462)
    bool GZOnMouseUpR(int x, int y, std::uint32_t modifiers) override;        // @ 0x100783474 (thunk 0x100783774)
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;       // @ 0x100783786 (thunk 0x100783ad0)

    // --- cISC4View3DWin: the mission / cursor-text / cost-indicator and menu members
    void HideMissionStatusPanel() override;                        // @ 0x100792e7e
    void SetCursorText(std::uint32_t id, std::uint32_t order, const cIGZString* line1,
                       const cIGZString* line2, int icon) override;   // @ 0x10079132c
    void ClearCursorText(std::uint32_t id) override;               // @ 0x100791772
    void CloseSecondaryMenu() override;                            // @ 0x100791d18
    void SetCursorTextPosition3D(const cS3DVector3& position) override;   // @ 0x100791cc4

    // --- not virtual in the original
    void DoScrollBegin();                                          // @ 0x100781b86
    void DoScrollEnd();                                            // @ 0x100781908
    void RecalcScrollVelocity();                                   // @ 0x100781d04
    void DoTemporarySimHiddenPause();                              // @ 0x1007819e0
    void DoTemporarySimHiddenResume();                             // @ 0x100781a20
    bool ProcessKeyDown(std::uint32_t key, std::uint32_t modifiers);   // @ 0x100782ba2
    bool ProcessKeyUp(std::uint32_t key, std::uint32_t modifiers);     // @ 0x100782d20
    void SendViewInputControlChangeMessage(cSC4ViewInputControl* previous, cSC4ViewInputControl* next);   // @ 0x100782556
    void UpdateCursorTextPositions(int x, int y);                  // @ 0x100783a60
    void CursorTextChanged();                                      // @ 0x10079155e
    void UpdateCostIndicator(float dt);                            // @ 0x10078178a (PARTIAL: the cost-indicator windows)
    void CreateProgressScreen(bool flag);                          // @ 0x100780910
    void UpdateProgressMessage();                                  // @ 0x100788d98
    void InvokeZoningTool(std::uint32_t type);                     // @ 0x100783d86
    void InvokeLandscapeTool(std::uint32_t type, bool flag);       // @ 0x10078408e
    void ApplyDefaultTerrainBrush(int brush, bool flag);           // @ 0x100790b4a
    void ChangeTerrainType();                                      // @ 0x1007928da
    void RunGlobalTerrainTool(int tool);                           // @ 0x1007849bc
    bool ToggleContourDisplay();                                   // @ 0x10078fe00
    void AddWinAnimationRecorder(bool enable);                     // @ 0x100780d78
    // --- the GUI (see "the GUI windows" below). AddGUI is not recovered, so everything it builds is null
    // until the host fills it; RemoveGUI and the refresh functions below cope with any window being absent.
    void RemoveGUI();                                              // @ 0x100780eaa (PARTIAL: AddGUI missing)
    bool SetRenderProperties(const cIGZString& name, const cIGZString& value);   // @ 0x10079064e
    static void SetRenderPropertiesStatic(const cRZString& name, const cRZString& value, void* context);   // @ 0x100792b3a
    void UpdateZoomButtons();                                      // @ 0x1007872e8 (PARTIAL: AddGUI missing)
    void DoCityEstablished();                                      // @ 0x100786f3e (PARTIAL: AddGUI missing)
    void UpdateCityName();                                         // @ 0x10078700a (PARTIAL: AddGUI missing)
    void UpdateDayNightButtons();                                  // @ 0x10078aa58 (PARTIAL: AddGUI missing)
    void UpdatePopulation();                                       // @ 0x100787fb6 (PARTIAL: AddGUI missing)
    void UpdateBudget();                                           // @ 0x1007887bc (PARTIAL: AddGUI missing)

protected:
    void ApplyScrollSpeed();                                       // the shared tail of RecalcScrollVelocity

    // ---- original members (offsets are the original's; the port's cGZWin is at +0x18 like the original's)
    bool initialized_ = false;                          // +0x168
    bool deferredPumpEnabled_ = false;                  // +0x169 (a byte the original only reads)
    cIGZUnknown* simulationClock_ = nullptr;            // +0x170 (reference held; vslot 0x48 = frame counter)
    cIGZUnknown* cameraObject_ = nullptr;               // +0x178 (cS3DCamera, referenced)
    cSC4CameraControl* cameraControl_ = nullptr;        // +0x180 (referenced)
    IRenderer3D* renderer_ = nullptr;                   // +0x188 (referenced)
    cIGZUnknown* modelMaker_ = nullptr;                 // +0x190 (referenced; cSC4ModelMaker)
    float frameTimeThreshold_ = 0.0f;                   // +0x198 seconds
    cRZTimer frameTimer_{cRZTimer::kMicroseconds};      // +0x1a0
    std::uint8_t focusState_ = 0;                       // +0x1b8
    std::uint32_t lastMessageModifiers_ = 0;            // +0x1bc
    cIGZWinKeyAccelerator* keyAccelerator_ = nullptr;   // +0x1c0 (referenced)
    std::uint16_t unknown1c8_ = 0x0100;                 // +0x1c8 (bytes 0x00, 0x01)
    bool terrainQueryEnabled_ = false;                  // +0x1ca
    INotificationHelper3D* notifications_ = nullptr;    // +0x1d0 (borrowed from the host, like the inline object)
    std::vector<cSC4ViewInputControl*> tools_;           // +0x1e8 (each holds one reference)
    std::vector<cSC4ViewInputControl*> persistentTools_; // +0x200 (each holds one reference)
    std::list<cRZAutoRefCount<cIGZMessage2>> deferredMessages_;   // +0x218
    cIGZUnknown* windowService_ = nullptr;              // +0x230 (referenced)
    ISimulatorPauseSink* simulatorToPause_ = nullptr;   // +0x238 (borrowed; vslots 0x50 / 0x68)
    cRZTimer simHiddenTimer_{cRZTimer::kMilliseconds};  // +0x248
    bool simHiddenPaused_ = false;                      // +0x260
    std::uint32_t simHiddenTimeoutMs_ = 2000;           // +0x264
    bool paintEnabled_ = false;                         // +0x244 (Init and Shutdown clear it; GZPaint needs it)
    cIGZWin* viewManagerSource_ = nullptr;              // +0x498 (set by AddGUI; GetViewManager queries it, RemoveGUI shuts it down)

    // ---- the GUI windows (the original's +0x280..+0x4d0 block; AddGUI @ 0x1007892fc builds them and
    // RemoveGUI tears them down). AddGUI is not recovered: it needs the five window procedures below, the
    // advisor popup tracker and the four nSC4UI window-set helpers. The port keeps the same members so the
    // refresh functions and RemoveGUI are the original's; in the port they only hold what the host fills.
    cIGZWin* panelWindow_ = nullptr;                    // +0x280 (the toolbar; every SetWindowEnabled target)
    cIGZWin* panelInterface_ = nullptr;                 // +0x288 (its interface 0x5386d516)
    cIGZWin* toolbarWindow_ = nullptr;                  // +0x290 (the second toolbar window)
    std::vector<cIGZWin*> guiWindows_;                  // +0x298 (each holds one reference)
    std::vector<cIGZWin*> panelWindows_[5];             // +0x2b0..+0x328 (panel i's windows, stride 0x18)
    cIGZWin* panelAttach_[5] = {};                      // +0x328..+0x348 (each holds one reference)
    bool panelShown_[3] = {};                           // +0x440 mayor sub-panel / +0x441 my sim / +0x442 tertiary
    cIGZWin* panelUIWinProc_ = nullptr;                 // +0x448 cSC4PanelUIWinProc (referenced)
    cIGZWin* newsTickerWinProc_ = nullptr;              // +0x450 cSC4NewsTickerWinProc
    cIGZWin* mySimWinProc_ = nullptr;                   // +0x460 cSC4MySimWinProc
    cIGZWin* budgetUIWinProc_ = nullptr;                // +0x468 cSC4BudgetUIWinProc
    cIGZWin* window470_ = nullptr;                      // +0x470
    cIGZWin* window478_ = nullptr;                      // +0x478
    cIGZWin* graphsViewWinProc_ = nullptr;              // +0x488 cSC4GraphsViewWinProc
    cIGZWin* buildingSelectWinProc_ = nullptr;          // +0x490 cSC4BuildingSelectWinProc
    cIGZWin* window4a0_ = nullptr;                      // +0x4a0
    cIGZWin* window4a8_ = nullptr;                      // +0x4a8
    cIGZWin* window4b0_ = nullptr;                      // +0x4b0
    gz::cIGZWinText* dateText_ = nullptr;               // +0x4b8 (SetCaption = vslot 0x20)
    gz::cIGZWinText* budgetText_ = nullptr;             // +0x4c0
    gz::cIGZWinText* populationText_ = nullptr;         // +0x4c8
    cIGZUnknown* tipManager_ = nullptr;                 // +0x4d0 (Shutdown = vslot 0x38)
    int currentPanel_ = -1;                             // +0x350
    ICoverageHelper3D* coverageHelper_ = nullptr;        // +0x5f8 (the helper object, borrowed from the host)
    cIGZUnknown* filterInfo_ = nullptr;                 // +0x4d8 (the cSC4ViewFilterInformation's object)
    std::uint32_t viewLevel_ = 1;                       // +0x4e0 (constructor)
    float scrollAngle_ = 0.0f;                          // +0x4e4
    float scrollSpeed_ = 0.0f;                          // +0x4e8
    bool scrolling_ = false;                            // +0x4ec
    float scrollStopFactor_ = kScrollStopFactor;        // +0x4f0
    bool scrollStopScaled_ = false;                     // +0x4f4
    bool scrollStopping_ = false;                       // +0x4f5
    float scrollStopSpeed_ = 0.0f;                      // +0x4f8
    float edgeScrollSpeed_ = kScrollDefaultSpeed;       // +0x4fc (used when only the edge scroll is active)
    float keyboardScrollSpeed_ = kScrollDefaultSpeed;   // +0x500 (used when a keyboard scroll input is set)
    int keyboardScrollX_ = 0;                           // +0x504
    int keyboardScrollY_ = 0;                           // +0x508
    int edgeScrollX_ = 0;                               // +0x50c
    int edgeScrollY_ = 0;                               // +0x510
    int mouseDownX_ = 0;                                // +0x514
    int mouseDownY_ = 0;                                // +0x518
    bool draggedPastDeadZone_ = false;                  // +0x51c
    int zoomMin_ = 0;                                   // +0x520
    int zoomMax_ = 5;                                   // +0x524
    std::uint32_t mayorSubPanel_ = 0x15200003;          // +0x52c
    cSC4RenderProperties renderProperties_;             // +0x540 (the scroll snapshot)
    std::uint32_t renderPropertiesAux_ = 0;             // +0x550 (the dword next to the property vectors)
    cIGZUnknown* progressScreen_ = nullptr;             // +0x630 (referenced)
    bool loadedFromStream_ = true;                      // +0x638 (constructor; DoLoad clears it)
    cIGZWin* missionPanel_ = nullptr;                   // +0x640 (referenced; HideMissionStatusPanel ChildDeletes it)
    cIGZUnknown* validationWindow_ = nullptr;           // +0x648 (referenced)
    cRZString errorReport_;                             // +0x650
    // The cost-indicator state (+0x5a0..+0x5ac). The two windows (+0x5c0 / +0x5c8) are not modelled yet.
    std::uint32_t costValue_ = 0;                       // +0x5a0 (the amount SetCostIndicator was given)
    std::int32_t costState_ = 0;                        // +0x5a4 (1 = the indicator is active)
    std::uint32_t costColorState_ = 0;                  // +0x5a8 (0 = cannot afford, 1 = affordable, 2 = free)
    float costTimer_ = 0.0f;                            // +0x5ac (UpdateCostIndicator's countdown, seconds)
    // The cursor-text block (+0x5a0..+0x5f0). The two windows and the bitmap window belong to the GUI AddGUI
    // builds; the port borrows the text window from the host and does not model the other two yet.
    int cursorTextX_ = 0;                               // +0x5b0
    int cursorTextY_ = 0;                               // +0x5b4
    ICursorTextWindow* cursorTextWindow_ = nullptr;     // +0x5d0 (borrowed from the host)
    std::list<CursorTextInfo> cursorText_;              // +0x5d8
    cIGZUnknown* cursorTextIcon_ = nullptr;             // +0x5f0 (the icon image; borrowed)
    // The secondary menu window (+0x358) and its state word (+0x368).
    cIGZWin* secondaryMenu_ = nullptr;                  // +0x358 (referenced; CloseSecondaryMenu ChildDeletes it)
    std::uint32_t secondaryMenuState_ = 0;              // +0x368
    bool contourDisplay_ = false;                       // +0x628
    int contourCallback_ = -1;                          // +0x62c (the contour layer's callback handle)
    std::unordered_map<std::uint32_t, std::uint32_t> globalEffects_;   // +0x678
    float unknown698_ = 1.0f;                           // +0x698
    std::uint32_t lastSelectedBuilding_ = 0;            // +0x6a0
    std::uint32_t lastSelectedLot_ = 0;                 // +0x6a4
    bool hasLastSelectedBuilding_ = false;              // +0x6a8
    bool hasLastSelectedLot_ = false;                   // +0x6a9
    std::uint8_t unknown6aa_ = 0;                       // +0x6aa

private:
    IView3DWinHost* host_ = nullptr;                    // port-only: the SL globals the original reads directly
    friend struct View3DWinTestAccess;
};

}  // namespace sc4
