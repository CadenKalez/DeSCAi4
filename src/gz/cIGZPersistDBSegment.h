// cIGZPersistDBSegment — a database segment of the persistence layer (a loaded DBPF file or
// directory). Only what the audio code touches is declared: its QueryInterface. Slot 0xa0
// (OpenRecord(key, &record, readOnly)) is used by cHitMan::ParseHitFileFromIGZPersistRecord...
// through the host seam instead; the remaining slots are not recovered yet.
#pragma once
#include "gz/cIGZUnknown.h"

class cIGZPersistDBSegment : public cIGZUnknown {};
