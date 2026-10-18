// The glue of the "G" sound-stream decoder plug-in interface (0x100624b7e..0x100624d15): a plug-in is a table of functions
// (about, is, open, info, read, close, wopen, write, wclose) over a GSTREAM, and the host hands plug-ins allocation and
// stream helpers. The only plug-in in the game binary is the do-nothing "NULL" one, which declines every stream.
#pragma once
#include <cstdint>

#include "gz/cIGZFile.h"

// A GSTREAM is a file object. Seeks return whether the position was reached.
void* _galloc(int size);                                         // operator new[]; sizes below -1 ask for everything. @ 0x100624b7e
bool _gfree(void* block);                                        // @ 0x100624b9b
int _gread(cIGZFile* stream, void* buffer, int count);           // bytes read, 0 for a negative count. @ 0x100624bad
int _gwrite(cIGZFile* stream, const void* buffer, int count);    // @ 0x100624bd5
bool _gseek(cIGZFile* stream, int position);                     // @ 0x100624bfd
std::uint32_t _glen(cIGZFile* stream);                           // @ 0x100624c18
std::uint32_t _gtell(cIGZFile* stream);                          // @ 0x100624c26

struct GSTREAM;
struct GINSTANCE;
struct GINFO;
// The 0x100-byte plug-in description ("Null" by Colin Andrews, version string "0", format name "Not Gimex"). Free it with
// _gfree. @ 0x100624c36
std::uint8_t* NULL_about();
int NULL_is(GSTREAM*);                                           // all of these return 0 (@ 0x100624cdd..0x100624d15)
int NULL_open(GINSTANCE**, GSTREAM*, const char*, bool);
int NULL_info(GINSTANCE*, int);
int NULL_read(GINSTANCE*, GINFO*, char*, int);
int NULL_close(GINSTANCE*);
int NULL_wopen(GINSTANCE**, GSTREAM*, const char*, int);
int NULL_write(GINSTANCE*, const GINFO*, char*, int);
int NULL_wclose(GINSTANCE*);
