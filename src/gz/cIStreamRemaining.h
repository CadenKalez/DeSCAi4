// PORT-ONLY: optional capability of an input stream — it knows how many bytes are left. The
// original reaches the size through the record-access stream view (iid 0xc56b8f08, slot +0x1a0);
// property-set resources need the whole record, so streams that back persisted records implement
// this in the port.
#pragma once
#include <cstddef>

class cIStreamRemaining {
public:
    virtual ~cIStreamRemaining() = default;
    virtual std::size_t RemainingBytes() const = 0;
};
