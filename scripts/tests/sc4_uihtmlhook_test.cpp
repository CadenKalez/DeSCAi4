#include <cstdio>
#include <cstring>
#include <map>

#include "fake_resourcemanager.h"
#include "gz/cIGZPersistDBSegment.h"
#include "sc4/UIHTMLHook.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
using namespace sc4;

struct Segment : cIGZPersistDBSegmentStub {
    std::map<std::uint64_t, std::string> docs;
    int releases = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    static std::uint64_t Id(const cGZPersistResourceKey& k) { return (std::uint64_t(k.group) << 32) ^ k.instance ^ (std::uint64_t(k.type) << 8); }
    std::uint32_t ReadRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t& size) override {
        auto it = docs.find(Id(key));
        if (it == docs.end()) return 0xffffffffu;
        if (buffer) std::memcpy(buffer, it->second.data(), size);
        return std::uint32_t(it->second.size());
    }
    std::uint32_t Release() override { ++releases; return 1; }
};
struct Manager : FakeResourceManager {
    Segment* segment = nullptr;
    bool found = true;
    int adds = 0, releases = 0;
    std::uint32_t AddRef() override { return ++adds; }
    std::uint32_t Release() override { return ++releases; }
    bool FindDBSegment(const cGZPersistResourceKey&, cIGZPersistDBSegment** out) override { if (!found) return false; *out = segment; return true; }
};
struct URL : IHTMLURL {
    cRZString full, scheme;
    const cRZString& GetFullURL() override { return full; }
    const cRZString& GetScheme() override { return scheme; }
};

int main() {
    cGZPersistResourceKey key;
    CHECK(cSC4UIHTMLHook::KeyFromURL("sc4://html/0123abcd", key) && key.type == 0x856ddbac && key.group == 0x0a133fc0 && key.instance == 0x0123abcd);
    CHECK(cSC4UIHTMLHook::KeyFromURL("sc4://HTML/0123abcd", key) && key.group == 0x0a133fc0);   // case-folded
    CHECK(cSC4UIHTMLHook::KeyFromURL("sc4://image/deadbeef", key) && key.group == 0xca133ecb && key.instance == 0xdeadbeef);
    CHECK(cSC4UIHTMLHook::KeyFromURL("sc4://other/deadbeef", key) && key.group == 0 && key.instance == 0xdeadbeef);
    CHECK(cSC4UIHTMLHook::KeyFromURL("sc4://x/1234abcd/00000010", key) && key.group == 0x1234abcd && key.instance == 0x10);
    CHECK(cSC4UIHTMLHook::KeyFromURL("sc4://html/abc", key) && key.group == 0x0a133fc0 && key.instance == 0);   // fewer than 8 characters: no instance
    CHECK(cSC4UIHTMLHook::KeyFromURL("sc4:////html//00000001", key) && key.group == 0x0a133fc0 && key.instance == 1);   // empty parts skipped
    CHECK(!cSC4UIHTMLHook::KeyFromURL("http://html/00000001", key));
    CHECK(!cSC4UIHTMLHook::KeyFromURL("sc4://", key));

    Segment segment;
    Manager manager;
    manager.segment = &segment;
    cGZPersistResourceKey docKey{0x856ddbac, 0x0a133fc0, 0x11};
    segment.docs[Segment::Id(docKey)] = "<html>hi</html>";

    cSC4UIHTMLHook hook(&manager);
    CHECK(hook.Init() && manager.adds == 1);
    hook.Init();   // the same manager again: no extra reference
    CHECK(manager.adds == 1 && manager.releases == 0);
    URL url;
    url.scheme = cRZString("SC4");
    CHECK(hook.TestURL(url));
    url.scheme = cRZString("http");
    CHECK(!hook.TestURL(url));
    url.full = cRZString("sc4://html/00000011");
    std::string text = "stale";
    CHECK(hook.LoadDocument(url, text) && text == "<html>hi</html>" && segment.releases == 1);
    url.full = cRZString("sc4://html/00000012");
    CHECK(!hook.LoadDocument(url, text) && segment.releases == 2);   // absent record: the segment is still released
    url.full = cRZString("ftp://html/00000011");
    CHECK(!hook.LoadDocument(url, text));
    manager.found = false;
    CHECK(!hook.LoadDocument(docKey, text));
    manager.found = true;

    void* iface = nullptr;
    CHECK(hook.QueryInterface(kGZIID_cISC4UIHTMLHook, &iface) && iface == &hook && hook.RefCount() == 1);
    CHECK(!hook.QueryInterface(0x1234, &iface));
    CHECK(hook.Shutdown() && manager.releases == 1);
    hook.Shutdown();
    CHECK(manager.releases == 1);   // nothing left to release
    std::printf(g_fail ? "FAILED %d\n" : "sc4_uihtmlhook OK\n", g_fail);
    return g_fail ? 1 : 0;
}
