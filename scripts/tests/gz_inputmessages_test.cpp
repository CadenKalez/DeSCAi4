#include <cstdio>

#include "gz/cGZInputMessages.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

int main() {
    auto* key = new cGZKeyMessage();
    key->AddRef();
    CHECK(key->GetType() == 0x7a104750 && key->EventType() == 0xffffffff && key->Key() == 0 && key->KeyFlags() == 0);
    key->Initialize(2, 65, 8, 3);
    CHECK(key->EventType() == 2 && key->Key() == 65 && key->KeyFlags() == 8 && key->RepeatCount() == 3);
    void* out = nullptr;
    CHECK(key->Create(kGZIID_cIGZKeyMessage, &out) && out != nullptr && out != key);
    auto* copy = static_cast<cIGZKeyMessage*>(out);
    CHECK(copy->GetType() == 0x7a104750 && copy->EventType() == 0xffffffff);   // a fresh message, not a copy
    CHECK(copy->IsEqual(*key) && !copy->IsLess(*key));
    copy->SetType(5);
    CHECK(copy->IsLess(*key) && !copy->IsEqual(*key));
    copy->Release();
    CHECK(!key->Create(0x1234, &out));
    CHECK(key->QueryInterface(kGZIID_cIGZMessage2, &out) && key->Release() == 1);
    key->Release();

    auto* mouse = new cGZMouseMessage();
    mouse->AddRef();
    CHECK(mouse->GetType() == 0xfa0c6390 && mouse->EventType() == 0xffffffff && mouse->WheelDelta() == 0);
    mouse->Initialize(4, cRZPoint{10, 20}, 0x30, -120);
    CHECK(mouse->Position() == (cRZPoint{10, 20}) && mouse->KeyFlags() == 0x30 && mouse->WheelDelta() == -120 && mouse->EventType() == 4);
    CHECK(!mouse->QueryInterface(kGZIID_cIGZKeyMessage, &out));
    mouse->Release();

    auto* canvas = new cGZCanvasMessage();
    canvas->AddRef();
    CHECK(canvas->GetType() == 0x1a10630a && canvas->EventType() == 0xffffffff);
    canvas->Initialize(7);
    CHECK(canvas->EventType() == 7);
    canvas->Release();
    std::printf(failures ? "FAILED\n" : "gz_inputmessages ok\n");
    return failures != 0;
}
