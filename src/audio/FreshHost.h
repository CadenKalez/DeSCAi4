// Seam between the Freshness (music) engine and the rest of the application.
// The original reaches the sound manager (RZ::SM() @ 0x100127e78) and the sound system (RZ::SS()
// @ 0x100127e0c) through globals; the port injects them here so the engine can be tested in isolation.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIFreshPlayer.h"
#include "audio/interfaces/cIGZSnd.h"
#include "audio/interfaces/cIGZSndUpdateTarget.h"
#include <string>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZString.h"

namespace Fresh {

class cIFreshHost {
public:
    virtual ~cIFreshHost() = default;
    // RZ::SS() != nullptr
    virtual bool HasSoundSystem() const = 0;
    // Sound manager slot +0x90: creates a sound for a file name; the result is AddRef'd (caller releases).
    virtual cIGZSnd* CreateSndByFileName(const cIGZString& fileName) = 0;
    // Sound manager slot +0x88: creates a sound for a resource id (taken from a "...GUID" filename).
    virtual cIGZSnd* CreateSndByResourceId(std::uint32_t resourceId) = 0;
    // Sound system slot +0x188 (cGZSndSys::GetTestMode): the dimension player does nothing in test mode.
    virtual bool IsSoundSystemInTestMode() const = 0;
    // GZCOM()->CreateInstance(kClsidFreshPlayer, kGZIID_cIFreshPlayer): a fresh, AddRef'd cFreshPlayer.
    virtual cIFreshPlayer* CreateFreshPlayerInstance() = 0;
    // Sound system slot +0xf0 / +0xf8: periodic Update(int) registration (the engine uses 250 ms).
    virtual void AddUpdateTarget(cIGZSndUpdateTarget* target, std::uint32_t intervalMs) = 0;
    virtual void RemoveUpdateTarget(cIGZSndUpdateTarget* target) = 0;
    // Reads the text of the score stored under a persistent-resource key (the original goes through
    // the resource manager and a DB-record file adapter). False if the resource is missing.
    virtual bool ReadScoreText(const cGZPersistResourceKey& key, std::string& outText) = 0;
};

// Supplied by the application; may be null (the original checks RZ::SS()/SM() for null).
cIFreshHost* GetHost();
void SetHost(cIFreshHost* host);

}  // namespace Fresh
