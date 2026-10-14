// Framework services the sound classes look up in the original (cRZSysServPtr<...>): the port
// supplies them here. All pointers are non-owning and may be null (the original null-checks some
// and crashes on others; see the individual classes).
#pragma once
#include <cstddef>
#include <cstdint>

#include "audio/interfaces/cIGZSndSys.h"
#include "gz/cIGZPersistRecord.h"

namespace Audio {

cIGZSndSys* SoundSystem();                     // service 0x23cecba0 / iid 0x106077d2
void SetSoundSystem(cIGZSndSys* system);
cIGZPersistRecordService* PersistService();    // service 0x056b906e / iid 0x656b8efc
void SetPersistService(cIGZPersistRecordService* service);

// The EA "UTalk" speech codec used by cGZSndSegmentDataSourceUTalk (_utalkinit @ 0x1004da172,
// _utalkdecode @ 0x1004da29b; part of the SND library). The 0xd48-byte state block and its
// decoded-sample table (432 dwords at +0x688) are laid out as the data source expects.
struct UTalkState {
    std::uint8_t bytes[0xd48];
};
class IUTalkCodec {
public:
    virtual ~IUTalkCodec() = default;
    // `compressed` stays valid for as long as the state is decoded from.
    virtual void Init(const std::uint8_t* compressed, UTalkState& state) = 0;
    // Decodes the next frame; the 432 samples are readable through Samples().
    virtual void Decode(UTalkState& state) = 0;
    virtual const std::uint32_t* Samples(const UTalkState& state) const = 0;
};
IUTalkCodec* UTalkCodec();
void SetUTalkCodec(IUTalkCodec* codec);

}  // namespace Audio
