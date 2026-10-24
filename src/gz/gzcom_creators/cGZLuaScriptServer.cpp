// GZCOM creator for cGZLuaScriptServer, wired once the class was recovered.
#include "gz/GZCOM.h"
#include "gz/LuaScriptServer.h"

namespace gzcom {

void* GZCOM_CreatecGZLuaScriptServer() { return static_cast<void*>(new cGZLuaScriptServer()); }

}  // namespace gzcom
