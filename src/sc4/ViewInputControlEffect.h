// cSC4ViewInputControlEffect — the view tool that plays a visual effect where the mouse points on the terrain
// (functions 0x100397140..0x10039827c; class id inherited from cSC4ViewInputControl). Used by the disaster tools
// and the effect cheats. An effect "name" N selects the effect set N_button_trigger / N_trigger (existence flags) and the
// effects N_button, N_single, N_control, N_active, N_inactive; SetEffectGroupID selects the same set by group id +
// type (5 button, 4 single, 7 control, 1 active, 2 inactive, 3 trigger).
// Layout (offsets of the original object): +0x39 capturing, +0x3a mouse is down, +0x3b control effect was idle at
// press, +0x3c picked terrain position, +0x48 source transform (translation at +0x70, scale at +0x7c), +0x80 effects
// manager, +0x88 name (cRZString, string at +0x90), +0xb0 group id, +0xb4 any effect found, +0xb5 has
// "_button_trigger", +0xb6 has "_trigger", +0xb7 pick flag passed to the view, +0xb8 button, +0xc0 control,
// +0xc8 single, +0xd0 active, +0xd8 inactive effects.
// Quirks kept: OnMouseDownL returns true even when the terrain pick fails; OnMouseMove returns true always;
// SetTerrainControlledEffectState dereferenced a null control effect in the original (guarded here); the "_button_trigger"
// flag (0xb5) is computed but never read inside the class; PlayTriggerEffect ignores its argument and uses the
// tool's own transform.
#pragma once
#include <cstdint>
#include <string>

#include "gz/StreamOps.h"
#include "sc4/ViewInputControl.h"

namespace sc4 {

// cISC4VisualEffect as the tool uses it (vslots 0x18 Start, 0x20 Stop, 0x28 IsStarted, 0x40 SetSourceTransform,
// 0x58 SetVectorParams, 0x60 SetFloatParams).
class IToolVisualEffect : public cIGZUnknown {
public:
    virtual void Start(int transition) = 0;
    virtual void Stop(int transition) = 0;
    virtual bool IsStarted() = 0;
    virtual void SetSourceTransform(const cS3DTransform& transform) = 0;
    virtual bool SetVectorParams(std::uint32_t type, const cS3DVector3* values, int count) = 0;
    virtual bool SetFloatParams(std::uint32_t type, const float* values, int count) = 0;
};

// cSC4EffectsManager as the tool uses it (vslots 0x28, 0x30, 0x38).
class IToolEffectsManager : public cIGZUnknown {
public:
    virtual bool HasEffect(const char* name) = 0;                                              // 0x28
    virtual bool CreateEffect(const char* name, IToolVisualEffect** out) = 0;                  // 0x30
    virtual bool CreateEffect(std::uint32_t groupId, int type, IToolVisualEffect** out) = 0;   // 0x38
};

// cISC4View3DWin::vslot 0x120: pick the terrain point under a screen position.
class IEffectView : public IViewInputView {
public:
    virtual bool PickTerrain(int x, int y, cS3DVector3* out, bool flag) = 0;
};

// SL::spEffectsManager
class IEffectToolHost {
public:
    virtual ~IEffectToolHost() = default;
    virtual IToolEffectsManager* EffectsManager() = 0;
};
IEffectToolHost* EffectToolHost();
void SetEffectToolHost(IEffectToolHost* host);

class cSC4ViewInputControlEffect : public cSC4ViewInputControl {
public:
    // effect group types used by SetEffectGroupID / PlayTriggerEffect
    enum : int { kTypeActive = 1, kTypeInactive = 2, kTypeTrigger = 3, kTypeSingle = 4, kTypeButton = 5, kTypeControl = 7 };

    cSC4ViewInputControlEffect();                               // @ 0x100397140
    ~cSC4ViewInputControlEffect() override;                     // @ 0x100397206 (+ deleting 0x10039737c, 0x100397372)

    bool Init() override;                                       // @ 0x1003973aa
    bool Shutdown() override;                                   // @ 0x100397410
    bool ShouldStack() override { return false; }               // @ 0x10039827c
    bool OnKeyDown(std::uint32_t key, std::uint32_t mods) override;        // @ 0x100397558
    bool OnMouseDownL(int x, int y, std::uint32_t mods) override;          // @ 0x100397566
    bool OnMouseUpL(int x, int y, std::uint32_t mods) override;            // @ 0x100397956
    bool OnMouseMove(int x, int y, std::uint32_t mods) override;           // @ 0x100397858
    bool OnMouseExit() override;                                           // @ 0x100397a44
    virtual void SetTerrainControlledEffectState(bool on);                 // vslot 0xf8 @ 0x10039823c

    void PlayTriggerEffect(const cS3DTransform& transform);     // @ 0x100397724
    void SetEffectName(const char* name);                       // @ 0x100397a70
    void SetEffectGroupID(std::uint32_t groupId);               // @ 0x100397f7c
    void SetPickFlag(bool flag) { pickFlag_ = flag; }

    // inspection for tests / subclasses
    bool IsCapturing() const { return capturing_; }
    bool IsMouseDown() const { return mouseDown_; }
    bool HasAnyEffect() const { return anyEffect_; }
    const cS3DTransform& SourceTransform() const { return transform_; }

protected:
    bool capturing_ = false;                    // +0x39
    bool mouseDown_ = false;                    // +0x3a
    bool controlWasIdle_ = false;               // +0x3b
    cS3DVector3 pickPos_;                       // +0x3c
    cS3DTransform transform_;                   // +0x48
    IToolEffectsManager* effectsManager_ = nullptr;   // +0x80
    std::string name_;                          // +0x90
    std::uint32_t groupId_ = 0;                 // +0xb0
    bool anyEffect_ = false;                    // +0xb4
    bool hasButtonTrigger_ = false;             // +0xb5
    bool hasTrigger_ = false;                   // +0xb6
    bool pickFlag_ = false;                     // +0xb7
    IToolVisualEffect* button_ = nullptr;       // +0xb8
    IToolVisualEffect* control_ = nullptr;      // +0xc0
    IToolVisualEffect* single_ = nullptr;       // +0xc8
    IToolVisualEffect* active_ = nullptr;       // +0xd0
    IToolVisualEffect* inactive_ = nullptr;     // +0xd8

private:
    void StampPick();
    static void ReleaseEffect(IToolVisualEffect*& e, bool stop);
};

}  // namespace sc4
