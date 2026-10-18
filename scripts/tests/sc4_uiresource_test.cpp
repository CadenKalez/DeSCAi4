#include <cstdio>

#include "sc4/UIResourceObjects.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

struct Counted final : cIGZUnknown {
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct Cache final : IGlyphTextureCache {
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t GetBoundIdForTexture(cSC4CachedStringTexture*) override { return 77; }
};
struct Host final : IScriptResourceHost {
    Counted buffer;
    cIGZUnknown* LoadScriptBuffer(const cGZPersistResourceKey&) override { return &buffer; }
    bool LoadString(std::uint32_t group, std::uint32_t instance, cIGZString& out) override {
        if (group == 1 && instance == 2) { out.AssignFromCString("hello"); return true; }
        return false;
    }
};

int main() {
    Counted binding;
    Cache cache;
    auto* texture = new cSC4CachedStringTexture(64, 16, &binding);
    texture->AddRef();
    std::uint32_t w = 0, h = 0;
    texture->GetTextureSize(w, h);
    CHECK(w == 64 && h == 16);
    texture->SetManager(&cache);
    CHECK(cache.refs == 2 && texture->GetTextureId() == 77);
    texture->SetManager(&cache);                 // same manager: nothing changes
    CHECK(cache.refs == 2);
    texture->AddRef();                           // count 2 with a manager: stays alive, answers 0
    CHECK(texture->Release() == 0 && cache.refs == 2);
    CHECK(texture->Release() == 0);              // count 1 -> destroyed
    CHECK(cache.refs == 1 && binding.refs == 0); // the destructor released both

    Counted binding2;
    auto* plain = new cSC4CachedStringTexture(1, 1, &binding2);
    plain->AddRef();
    plain->AddRef();
    CHECK(plain->Release() == 0 && binding2.refs == 0);   // count 2 without a manager destroys at once (original quirk)

    Host host;
    cSC4ScriptServiceResourceFactory factory(&host);
    factory.AddRef();
    cGZPersistResourceKey key{0, 1, 2};
    CHECK(factory.GZUIScriptLoadBuffer(key) == &host.buffer && host.buffer.refs == 2);
    cRZString* text = factory.GZUIScriptLoadString(key);
    CHECK(text && std::string(text->Data()) == "hello");
    text->Release();
    cGZPersistResourceKey missing{0, 9, 9};
    CHECK(factory.GZUIScriptLoadString(missing) == nullptr);
    cRZString out;
    factory.GZUIScriptLoadString(key, out);
    CHECK(std::string(out.Data()) == "hello");
    void* raw = nullptr;
    CHECK(factory.QueryInterface(0x8aa22237, &raw) && !factory.QueryInterface(4, &raw));
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
