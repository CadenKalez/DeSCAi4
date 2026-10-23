#include <cstdio>

#include "rz/PortMemoryStreams.h"
#include "sc4/AutomataState.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Automaton : IAutomataStateAutomaton { int cancels = 0; void CancelState() override { ++cancels; } };
struct Controller final : IAutomataStateController {
    int refs = 1;
    bool finished = false;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool IsFinished() const override { return finished; }
};
}  // namespace

int main() {
    tAutomataState state;
    state.stateId = 7; state.progress = 0.25f; state.flag = true; state.counterA = 3; state.counterB = 4; state.counterC = 5;
    PortMemoryOStream os;
    CHECK(state.Write(os));
    tAutomataState loaded;
    loaded.runtime = &loaded;
    PortMemoryIStream is(os.bytes().data(), os.bytes().size());
    CHECK(loaded.Read(is));
    CHECK(loaded.stateId == 7 && loaded.progress == 0.25f && loaded.flag && loaded.counterA == 3 && loaded.counterB == 4 && loaded.counterC == 5);
    CHECK(loaded.runtime == nullptr && loaded.controller == nullptr);

    // old stream (version 2): no third counter
    {
        PortMemoryOStream old;
        old.SetUint16(2);
        old.SetUint32(1); old.SetFloat32(2.0f); old.SetUint8(0); old.SetUint32(8); old.SetUint32(9);
        PortMemoryIStream in(old.bytes().data(), old.bytes().size());
        tAutomataState s;
        s.counterC = 99;
        CHECK(s.Read(in) && s.stateId == 1 && s.counterA == 8 && s.counterB == 9 && s.counterC == 99);
        const std::uint8_t newer[2] = {4, 0};
        PortMemoryIStream future(newer, 2);
        CHECK(!s.Read(future));
    }
    // shutdown: controller released, automaton cancelled only for a non-zero counter
    Controller c;
    Automaton a;
    state.controller = &c;
    state.counterA = 0;
    state.Shutdown(&a);
    CHECK(c.refs == 0 && !state.controller && a.cancels == 0);
    state.counterA = 1;
    state.Shutdown(&a);
    CHECK(a.cancels == 1);
    // a finished controller is not written (the writer only skips the nested object, so the plain stream is the same)
    c.refs = 1; c.finished = true; state.controller = &c;
    PortMemoryOStream os2;
    CHECK(state.Write(os2) && os2.bytes().size() == os.bytes().size());
    if (failures) { std::printf("sc4_automatastate: %d failure(s)\n", failures); return 1; }
    std::printf("sc4_automatastate OK\n");
    return 0;
}
