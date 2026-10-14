// Framework services the sound classes look up in the original (cRZSysServPtr<...>): the port
// supplies them here. All pointers are non-owning and may be null (the original null-checks some
// and crashes on others; see the individual classes).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "audio/interfaces/cIGZSndMgr.h"
#include "audio/interfaces/cIGZSndSys.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZPersistRecord.h"

namespace Audio {

cIGZSndSys* SoundSystem();                     // service 0x23cecba0 / iid 0x106077d2
void SetSoundSystem(cIGZSndSys* system);
cIGZSndMgr* SoundManagerService();             // service 0x1af85bfb / iid 0x3af85bb7
void SetSoundManagerService(cIGZSndMgr* manager);
cIGZMessageServer2* MessageServer();           // service 0x04fa845b / iid 0x652294c7 (RZ::MS2())
void SetMessageServer(cIGZMessageServer2* server);
// The data directory the file system service reports (cIGZFileSystem slot +0x38); empty = none.
const std::string& FileSystemDataDirectory();
void SetFileSystemDataDirectory(const std::string& directory);
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
