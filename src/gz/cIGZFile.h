// cIGZFile — file interface of the GZ framework. Slot layout (5..33) recovered from the vtable of
// cRZFile (0x100a68298). Sizes/positions are 32-bit, as in the original (files are limited to 2 GiB).
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"

class cIGZFile : public cIGZUnknown {
public:
    // [3],[4] complete/deleting destructors are declared here (Open is slot 5).
    virtual ~cIGZFile() = default;

    // [5] access: 1 = read, 2 = write, 3 = read/write. creation: 0 = exclusive, 1 = create+truncate,
    // 2 = open existing, 3 = open or create, 4 = truncate existing. Returns true if already open.
    virtual bool Open(int access, int creation, int shareFlags) = 0;
    virtual bool IsOpen() const = 0;                                         // [6]
    virtual bool Close() = 0;                                                // [7]
    virtual int Position() = 0;                                              // [8] -1 if not open
    virtual std::uint32_t Length() = 0;                                      // [9] 0xffffffff on error
    virtual bool SetLength(std::uint32_t length) = 0;                        // [10]
    virtual void SeekToBegin() = 0;                                          // [11]
    virtual void SeekToEnd() = 0;                                            // [12]
    virtual void SeekToRelativePosition(int offset) = 0;                     // [13]
    virtual void SeekToPosition(std::uint32_t position) = 0;                 // [14]
    // [15] origin: 0 = begin, 1 = current, 2 = end. Returns the new position or 0xffffffff.
    virtual std::uint32_t Seek(int offset, int origin) = 0;
    virtual bool Read(void* destination, std::uint32_t byteCount) = 0;       // [16] all-or-nothing
    virtual bool ReadWithCount(void* destination, std::uint32_t& count) = 0; // [17] count = bytes read
    virtual bool Write(const void* source, std::uint32_t byteCount) = 0;     // [18]
    virtual bool WriteWithCount(const void* source, std::uint32_t& count) = 0;  // [19]
    virtual bool Flush() = 0;                                                // [20]
    virtual bool Remove() = 0;                                               // [21]
    virtual bool Rename(const cIGZString& newPath) = 0;                      // [22]
    virtual bool Exists() = 0;                                               // [23]
    virtual bool Copy(const cIGZString& sourcePath, bool overwrite) = 0;     // [24]
    virtual bool Copy(const cIGZFile& source, bool overwrite) = 0;           // [25]
    virtual void GetPath(cIGZString& out) const = 0;                         // [26]
    virtual const cIGZString* GetPath() const = 0;                           // [27]
    virtual const char* GetPathCStr() const = 0;                             // [28]
    virtual void SetPath(const cIGZString& path) = 0;                        // [29]
    virtual void SetPath(const char* path) = 0;                              // [30]
    virtual bool GetOpenModes(int& access, int& creation, int& shareFlags) const = 0;  // [31]
    virtual void GetBuffering(std::uint32_t& readSize, std::uint32_t& writeSize) const = 0;  // [32]
    virtual bool SetBuffering(std::uint32_t readSize, std::uint32_t writeSize) = 0;  // [33]
};

constexpr std::uint32_t kGZIID_cIGZFile = 0xe0203645;
