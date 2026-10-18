#include "sc4/ParticleSimulatorBase.h"

#include <cstring>

namespace sc4 {

bool cSCParticleSimulatorBase::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSCParticleSimulatorBase::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    return 0;
}

void cSCParticleSimulatorBase::FinalRelease() {
    Shutdown();
    delete this;
}

bool cSCParticleSimulatorBase::Init() {
    if (!initialised_) initialised_ = true;
    return true;
}

bool cSCParticleSimulatorBase::Shutdown() {
    if (initialised_) {
        delayedStop_ = false;
        Stop(false, false);
        initialised_ = false;
    }
    return true;
}

bool cSCParticleSimulatorBase::Start() {
    if ((!started_ || delayedStop_) && !releaseWhenDone_) {
        delayedStop_ = false;
        started_ = true;
        return true;
    }
    return false;
}

// Stop(false, ...) kills every particle at once; Stop(true, release) lets them run out (and, with `release`, takes
// a reference that CheckForStop gives back once the last one is gone).
bool cSCParticleSimulatorBase::Stop(bool delayed, bool releaseWhenDone) {
    if (!started_) return false;
    delayedStop_ = delayed;
    if (!delayed) {
        started_ = false;
        releaseWhenDone_ = false;
        for (cSCParticle* particle : particles_) {
            --liveCount_;
            DestroyParticle(particle);
        }
        particles_.clear();
        return true;
    }
    releaseWhenDone_ = releaseWhenDone;
    if (releaseWhenDone) AddRef();
    return false;
}

bool cSCParticleSimulatorBase::CheckForStop() {
    if (liveCount_ != 0) return false;
    if (delayedStop_) {
        Stop(false, false);
        return true;
    }
    if (!releaseWhenDone_) return false;
    Shutdown();
    Release();
    return true;
}

void cSCParticleSimulatorBase::AddParticle(cSCParticle* particle) {
    particles_.push_back(particle);
    ++liveCount_;
}

cSCParticle* cSCParticleSimulatorBase::CreateParticle() {
    const std::uint32_t size = sParticleSize < sizeof(cSCParticle) ? sizeof(cSCParticle) : sParticleSize;
    auto* particle = static_cast<cSCParticle*>(std::malloc(size));
    std::memset(particle, 0, sizeof(cSCParticle));
    return particle;
}

}  // namespace sc4
