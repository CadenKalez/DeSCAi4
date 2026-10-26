#include "app/NativeRuntimeWire.h"

#include <iostream>

#include "app/NativeAudio.h"
#include "app/NativeBoot.h"
#include "sc4/App.h"
#include "render/gd/VideoModes.h"
#include "runtime/engine_lifecycle_wire.h"
#include "sc4/ServiceLocator.h"

namespace native {
namespace {

class GameAudioAdapter final : public runtime::IAudioSystem {
public:
    bool Init() override { return true; }
    void Shutdown() override {}
    bool PlaySound(std::uint32_t soundId) override { PlayGameSound(soundId); return true; }
    void StopAll() override {}
    void SetMasterVolume(float) override {}
};

}  // namespace

bool VerifyRuntimeWiring(gd::cIGZGDriver* driver, bool audioRunning) {
    static GameAudioAdapter audioAdapter;
    runtime::EngineLifecycle lifecycle;
    runtime::SubsystemHooks hooks;
    hooks.fileSystemInit = [] { return SL::spRM != nullptr; };
    hooks.configLoad = [] { return BootApplication() && BootApplication()->GetPreferences(); };
    hooks.memoryPoolAlloc = []() -> void* { return BootFramework(); };   // the framework owns the allocator and the system services
    hooks.graphicsCreate = [driver]() -> void* { return driver; };
    hooks.graphicsExposesMode0 = [](void* d) {
        gd::sGDMode mode{};
        static_cast<gd::cIGZGDriver*>(d)->GetVideoModeInfo(mode);
        return mode.id == 0 && mode.width > 0 && mode.height > 0;
    };
    hooks.audioCreate = [audioRunning]() -> runtime::IAudioSystem* { return audioRunning ? &audioAdapter : nullptr; };
    hooks.inputCreate = []() -> runtime::IInputSystem* { return nullptr; };   // events arrive through the window manager; the null input polls the shared queue
    hooks.wireGlobalDependencies = [&lifecycle]() {
        runtime::WireRegistry& registry = lifecycle.Registry();
        bool ok = true;
        ok &= registry.Require("SL::spRM (resource manager)", SL::spRM);
        ok &= registry.Require("SL::spMS2 (message server 2)", SL::spMS2);
        // Bound by later boot steps / the city load: reported, not fatal.
        struct Soft { const char* name; const void* value; };
        const Soft soft[] = {
            {"SL::spRenderProperties", SL::spRenderProperties}, {"SL::spGF (gimex factory)", SL::spGF}, {"SL::spLua", SL::spLua},
            {"SL::spTerrain (bound at city load)", SL::spTerrain}, {"SL::spLightingManager (bound at city load)", SL::spLightingManager},
        };
        for (const Soft& s : soft) {
            if (s.value) registry.Require(s.name, const_cast<void*>(s.value));
            else std::cerr << "[Runtime Wire] note: " << s.name << " is not bound yet" << std::endl;
        }
        return ok;
    };
    hooks.mainSimulationLoop = [] { return 0; };   // the frame loop in game_main continues after the check
    return lifecycle.Run(hooks) == 0;
}

}  // namespace native
