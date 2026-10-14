// cIGZSndDataSourceHolder — owner of a sound data source. Slots 3..4 (+0x18..+0x20).
#pragma once
#include <cstdint>

#include "audio/interfaces/cIGZSnd.h"

class cIGZSndDataSourceHolder : public cIGZUnknown {
public:
    virtual bool SetDataSource(cIGZSndDataSource* source, cIGZSnd::SndStreamingType streaming) = 0;  // [3]
    virtual cIGZSndDataSource* GetDataSource() = 0;                                                   // [4]
};
