#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace runtime {

enum class Stage {
    FileSystem,
    Config,
    MemoryPool,
    Graphics,
    Audio,
    Input,
    SimulationLoop,
    Complete
};

const char* StageName(Stage stage);

enum class InputType { KeyDown, KeyUp, MouseMove, MouseDown, MouseUp, MouseWheel, Char, Close, Resize, Unknown };

struct InputEvent {
    InputType type;
    std::uint32_t code;
    std::int32_t x;
    std::int32_t y;
    std::uint32_t modifiers;
    std::uint64_t sequence;
};

class MainThreadQueue {
public:
    MainThreadQueue();
    void Push(InputEvent event);
    std::size_t Drain(const std::function<void(const InputEvent&)>& consumer);
    std::size_t Size() const;

private:
    mutable std::mutex mutex_;
    std::deque<InputEvent> events_;
    std::uint64_t nextSequence_;
};

MainThreadQueue& InputQueue();

// Maps a legacy Win32 message (WM_* value, wParam, lParam) to an InputEvent and pushes it to the main-thread queue.
// Returns false (and logs the hexadecimal message id) for messages that have no mapping.
bool TranslateLegacyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam);

class IAudioSystem {
public:
    virtual ~IAudioSystem() {}
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;
    virtual bool PlaySound(std::uint32_t soundId) = 0;
    virtual void StopAll() = 0;
    virtual void SetMasterVolume(float volume) = 0;
};

class IInputSystem {
public:
    virtual ~IInputSystem() {}
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;
    virtual bool PollEvent(InputEvent& out) = 0;
    virtual bool IsKeyDown(std::uint32_t key) const = 0;
};

class INetworkSystem {
public:
    virtual ~INetworkSystem() {}
    virtual bool Connect(const std::string& address) = 0;
    virtual void Disconnect() = 0;
    virtual bool Send(const void* data, std::size_t size) = 0;
    virtual std::size_t Receive(void* buffer, std::size_t capacity) = 0;
};

class NullAudioSystem : public IAudioSystem {
public:
    NullAudioSystem();
    bool Init() override;
    void Shutdown() override;
    bool PlaySound(std::uint32_t soundId) override;
    void StopAll() override;
    void SetMasterVolume(float volume) override;
    std::uint64_t CallCount() const;

private:
    void Note(const char* name, std::uint32_t argument);
    mutable std::mutex mutex_;
    std::uint64_t calls_;
    bool reported_;
};

class NullInputSystem : public IInputSystem {
public:
    NullInputSystem();
    bool Init() override;
    void Shutdown() override;
    bool PollEvent(InputEvent& out) override;
    bool IsKeyDown(std::uint32_t key) const override;

private:
    mutable std::mutex mutex_;
    bool reported_;
};

class NullNetworkSystem : public INetworkSystem {
public:
    NullNetworkSystem();
    bool Connect(const std::string& address) override;
    void Disconnect() override;
    bool Send(const void* data, std::size_t size) override;
    std::size_t Receive(void* buffer, std::size_t capacity) override;

private:
    mutable std::mutex mutex_;
    bool reported_;
};

struct WireEntry {
    std::string name;
    void* address;
    bool fallback;
};

// Explicit wiring table: every orphan global the decompiler separated is bound here, with its address logged.
class WireRegistry {
public:
    // Stores `value` into `*slot`. A null value takes `fallbackFactory()` when one is given, otherwise the wire fails.
    bool Wire(const std::string& name, void** slot, void* value, const std::function<void*()>& fallbackFactory);
    bool Require(const std::string& name, void* value);
    void PrintMap() const;
    std::size_t Count() const;
    const std::vector<WireEntry>& Entries() const;

private:
    mutable std::mutex mutex_;
    std::vector<WireEntry> entries_;
};

struct SubsystemHooks {
    std::function<bool()> fileSystemInit;
    std::function<bool()> configLoad;
    std::function<void*()> memoryPoolAlloc;
    // Returns the driver object; null means the graphics system failed.
    std::function<void*()> graphicsCreate;
    // True when the driver exposes a current video mode with id 0 and a non-zero size.
    std::function<bool(void* driver)> graphicsExposesMode0;
    std::function<IAudioSystem*()> audioCreate;
    std::function<IInputSystem*()> inputCreate;
    std::function<bool()> wireGlobalDependencies;
    std::function<int()> mainSimulationLoop;
};

class EngineLifecycle {
public:
    EngineLifecycle();
    // Runs the rigid boot sequence. Returns the loop's exit code, or a non-zero failure code after logging the missing dependency.
    int Run(const SubsystemHooks& hooks);
    Stage FailedStage() const { return failedStage_; }
    const WireRegistry& Registry() const { return registry_; }
    WireRegistry& Registry() { return registry_; }
    IAudioSystem* Audio() const { return audio_; }
    IInputSystem* Input() const { return input_; }
    INetworkSystem* Network() const { return network_; }
    void* Graphics() const { return graphics_; }
    void* Memory() const { return memory_; }

private:
    int Halt(Stage stage, const std::string& missing);

    WireRegistry registry_;
    Stage failedStage_;
    void* graphics_;
    void* memory_;
    IAudioSystem* audio_;
    IInputSystem* input_;
    INetworkSystem* network_;
    NullAudioSystem nullAudio_;
    NullInputSystem nullInput_;
    NullNetworkSystem nullNetwork_;
};

}  // namespace runtime
