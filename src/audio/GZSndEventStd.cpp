#include "audio/GZSndEventStd.h"

cGZSndEventStd::cGZSndEventStd() { SetType(kGZMessageTypeSndEvent); }

cGZSndEventStd::cGZSndEventStd(std::uint32_t eventId, std::uint32_t data1, std::uint32_t data2,
                               std::uint32_t data3, std::uint32_t data4) {
    SetType(kGZMessageTypeSndEvent);
    SetEventId(eventId);
    SetData1(data1);
    SetData2(data2);
    SetData3(data3);
    SetData4(data4);
}

bool cGZSndEventStd::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZCLSID_cGZSndEventStd) {
        *out = static_cast<cIGZSndEventStd*>(this);
        AddRef();
        return true;
    }
    return cRZMessage2Standard::QueryInterface(iid, out);
}
