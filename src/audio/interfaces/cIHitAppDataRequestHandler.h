// cIHitAppDataRequestHandler — application callback that supplies "source data" fields to Hit
// tracks. Only slot 3 (+0x18) is called from the recovered code (cHitMan::GetSourceDataField).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIHitAppDataRequestHandler : public cIGZUnknown {
public:
    // [3] Fills *out with the value of field `fieldId` for `sourceId`; the return value of the
    // original is forwarded unchanged by cHitMan::GetSourceDataField.
    virtual bool GetSourceDataField(std::int32_t sourceId, std::int32_t fieldId, std::int32_t* out) = 0;
};
