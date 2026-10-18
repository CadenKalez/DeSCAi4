// cSCParticleSimulatorBase (21 functions, 0x1005f9e84..0x100702c30): the base of the particle effect simulators.
// It owns the live particle list and a small start / stop state machine; subclasses supply Update behaviour and
// override CreateParticle / DestroyParticle. Layout: +8 initialised, +9 started, +0xc refcount, +0x10 delayed stop
// requested, +0x11 shut down and release once the last particle dies, +0x18 particle list (+0x28 its size),
// +0x30 number of live particles.
// A particle is a malloc'ed record whose first byte is its "alive" flag; the base allocates
// max(_sSCParticleAllocator, 0x1c) bytes (the static initialiser value is 0x16 but 0x1c bytes are cleared).
#pragma once
#include <cstdint>
#include <cstdlib>
#include <list>

namespace sc4 {

struct cSCParticle {
    std::uint8_t alive;
    std::uint8_t pad[0x1b];
};

class cSCParticleSimulatorBase {
public:
    cSCParticleSimulatorBase() = default;
    virtual ~cSCParticleSimulatorBase() = default;

    bool QueryInterface(std::uint32_t iid, void** out);
    std::uint32_t AddRef() { return ++refCount_; }
    virtual std::uint32_t Release();
    virtual bool Init();
    virtual bool Shutdown();
    virtual bool Start();
    virtual bool IsStarted() const { return started_; }
    virtual bool Stop(bool delayed, bool releaseWhenDone);
    virtual bool IsInDelayedStop() const { return delayedStop_; }
    virtual void FinalRelease();
    virtual void AddParticle(cSCParticle* particle);
    virtual void RemoveParticle(cSCParticle* particle) { particle->alive = 0; }
    virtual const std::list<cSCParticle*>& GetParticles() const { return particles_; }
    virtual std::uint32_t RefCount() const { return refCount_; }
    virtual cSCParticle* CreateParticle();
    virtual void DestroyParticle(cSCParticle* particle) { std::free(particle); }
    bool CheckForStop();

    int LiveParticleCount() const { return liveCount_; }
    void NoteParticleDied() { --liveCount_; }
    static inline std::uint32_t sParticleSize = 0x16;   // _sSCParticleAllocator

protected:
    bool initialised_ = false;      // +8
    bool started_ = false;          // +9
    std::uint32_t refCount_ = 0;    // +0xc
    bool delayedStop_ = false;      // +0x10
    bool releaseWhenDone_ = false;  // +0x11
    std::list<cSCParticle*> particles_;   // +0x18
    int liveCount_ = 0;             // +0x30
};

}  // namespace sc4
