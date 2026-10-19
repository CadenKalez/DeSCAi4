// cRZTextFile — a cRZFile that reads and writes text a line at a time (7 functions, 0x100051d46..0x100052388).
//  * Open detects a UTF-16 byte order mark (0xFFFE or 0xFEFF, both read as little-endian units) and switches to wide
//    mode; otherwise the file is read as narrow text from its start.
//  * ReadLine reads 0x80-byte chunks, ends the line at the first CR or LF and swallows the other half of a CRLF / LFCR
//    pair. The last line needs no terminator. Answers false when nothing was left to read.
//  * WriteLine writes the bytes followed by "\n" (a two-byte "\n" in wide mode).
// Deviation: the original's wide path appends `bytes` units for a line of `bytes/2` characters (it copies past the
// chunk); this version appends exactly the characters read.
#pragma once
#include <cstdint>
#include <string>

#include "rz/cRZFile.h"

class cRZTextFile : public cRZFile {
public:
    cRZTextFile() = default;
    explicit cRZTextFile(const char* path) : cRZFile(path) {}
    ~cRZTextFile() = default;

    bool Open(int access, int creation, int shareFlags) override;
    bool ReadLine(cIGZString& out);                         // @ 0x100051ed2
    bool WriteLine(const void* data, std::uint32_t bytes);  // @ 0x100052330
    bool IsWide() const { return textMode_ == 2; }

private:
    int textMode_ = 0;           // +0xd0: 2 = UTF-16
    bool hasByteOrderMark_ = false;   // +0xd4
};
