// cIGZSndDataSource — a source of sound data (wav segment, streamed file, ...). Only the
// reference-counting contract is known so far; the data accessors are recovered together with
// the cGZSnd*DataSource classes.
#pragma once
#include "gz/cIGZUnknown.h"

class cIGZSndDataSource : public cIGZUnknown {};
