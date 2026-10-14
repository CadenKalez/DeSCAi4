// cRZFile — buffered POSIX file object (cIGZFile) with static path/attribute helpers.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100a68298, 60 functions).
//
// Layout of the original (0xd0 bytes): +0x08 cRZString path, +0x30 open flag, +0x34 fd,
// +0x38/+0x3c/+0x40 open modes, +0x44 refcount, +0x48 last error, +0x78 logical position,
// +0x7c descriptor position, +0x80/+0xa8 read/write buffer sizes (default 0x200), +0xa0/+0xa4
// read-buffer window (file offset / byte count), +0xc8/+0xcc write-buffer file offset / fill.
//
// Quirks kept on purpose (callers were tuned against them):
//  * Copy(source, dest, overwrite=false) fails whenever the *source* exists (the existence check
//    is applied to the first argument).
//  * SetAttribute returns true when chmod FAILS.
//  * GetPathDirectory always yields an empty string and false (it returns the drive buffer).
//  * Seek with an origin other than 0/1/2 returns the current position.
//  * Paths: backslashes become '/', the encoding conversion is the identity on UTF-8 (the original
//    converts from the current locale code page, which is UTF-8 on macOS).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gz/cIGZFile.h"
#include "rz/cRZString.h"

// Splits `path` like _splitpath: drive is always "", dir = up to and including the last '/', name,
// ext = from the last '.' that lies after the last '/'. Each output buffer holds 0x100 bytes
// (copies are clamped to 255 characters); any output may be null.
void Splitpath(const char* path, char* drive, char* dir, char* name, char* ext);
void Splitpath(const cRZString& path, cRZString* drive, cRZString* dir, cRZString* name, cRZString* ext);
// Copies `source` into `dest` replacing '\\' with '/'; `dest` must already be at least as long.
void SC4AdjustPathSeparatorsForPlatform(const cRZString& source, cRZString& dest);

class cRZFile : public cIGZFile {
public:
    enum TimeType { kTimeCreated = 1, kTimeModified = 2, kTimeAccessed = 3 };

    // Open flags used by the attribute helpers.
    enum Attribute : int {
        kAttrReadable = 0x1, kAttrWritable = 0x2, kAttrExecutable = 0x4, kAttrDirectory = 0x8,
        kAttrFile = 0x10, kAttrReadOnly = 0x1000,
    };

    cRZFile();
    explicit cRZFile(const cIGZString& path);
    explicit cRZFile(const char* path);
    ~cRZFile();

    // cIGZUnknown
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    // cIGZFile
    bool Open(int access, int creation, int shareFlags) override;
    bool IsOpen() const override { return open_; }
    bool Close() override;
    int Position() override;
    std::uint32_t Length() override;
    bool SetLength(std::uint32_t length) override;
    void SeekToBegin() override { Seek(0, 0); }
    void SeekToEnd() override { Seek(0, 2); }
    void SeekToRelativePosition(int offset) override { Seek(offset, 1); }
    void SeekToPosition(std::uint32_t position) override { Seek(static_cast<int>(position), 0); }
    std::uint32_t Seek(int offset, int origin) override;
    bool Read(void* destination, std::uint32_t byteCount) override;
    bool ReadWithCount(void* destination, std::uint32_t& count) override;
    bool Write(const void* source, std::uint32_t byteCount) override;
    bool WriteWithCount(const void* source, std::uint32_t& count) override;
    bool Flush() override;
    bool Remove() override;
    bool Rename(const cIGZString& newPath) override;
    bool Exists() override;
    bool Copy(const cIGZString& sourcePath, bool overwrite) override;
    bool Copy(const cIGZFile& source, bool overwrite) override;
    void GetPath(cIGZString& out) const override;
    const cIGZString* GetPath() const override { return &path_; }
    const char* GetPathCStr() const override { return path_.ToChar(); }
    void SetPath(const cIGZString& path) override;
    void SetPath(const char* path) override;
    bool GetOpenModes(int& access, int& creation, int& shareFlags) const override;
    void GetBuffering(std::uint32_t& readSize, std::uint32_t& writeSize) const override;
    bool SetBuffering(std::uint32_t readSize, std::uint32_t writeSize) override;

    int LastError() const { return lastError_; }

    // --- static helpers (all take paths as cIGZString) ---
    static bool FileCreate(const cIGZString& path, bool truncate);
    static bool FileExists(const cIGZString& path);
    static bool Copy(const cIGZString& source, const cIGZString& dest, bool overwrite);
    static bool GetAttribute(const cIGZString& path, int attributes);
    static bool SetAttribute(const cIGZString& path, int attributes, bool set);
    static std::uint32_t GetTime(const cIGZString& path, TimeType type);
    static bool IsFileWritable(const cIGZString& path);
    static bool Remove(const cIGZString& path);
    static bool Rename(const cIGZString& from, const cIGZString& to);
    static std::uint32_t GetLength(const cIGZString& path);
    static bool GetTempDirectory(cIGZString& out);
    // `inOutPrefix` is the file-name prefix on entry (default "RZ_") and the full path on success.
    static bool MakeTempPathName(cIGZString& inOutPrefix, const cIGZString* extension);
    static bool MakeTempFileName(const cIGZString& directory, cIGZString& inOutPrefix,
                                 const cIGZString* extension);
    static bool GetPathExtension(const cIGZString& path, cIGZString& out);
    static bool GetPathDirectory(const cIGZString& path, cIGZString& out);
    static bool GetPathName(const cIGZString& path, cIGZString& out, bool includeExtension);
    static bool GetVolumeName(const cIGZString& path, cIGZString& out);

private:
    bool FlushWriteBuffer();
    bool FillReadBuffer();
    bool FillWriteBuffer(const std::uint8_t* data, int count);
    void NoteErrno();

    cRZString path_;
    bool open_ = false;
    int fd_ = -1;
    int access_ = 0, creation_ = 0, shareFlags_ = 0;
    std::uint32_t refCount_ = 0;
    int lastError_ = 0x20000000;
    int position_ = 0;       // +0x78 logical position
    int fdPosition_ = 0;     // +0x7c position of the descriptor
    std::uint32_t readBufferSize_ = 0x200;
    std::vector<std::uint8_t> readBuffer_;
    int readWindowOffset_ = 0, readWindowCount_ = 0;
    std::uint32_t writeBufferSize_ = 0x200;
    std::vector<std::uint8_t> writeBuffer_;
    int writeFileOffset_ = 0;
    int writeFill_ = 0;
};
