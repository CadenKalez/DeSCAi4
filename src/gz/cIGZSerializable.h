// cIGZSerializable — objects that can write themselves to / read themselves from a stream.
// Slots 3..5 (byte offsets +0x18..+0x28), from cSC424HourClock's cIGZSerializable sub-vtable.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZIStream;
class cIGZOStream;

class cIGZSerializable : public cIGZUnknown {
public:
    virtual bool Write(cIGZOStream& stream) const = 0;  // [3]
    virtual bool Read(cIGZIStream& stream) = 0;         // [4]
    virtual std::uint32_t GetGZCLSID() const = 0;       // [5]
};
