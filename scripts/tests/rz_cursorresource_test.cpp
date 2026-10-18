#include <cstdio>

#include "audio/CompletionObj.h"
#include "rz/CursorResource.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

struct Cursor final : cIGZCursor {
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid != 0x834c471e && iid != 0x55) return false; *out = this; ++refs; return true; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct Source final : IRZCursorSource {
    Cursor cursor;
    cIGZCursor* CreateCursor() override { ++cursor.refs; return &cursor; }
};

int main() {
    Source source;
    SetRZCursorSource(&source);
    auto* resource = new cRZCursorResource();
    resource->AddRef();
    void* out = nullptr;
    CHECK(source.cursor.refs == 2);
    CHECK(resource->GetCursorAs(0x55, &out) && out == &source.cursor && !resource->GetCursorAs(0x56, &out));
    cIGZCursor* cursor = nullptr;
    CHECK(resource->GetCursor(&cursor) && cursor == &source.cursor);
    CHECK(resource->QueryInterface(0x88696706, &out) == false || out != nullptr);   // the held cursor answers first only for its own ids
    Cursor other;
    CHECK(resource->SetCursor(&other) && other.refs == 2 && source.cursor.refs >= 1);
    CHECK(!resource->LoadFromSerialRecord(nullptr));
    resource->SetCursor(nullptr);
    CHECK(!resource->GetCursorAs(0x55, &out));
    resource->Release();
    resource->Release();

    cRZCursorResourceFactory factory;
    factory.AddRef();
    void* made = nullptr;
    CHECK(factory.CreateInstance(0x686aa4b0u, 0x88696706u, &made) && made);
    static_cast<cIGZUnknown*>(made)->Release();
    CHECK(!factory.CreateInstance(0x1234u, 0x88696706u, &made));
    CHECK(factory.QueryInterface(0x08696779, &out) && factory.QueryInterface(0xa56b8f17, &out) && !factory.QueryInterface(7, &out));

    auto* completion = new cCompletionObj();
    completion->AddRef();
    completion->OnSoundCompleted(nullptr);
    CHECK(completion->QueryInterface(0x1b3637c6, &out) && !completion->QueryInterface(3, &out));
    completion->Release();
    CHECK(completion->Release() == 0);
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
