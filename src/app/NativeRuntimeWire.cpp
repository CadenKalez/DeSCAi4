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

struct ServiceEntry {
    const char* name;
    const void* const* slot;
    int phase;       // 0 bound at boot, 1 bound by the city load
    bool required;   // missing at boot halts
};
const ServiceEntry kServices[] = {
#include "app/ServiceTable.inc"
};


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
        for (const ServiceEntry& entry : kServices) {
            if (entry.phase != 0) continue;
            void* value = const_cast<void*>(*entry.slot);
            if (value) registry.Require(entry.name, value);
            else if (entry.required) ok &= registry.Require(entry.name, nullptr);
            else std::cerr << "[Runtime Wire] note: " << entry.name << " is not bound at boot" << std::endl;
        }
        return ok;
    };
    hooks.mainSimulationLoop = [] { return 0; };   // the frame loop in game_main continues after the check
    return lifecycle.Run(hooks) == 0;
}

void ReportCityWiring() {
    int bound = 0, total = 0;
    std::cerr << "[Runtime Wire] ---- city-phase service table ----" << std::endl;
    for (const ServiceEntry& entry : kServices) {
        if (entry.phase != 1) continue;
        ++total;
        if (*entry.slot) { ++bound; std::cerr << "[Runtime Wire]   " << entry.name << " -> " << *entry.slot << std::endl; }
        else std::cerr << "[Runtime Wire]   " << entry.name << " UNBOUND" << std::endl;
    }
    std::cerr << "[Runtime Wire] city services bound: " << bound << " of " << total << " (see docs/WIRING.md for the host seams)" << std::endl;
    for (const ServiceEntry& entry : kServices)
        if (entry.phase == 2) std::cerr << "[Runtime Wire]   view-phase " << entry.name << (*entry.slot ? " bound" : " pending (not a COM object, or not published natively yet)") << std::endl;
}

}  // namespace native
