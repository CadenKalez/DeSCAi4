#include "audio/AudioServices.h"

namespace Audio {
namespace {
cIGZSndSys* g_soundSystem = nullptr;
cIGZPersistRecordService* g_persist = nullptr;
IUTalkCodec* g_utalk = nullptr;
cIGZMessageServer2* g_messageServer = nullptr;
cIGZSndMgr* g_soundManager = nullptr;
std::string g_dataDirectory;
}  // namespace

cIGZSndSys* SoundSystem() { return g_soundSystem; }
void SetSoundSystem(cIGZSndSys* system) { g_soundSystem = system; }
cIGZSndMgr* SoundManagerService() { return g_soundManager; }
void SetSoundManagerService(cIGZSndMgr* manager) { g_soundManager = manager; }
cIGZMessageServer2* MessageServer() { return g_messageServer; }
void SetMessageServer(cIGZMessageServer2* server) { g_messageServer = server; }
const std::string& FileSystemDataDirectory() { return g_dataDirectory; }
void SetFileSystemDataDirectory(const std::string& directory) { g_dataDirectory = directory; }
cIGZPersistRecordService* PersistService() { return g_persist; }
void SetPersistService(cIGZPersistRecordService* service) { g_persist = service; }
IUTalkCodec* UTalkCodec() { return g_utalk; }
void SetUTalkCodec(IUTalkCodec* codec) { g_utalk = codec; }

}  // namespace Audio
