// cIGZVariant — typed value holder. Only the cIGZUnknown part is declared here; the ~100 typed
// accessor slots (GetValXxx/SetValXxx, see cRZVariant's vtable @ 0x100a6a7a0) are not recovered yet.
#pragma once
#include "gz/cIGZUnknown.h"

class cIGZVariant : public cIGZUnknown {};
