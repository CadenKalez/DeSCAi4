#include "runtime/engine_lifecycle_wire.h"

#include <cstdio>
#include <iostream>
#include <sstream>

namespace runtime {
namespace {

std::string Hex(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

std::string Pointer(const void* pointer) { return Hex(reinterpret_cast<std::uintptr_t>(pointer)); }

constexpr std::uint32_t kWmClose = 0x0010;
constexpr std::uint32_t kWmSize = 0x0005;
constexpr std::uint32_t kWmKeyDown = 0x0100;
constexpr std::uint32_t kWmKeyUp = 0x0101;
constexpr std::uint32_t kWmChar = 0x0102;
constexpr std::uint32_t kWmSysKeyDown = 0x0104;
constexpr std::uint32_t kWmSysKeyUp = 0x0105;
constexpr std::uint32_t kWmMouseMove = 0x0200;
constexpr std::uint32_t kWmLButtonDown = 0x0201;
constexpr std::uint32_t kWmLButtonUp = 0x0202;
constexpr std::uint32_t kWmRButtonDown = 0x0204;
constexpr std::uint32_t kWmRButtonUp = 0x0205;
constexpr std::uint32_t kWmMButtonDown = 0x0207;
constexpr std::uint32_t kWmMButtonUp = 0x0208;
constexpr std::uint32_t kWmMouseWheel = 0x020a;

}  // namespace

const char* StageName(Stage stage) {
    switch (stage) {
    case Stage::FileSystem: return "FileSystem::Init";
    case Stage::Config: return "Config::Load";
    case Stage::MemoryPool: return "MemoryPool::Alloc";
    case Stage::Graphics: return "GraphicSystemW32::Create";
    case Stage::Audio: return "AudioSystem::Init";
    case Stage::Input: return "InputSystem::Init";
    case Stage::SimulationLoop: return "MainSimulationLoop";
    case Stage::Complete: return "Complete";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------------------------------- input queue

MainThreadQueue::MainThreadQueue() : nextSequence_(1) {}

void MainThreadQueue::Push(InputEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    event.sequence = nextSequence_++;
    events_.push_back(event);
}

std::size_t MainThreadQueue::Drain(const std::function<void(const InputEvent&)>& consumer) {
    std::deque<InputEvent> batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(events_);
    }
    for (std::deque<InputEvent>::const_iterator it = batch.begin(); it != batch.end(); ++it)
        if (consumer) consumer(*it);
    return batch.size();
}

std::size_t MainThreadQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

MainThreadQueue& InputQueue() {
    static MainThreadQueue queue;
    return queue;
}

bool TranslateLegacyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam) {
    InputEvent event;
    event.type = InputType::Unknown;
    event.code = static_cast<std::uint32_t>(wParam);
    event.x = static_cast<std::int16_t>(lParam & 0xffff);
    event.y = static_cast<std::int16_t>((lParam >> 16) & 0xffff);
    event.modifiers = 0;
    event.sequence = 0;
    switch (message) {
    case kWmClose: event.type = InputType::Close; break;
    case kWmSize: event.type = InputType::Resize; break;
    case kWmKeyDown:
    case kWmSysKeyDown: event.type = InputType::KeyDown; break;
    case kWmKeyUp:
    case kWmSysKeyUp: event.type = InputType::KeyUp; break;
    case kWmChar: event.type = InputType::Char; break;
    case kWmMouseMove: event.type = InputType::MouseMove; break;
    case kWmLButtonDown: event.type = InputType::MouseDown; event.code = 0; break;
    case kWmRButtonDown: event.type = InputType::MouseDown; event.code = 1; break;
    case kWmMButtonDown: event.type = InputType::MouseDown; event.code = 2; break;
    case kWmLButtonUp: event.type = InputType::MouseUp; event.code = 0; break;
    case kWmRButtonUp: event.type = InputType::MouseUp; event.code = 1; break;
    case kWmMButtonUp: event.type = InputType::MouseUp; event.code = 2; break;
    case kWmMouseWheel:
        event.type = InputType::MouseWheel;
        event.code = static_cast<std::uint32_t>(static_cast<std::int16_t>((wParam >> 16) & 0xffff));
        break;
    default:
        std::cerr << "[Runtime Wire] unmapped legacy window message " << Hex(message) << " wParam=" << Hex(wParam)
                  << " lParam=" << Hex(static_cast<std::uint64_t>(lParam)) << std::endl;
        return false;
    }
    InputQueue().Push(event);
    return true;
}

// ---------------------------------------------------------------------------------------------------- null objects

NullAudioSystem::NullAudioSystem() : calls_(0), reported_(false) {}

void NullAudioSystem::Note(const char* name, std::uint32_t argument) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++calls_;
    if (!reported_) {
        reported_ = true;
        std::cerr << "[Runtime Wire] NullAudioSystem::" << name << "(" << Hex(argument) << ") reached; audio is stubbed" << std::endl;
    }
}

bool NullAudioSystem::Init() { Note("Init", 0); return true; }
void NullAudioSystem::Shutdown() { Note("Shutdown", 0); }
bool NullAudioSystem::PlaySound(std::uint32_t soundId) { Note("PlaySound", soundId); return true; }
void NullAudioSystem::StopAll() { Note("StopAll", 0); }
void NullAudioSystem::SetMasterVolume(float) { Note("SetMasterVolume", 0); }

std::uint64_t NullAudioSystem::CallCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_;
}

