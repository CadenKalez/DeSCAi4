#include <cassert>
#include <cstdio>

#include "audio/HitControlGroup.h"

int main() {
    auto* g = new cHitControlGroup(42);
    assert(g->GetID() == 42 && g->GetVolume() == 0x400 && g->tracks().empty());
    g->AddTrack(7); g->AddTrack(3); g->AddTrack(7);
    assert(g->tracks().size() == 2 && *g->tracks().begin() == 3);
    assert(g->SetVolume(512) && g->GetVolume() == 512);
    cRZString name("Ambience");
    assert(g->SetName(name));
    cRZString out;
    assert(g->GetName(out) && out.str() == "Ambience");

    // interface mapping: AsIGZSnd is the second base; QueryInterface only knows the group id
    cIGZSnd* snd = g->AsIGZSnd();
    assert(static_cast<void*>(snd) != static_cast<void*>(static_cast<cIHitControlGroup*>(g)));
    assert(snd->GetID() == 42 && snd->GetVolume() == 512 && snd->IsPlaying() && !snd->IsFading());
    snd->SetID(9);
    assert(g->GetID() == 9);
    void* p = nullptr;
    assert(!g->QueryInterface(kGZIID_cIGZSnd, &p));
    assert(g->QueryInterface(kGZIID_cIHitControlGroup, &p) && p == static_cast<cIHitControlGroup*>(g));
    assert(g->Release() == 1);            // 2 -> 1
    assert(g->Release() == 0);            // deleted at count 1
    std::puts("hit control group ok");
}
