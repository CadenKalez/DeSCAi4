// MacTools: CD probe, sleep, the two event posters (including the Windows-message filter) and the two
// message boxes (main-thread only, the flag quirk and the CoreFoundation round trip).
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "platform/MacTools.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace MacTools;

namespace {

struct FakeHost final : IMacToolsHost {
    int registered = 0;
    int registerCalls = 0;
    std::vector<SdlEvent> events;
    bool mainThread = true;
    void* window = reinterpret_cast<void*>(0x1234);
    std::uint32_t lastFlags = 0;
    std::string lastTitle, lastText;
    int boxes = 0;
    std::vector<std::string> created;
    std::vector<void*> released;
    bool convertOk = true;

    int RegisterEvents(int count) override { ++registerCalls; registered += count; return 77; }
    int PushEvent(const SdlEvent& event) override { events.push_back(event); return 1; }
    bool IsMainThread() override { return mainThread; }
    void* MainWindow() override { return window; }
    void ShowMessageBox(std::uint32_t flags, const char* title, const char* text, void* w) override {
        lastFlags = flags; lastTitle = title; lastText = text; ++boxes;
        CHECK(w == window);
    }
    void* CreateCFString(const char* utf8) override { created.emplace_back(utf8); return reinterpret_cast<void*>(0x5678); }
    bool CFStringToUTF8(const void* cfString, char* out, int size) override {
        if (!convertOk) return false;
        CHECK(cfString == reinterpret_cast<const void*>(0x5678));
        std::snprintf(out, std::size_t(size), "%s", created.empty() ? "" : created.back().c_str());
        return true;
    }
    void ReleaseCFString(void* cfString) override { released.push_back(cfString); }
};

}  // namespace

int main() {
    FakeHost host;

    // Without a host nothing crashes and nothing is posted.
    SetHost(nullptr);
    CHECK(!PostPrivateEvent(1, 2, 3));
    CHECK(!PostWindowsEvent(0x500, 0, 0));
    MessageBoxA("no host", 1);
    SetHost(&host);

    // ---- FindCDROM: always null (0x100796c12) ----
    CHECK(FindCDROM("SimCity 4") == nullptr);
    CHECK(FindCDROM(nullptr) == nullptr);

    // ---- Sleep (0x100796c1a) ----
    {
        const auto start = std::chrono::steady_clock::now();
        Sleep(0);                                   // a zero sleep still yields, but must not hang
        Sleep(20u);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count();
        CHECK(ms >= 10);
        CHECK(ms < 2000);
    }

    // ---- PostPrivateEvent (0x100796ca6): registers once, fills type / +0x0c / +0x18 / +0x20 ----
    {
        host.events.clear();
        host.registerCalls = 0;
        CHECK(PostPrivateEvent(0x1'0000'0042ULL, 0xaaa, 0xbbb));
        CHECK(host.registerCalls == 1);
        CHECK(host.events.size() == 1);
        CHECK(host.events[0].type == 77);
        CHECK(host.events[0].message == 0x42);      // only the low half of the message id
        CHECK(host.events[0].paramA == 0xaaa && host.events[0].paramB == 0xbbb);
        CHECK(PostPrivateEvent(5, 6, 7));
        CHECK(host.registerCalls == 1);             // the event type is registered only once
        CHECK(host.events.size() == 2 && host.events[1].message == 5);
    }

    // ---- PostWindowsEvent (0x100796c6d): the translated messages are dropped ----
    {
        struct Case { std::uint64_t id; bool forwarded; };
        const Case cases[] = {
            {0x200, false}, {0x201, false}, {0x202, false}, {0x203, true}, {0x204, false}, {0x205, false},
            {0x206, true}, {0x207, true}, {0x208, true}, {0x209, true}, {0x20a, false}, {0x20b, true},
            {0x100, false}, {0x101, false}, {0x102, true}, {0x0ff, true}, {0x1c, false}, {0x1d, true},
            {0x0010, true}, {0xffffffffULL, true},
        };
        for (const Case& c : cases) {
            host.events.clear();
            const bool pushed = PostWindowsEvent(c.id, 1, 2);
            if (pushed != c.forwarded) std::printf("case 0x%llx wanted %d got %d\n",
                                                   static_cast<unsigned long long>(c.id), int(c.forwarded), int(pushed));
            CHECK(pushed == c.forwarded);
            CHECK(host.events.size() == (c.forwarded ? 1u : 0u));
            if (pushed) CHECK(host.events[0].message == std::uint32_t(c.id));
        }
        // the dropped ids never reach the host at all
        host.events.clear();
        CHECK(!PostWindowsEvent(0x200, 0, 0));
        CHECK(!PostWindowsEvent(0x100, 0, 0));
        CHECK(!PostWindowsEvent(0x1c, 0, 0));
        CHECK(host.events.empty());
    }

    // ---- message boxes ----
    {
        host.boxes = 0;
        host.created.clear();
        host.released.clear();
        host.mainThread = false;
        MessageBoxA("off thread", 1);
        CHECK(host.boxes == 0);                        // nothing off the main thread
        // the char* overload still builds and releases its CFString first, as the original does
        CHECK(host.created.size() == 1 && host.released.size() == 1);

        host.mainThread = true;
        host.created.clear();
        host.released.clear();
        MessageBoxA("hello", 1);
        CHECK(host.boxes == 1);
        CHECK(host.lastText == "hello");
        CHECK(host.lastFlags == kMessageBoxInformation);   // quirk: the flag is not forwarded
        CHECK(host.created.size() == 1 && host.created[0] == "hello");
        CHECK(host.released.size() == 1);

        host.boxes = 0;
        MessageBoxA("warning", 0);
        CHECK(host.boxes == 1 && host.lastFlags == kMessageBoxInformation);

        // the CFString overload honours its own flag
        host.created.clear();
        host.boxes = 0;
        MessageBoxA(reinterpret_cast<const void*>(0x5678), 1);
        CHECK(host.boxes == 1 && host.lastFlags == kMessageBoxError);

        host.boxes = 0;
        host.convertOk = false;
        MessageBoxA("unconvertible", 1);
        CHECK(host.boxes == 0);                        // a failed conversion shows nothing
        host.convertOk = true;

        // null arguments are ignored
        host.boxes = 0;
        MessageBoxA(static_cast<char const*>(nullptr), 1);
        MessageBoxA(static_cast<const void*>(nullptr), 1);
        CHECK(host.boxes == 0);
    }

    SetHost(nullptr);
    if (failures == 0) std::printf("mactools: ok\n");
    return failures == 0 ? 0 : 1;
}
