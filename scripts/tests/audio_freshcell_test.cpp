#include "audio/FreshCell.h"
#include <cassert>
#include <cstdio>

int main() {
    cFreshCell* c = cFreshCell::Create();
    // constructor defaults
    assert(c->GetVolume() == 1024 && c->GetPanX() == 512 && c->GetPanZ() == 512);
    assert(c->GetLoopLength() == 8 && c->GetSampleLooped() && !c->GetStereoSample());
    assert(c->GetSndId() == 0 && c->GetQuantize() == 0 && c->GetFadeInTime() == 0 && !c->GetUseFastTimer());
    // setters/getters
    assert(c->SetSndId(0xAB) && c->GetSndId() == 0xAB);
    assert(c->SetVolume(-5) && c->GetVolume() == -5 && c->SetProbability(70) && c->GetProbability() == 70);
    assert(c->SetVolIsRandom(true) && c->GetVolIsRandom() && !c->GetPanIsRandom());
    assert(c->SetGroupId(9) && c->GetGroupId() == 9 && c->SetStereoSample(true) && c->GetStereoSample());
    cRZString name("loop.wav");
    assert(c->SetFilename(name) && c->GetFilename()->Strlen() == 8);
    c->connections().push_back({3, 4});
    // QueryInterface / refcount semantics
    void* p = nullptr;
    assert(c->QueryInterface(kGZIID_cIFreshCell, &p) && p == c);
    assert(c->AddRef() == 2);
    assert(c->Release() == 1);
    assert(c->Release() == 0);            // deleted
    cFreshCell* d = cFreshCell::Create();
    assert(!d->QueryInterface(0x1234, &p));
    assert(d->Release() == 0);
    std::puts("freshcell ok");
}
