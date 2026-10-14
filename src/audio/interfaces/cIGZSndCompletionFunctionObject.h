// cIGZSndCompletionFunctionObject — callback object told when a sound finished. Slot 3 (+0x18)
// is invoked by cHitTrackPlayer::Kill with the finished sound.
#pragma once
#include "audio/interfaces/cIGZSnd.h"

class cIGZSndCompletionFunctionObject : public cIGZUnknown {
public:
    virtual void OnSoundCompleted(cIGZSnd* sound) = 0;  // [3]
};
