#include "audio/AudioServices.h"

namespace Audio {
namespace {
cIGZSndSys* g_soundSystem = nullptr;
cIGZPersistRecordService* g_persist = nullptr;
IUTalkCodec* g_utalk = nullptr;
}  // namespace

cIGZSndSys* SoundSystem() { return g_soundSystem; }
void SetSoundSystem(cIGZSndSys* system) { g_soundSystem = system; }
cIGZPersistRecordService* PersistService() { return g_persist; }
void SetPersistService(cIGZPersistRecordService* service) { g_persist = service; }
IUTalkCodec* UTalkCodec() { return g_utalk; }
void SetUTalkCodec(IUTalkCodec* codec) { g_utalk = codec; }

}  // namespace Audio
