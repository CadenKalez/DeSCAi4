// SFX — the three accessors the game uses to reach the audio COM director's managers (3 functions,
// 0x1003ba4b2, 0x1003efc28, 0x10040d1ba). Each one asks GetSC4AudioCOMDirector() for the director,
// reads one member pointer out of it and hands it back *borrowed*.
//
// Quirk kept: every accessor calls AddRef (vslot 0x08) and then Release (vslot 0x10) on the member
// before returning it, so the reference count is unchanged - the pair is a no-op that the original
// still performs. A missing director or a null member answers null.
//
// Director member offsets (cSC4AudioCOMDirector): +0xa0 ambience manager, +0xb8 sound manager,
// +0xc0 listener.
#pragma once

namespace sfx {

class ISfxHost {
public:
    virtual ~ISfxHost() = default;
    virtual void* Director() = 0;            // GetSC4AudioCOMDirector()
    virtual void AddRefUnknown(void* p) = 0;   // vslot 0x08
    virtual void ReleaseUnknown(void* p) = 0;  // vslot 0x10
};

ISfxHost* Host();
void SetHost(ISfxHost* host);

void* Listener();           // @ 0x1003ba4b2: director + 0xc0
void* SoundManager();       // @ 0x1003efc28: director + 0xb8
void* AmbienceManager();    // @ 0x10040d1ba: director + 0xa0

}  // namespace sfx
