// The application's resource set-up (0x1000b0a3c SetupResources, 0x1000b1f30 SetupPackedFileSegment): every database
// file of the game becomes a segment of the persist resource manager.
#pragma once
#include "rz/cRZString.h"

// @ 0x1000b1f30: opens the database at `path` as a segment and registers it with the resource manager. `multiFile`
// makes it a segment over a directory of databases (class 0x090f8619) instead of one packed file (class 0x086d4a89);
// `onlyDatabases` skips files that are not DBPF databases.
void SetupPackedFileSegment(const cRZString& path, bool multiFile, bool onlyDatabases);
