// cIGZSndViewUpdateTarget — told when the listener/view changed. Slot 3 (+0x18).
#pragma once
#include "gz/cIGZUnknown.h"

class cIGZSndViewUpdateTarget : public cIGZUnknown {
public:
    virtual void UpdateView() = 0;  // [3]
};