NullInputSystem::NullInputSystem() : reported_(false) {}
bool NullInputSystem::Init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!reported_) { reported_ = true; std::cerr << "[Runtime Wire] NullInputSystem::Init reached; events come from the main-thread queue" << std::endl; }
    return true;
}
void NullInputSystem::Shutdown() {}
bool NullInputSystem::PollEvent(InputEvent& out) {
    bool got = false;
    InputQueue().Drain([&out, &got](const InputEvent& event) {
        if (!got) { out = event; got = true; }
    });
    return got;
}
bool NullInputSystem::IsKeyDown(std::uint32_t) const { return false; }

NullNetworkSystem::NullNetworkSystem() : reported_(false) {}
bool NullNetworkSystem::Connect(const std::string& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!reported_) { reported_ = true; std::cerr << "[Runtime Wire] NullNetworkSystem::Connect(" << address << ") reached; networking is stubbed" << std::endl; }
    return false;
}
void NullNetworkSystem::Disconnect() {}
bool NullNetworkSystem::Send(const void*, std::size_t) { return false; }
std::size_t NullNetworkSystem::Receive(void*, std::size_t) { return 0; }

// ---------------------------------------------------------------------------------------------------- wiring

bool WireRegistry::Wire(const std::string& name, void** slot, void* value, const std::function<void*()>& fallbackFactory) {
    bool usedFallback = false;
    if (!slot) {
        std::cerr << "[Runtime Wire] FATAL: sub-system " << name << " has no destination slot (null global address)" << std::endl;
        return false;
    }
    if (!value && fallbackFactory) {
        value = fallbackFactory();
        usedFallback = value != nullptr;
    }
    if (!value) {
        std::cerr << "[Runtime Wire] FATAL: sub-system " << name << " is null and has no fallback" << std::endl;
        return false;
    }
    *slot = value;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        WireEntry entry;
        entry.name = name;
        entry.address = value;
        entry.fallback = usedFallback;
        entries_.push_back(entry);
    }
    std::cerr << "[Runtime Wire] Sub-system " << name << " successfully bound to Memory Address " << Pointer(value)
              << " (slot " << Pointer(slot) << ")" << (usedFallback ? " [null-object fallback]" : "") << std::endl;
    return true;
}

bool WireRegistry::Require(const std::string& name, void* value) {
    if (!value) {
        std::cerr << "[Runtime Wire] FATAL: required dependency " << name << " is null" << std::endl;
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    WireEntry entry;
    entry.name = name;
    entry.address = value;
    entry.fallback = false;
    entries_.push_back(entry);
    std::cerr << "[Runtime Wire] Sub-system " << name << " successfully bound to Memory Address " << Pointer(value) << std::endl;
    return true;
}

void WireRegistry::PrintMap() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << "[Runtime Wire] ---- wired systems (" << entries_.size() << ") ----" << std::endl;
    for (std::size_t i = 0; i < entries_.size(); ++i)
        std::cerr << "[Runtime Wire]   " << entries_[i].name << " -> " << Pointer(entries_[i].address)
                  << (entries_[i].fallback ? "  (fallback)" : "") << std::endl;
    std::cerr << "[Runtime Wire] ---------------------------" << std::endl;
}

std::size_t WireRegistry::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

const std::vector<WireEntry>& WireRegistry::Entries() const { return entries_; }

// ---------------------------------------------------------------------------------------------------- lifecycle

