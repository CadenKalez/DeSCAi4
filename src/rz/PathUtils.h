// Path and file name helpers of the RZ layer (0x10003544f..0x100035ac1, Fullpath 0x100034ed8).
#pragma once
#include <string>

#include "rz/cRZString.h"

// A full path starts with '/' and has no "/." segment (neither "/./" nor "/../").
bool IsFullPath(const cRZString& path);
// Joins `path` onto `base` and resolves "." and ".." segments in place. False when the result does not fit
// the 0x200 byte working buffer or ".." climbs above the root.
bool Fullpath(std::string& out, const char* path, const char* base);
bool Fullpath(cRZString& out, const cRZString& path, const cRZString* base);
bool ResolvePath(cRZString& out, const cRZString& path, const cRZString* base);
// Valid file names have no '"', '*', '/', '?' or '|'; on FAT / HPFS / NTFS (`fileSystem`) the part before the
// first '.' must not be a reserved device name; names of more than 256 bytes are accepted unchecked (sic)
// and a name of only spaces and dots is rejected.
bool IsFileNameValid(const cRZString& name, const cRZString* fileSystem);
bool MakeFileNameValid(const cRZString& name, cRZString& out, const cRZString* fileSystem);
bool MatchPattern(const char* text, const char* pattern);          // '*' and '?' wildcards
bool WildcardMatch(const cRZString& text, const cRZString& pattern, bool caseSensitive);
