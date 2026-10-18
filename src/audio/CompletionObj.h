// cCompletionObj (8 functions, 0x1004aa2c0..0x1004aff50, iid 0x1b3637c6): the do-nothing sound completion
// callback the Hit manager installs when a sound needs one. Reference counted from 0; deletes itself at 0.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIGZSndCompletionFunctionObject.h"

class cCompletionObj final : public cIGZSndCompletionFunctionObject {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != 0x1b3637c6 && iid != 1) return false;
        *out = this;
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override {
        if (--refCount_ == 0) {
            delete this;
            return 0;
        }
        return static_cast<std::uint32_t>(refCount_);
    }
    void OnSoundCompleted(cIGZSnd*) override {}

private:
    int refCount_ = 0;   // +8
};