EngineLifecycle::EngineLifecycle()
    : failedStage_(Stage::Complete), graphics_(nullptr), memory_(nullptr), audio_(nullptr), input_(nullptr), network_(nullptr) {}

int EngineLifecycle::Halt(Stage stage, const std::string& missing) {
    failedStage_ = stage;
    std::cerr << "[Runtime Wire] HALT at " << StageName(stage) << ": missing dependency: " << missing << std::endl;
    registry_.PrintMap();
    return 1 + static_cast<int>(stage);
}

int EngineLifecycle::Run(const SubsystemHooks& hooks) {
    std::cerr << "[Runtime Wire] boot sequence start" << std::endl;

    if (!hooks.fileSystemInit) return Halt(Stage::FileSystem, "hook fileSystemInit is not set");
    if (!hooks.fileSystemInit()) return Halt(Stage::FileSystem, "file system (game data packages) did not initialise");
    std::cerr << "[Runtime Wire] stage FileSystem::Init ok" << std::endl;

    if (!hooks.configLoad) return Halt(Stage::Config, "hook configLoad is not set");
    if (!hooks.configLoad()) return Halt(Stage::Config, "configuration (ini / preferences) did not load");
    std::cerr << "[Runtime Wire] stage Config::Load ok" << std::endl;

    if (!hooks.memoryPoolAlloc) return Halt(Stage::MemoryPool, "hook memoryPoolAlloc is not set");
    memory_ = hooks.memoryPoolAlloc();
    if (!registry_.Require("MemoryPool", memory_)) return Halt(Stage::MemoryPool, "memory pool allocation returned null");

    if (!hooks.graphicsCreate) return Halt(Stage::Graphics, "hook graphicsCreate is not set");
    graphics_ = hooks.graphicsCreate();
    if (!registry_.Require("GraphicSystemW32", graphics_)) return Halt(Stage::Graphics, "graphics driver reference is null");
    if (hooks.graphicsExposesMode0 && !hooks.graphicsExposesMode0(graphics_))
        return Halt(Stage::Graphics, "graphics driver does not expose current video mode 0 (the boot code reads it)");
    std::cerr << "[Runtime Wire] stage GraphicSystemW32::Create ok (mode 0 exposed)" << std::endl;

    audio_ = hooks.audioCreate ? hooks.audioCreate() : nullptr;
    void* audioSlot = nullptr;
    if (!registry_.Wire("AudioSystem", &audioSlot, audio_, [this]() -> void* { return static_cast<IAudioSystem*>(&nullAudio_); }))
        return Halt(Stage::Audio, "audio system is null and no fallback is available");
    audio_ = static_cast<IAudioSystem*>(audioSlot);
    if (!audio_->Init()) {
        std::cerr << "[Runtime Wire] AudioSystem::Init failed; replacing it with the null object" << std::endl;
        audio_ = &nullAudio_;
        audio_->Init();
    }
    std::cerr << "[Runtime Wire] stage AudioSystem::Init ok" << std::endl;

    input_ = hooks.inputCreate ? hooks.inputCreate() : nullptr;
    void* inputSlot = nullptr;
    if (!registry_.Wire("InputSystem", &inputSlot, input_, [this]() -> void* { return static_cast<IInputSystem*>(&nullInput_); }))
        return Halt(Stage::Input, "input system is null and no fallback is available");
    input_ = static_cast<IInputSystem*>(inputSlot);
    if (!input_->Init()) return Halt(Stage::Input, "input system Init returned false");
    std::cerr << "[Runtime Wire] stage InputSystem::Init ok" << std::endl;

    void* networkSlot = nullptr;
    registry_.Wire("NetworkSystem", &networkSlot, nullptr, [this]() -> void* { return static_cast<INetworkSystem*>(&nullNetwork_); });
    network_ = static_cast<INetworkSystem*>(networkSlot);

    if (hooks.wireGlobalDependencies && !hooks.wireGlobalDependencies())
        return Halt(Stage::Input, "WireGlobalDependencies reported an unbound global");

    registry_.PrintMap();
    if (!hooks.mainSimulationLoop) return Halt(Stage::SimulationLoop, "hook mainSimulationLoop is not set");
    std::cerr << "[Runtime Wire] stage MainSimulationLoop enter" << std::endl;
    const int code = hooks.mainSimulationLoop();
    std::cerr << "[Runtime Wire] stage MainSimulationLoop exit code " << code << std::endl;
    return code;
}

}  // namespace runtime
