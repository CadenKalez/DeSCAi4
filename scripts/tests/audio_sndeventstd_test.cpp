#include <cassert>
#include <cstdio>

#include "audio/GZSndEventStd.h"

int main() {
    auto* e = new cGZSndEventStd(0xea451903, 100, 7, 0, 0);
    assert(e->GetType() == 0xdb53383b && e->GetHasEventId() && e->GetEventId() == 0xea451903);
    assert(e->GetData1() == 100 && e->GetData2() == 7 && e->GetHasData4());
    int x;
    e->SetVoid1(&x);
    assert(e->GetVoid1() == nullptr);
    assert(!e->GetHasIGZUnknown() && !e->GetHasExtra());
    void* p = nullptr;
    assert(e->QueryInterface(0x5b523ba4, &p));
    auto* std_ = static_cast<cIGZSndEventStd*>(p);
    assert(std_->GetEventId() == 0xea451903 && std_->AsMessage2Standard() == static_cast<cIGZMessage2Standard*>(e));
    assert(e->QueryInterface(0x65297976, &p) && p == static_cast<cIGZMessage2Standard*>(e));
    assert(!e->QueryInterface(0x1234, &p));
    cGZSndEventStd empty;
    assert(!empty.GetHasEventId() && empty.GetType() == 0xdb53383b);
    assert(e->Release() == 1 && e->Release() == 0);   // two QI references; destroyed at 1
    std::puts("sndeventstd ok");
}
