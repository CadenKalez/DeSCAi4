#include <cstdio>

#include "rz/cRZString.h"
#include "sc4/TickerMessage.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
static void Done(std::uint32_t, void*) {}

int main() {
    auto* text = new cRZString("headline");
    text->AddRef();   // our reference
    cGZMessage click; click.type = 7; click.param1 = 1; click.param2 = 2; click.param3 = 3;
    int ctx = 0;
    {
        TickerMessage m(5, text, nullptr, 10, 11, 12, 13, 14, &click, Done, &ctx);
        CHECK(m.id_ == 5 && m.word0c_ == 12 && m.word30_ == 10 && m.word34_ == 11 && m.word38_ == 13 && m.word3c_ == 14);
        CHECK(m.message_.type == 7 && m.message_.param1 == 1 && m.message_.param2 == 2 && m.message_.param3 == 3);
        CHECK(m.callback_ == Done && m.context_ == &ctx);
        CHECK(m.state20_ == 0xfffe && m.state28_ == 0xffff);
        CHECK(m.text_ == text);
    }   // destructor drops the ticker's reference: our own remains
    CHECK(text->AddRef() == 2);   // ours + this probe (the ticker's is gone)
    TickerMessage empty(1, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr);
    CHECK(empty.message_.type == 0 && empty.message_.param3 == 0 && empty.text_ == nullptr);
    text->Release(); text->Release();
    std::printf(g_fail ? "FAILED %d\n" : "sc4_tickermessage OK\n", g_fail);
    return g_fail ? 1 : 0;
}
