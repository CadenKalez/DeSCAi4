#include <cstdio>
#include <thread>

#include "runtime/engine_lifecycle_wire.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    using namespace runtime;
    static int memoryBlock = 0, driver = 0;
    SubsystemHooks good;
    good.fileSystemInit = [] { return true; };
    good.configLoad = [] { return true; };
    good.memoryPoolAlloc = []() -> void* { return &memoryBlock; };
    good.graphicsCreate = []() -> void* { return &driver; };
    good.graphicsExposesMode0 = [](void*) { return true; };
    good.mainSimulationLoop = [] {
        std::thread producer([] { for (int i = 0; i < 100; ++i) TranslateLegacyMessage(0x0100, 'A', 0); });
        producer.join();
        TranslateLegacyMessage(0x9999, 0, 0);
        return static_cast<int>(InputQueue().Size());
    };
    {
        EngineLifecycle life;
        CHECK(life.Run(good) == 100);
        CHECK(life.Audio() != nullptr && life.Input() != nullptr && life.Network() != nullptr);
        CHECK(life.Audio()->PlaySound(0x1234));
        CHECK(!life.Network()->Connect("x") && life.Network()->Receive(nullptr, 0) == 0);
        std::size_t seen = 0;
        InputQueue().Drain([&seen](const InputEvent& e) { if (e.type == InputType::KeyDown && e.code == 'A') ++seen; });
        CHECK(seen == 100);
    }
    {
        SubsystemHooks bad = good;
        bad.graphicsCreate = []() -> void* { return nullptr; };
        EngineLifecycle life;
        CHECK(life.Run(bad) != 0 && life.FailedStage() == Stage::Graphics);
    }
    {
        SubsystemHooks bad = good;
        bad.graphicsExposesMode0 = [](void*) { return false; };
        EngineLifecycle life;
        CHECK(life.Run(bad) != 0 && life.FailedStage() == Stage::Graphics);
    }
    {
        SubsystemHooks bad = good;
        bad.configLoad = [] { return false; };
        EngineLifecycle life;
        CHECK(life.Run(bad) != 0 && life.FailedStage() == Stage::Config);
    }
    {
        WireRegistry registry;
        void* slot = nullptr;
        CHECK(!registry.Wire("Orphan", &slot, nullptr, nullptr) && slot == nullptr);
        CHECK(registry.Wire("Orphan", &slot, &driver, nullptr) && slot == &driver);
    }
    if (g_fail == 0) std::printf("engine_lifecycle ok\n");
    return g_fail ? 1 : 0;
}
