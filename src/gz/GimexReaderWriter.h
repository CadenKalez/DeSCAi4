// cGZGimexReader / cGZGimexWriter — the COM wrappers around the Gimex image library (15 + 14 functions,
// 0x100510cce..0x100511974). Classes 0x2a316c02 / 0xca316c55, interfaces 0xca316beb / 0x2a316c43.
// A reader/writer holds an open cIGZFile (checked for read respectively write access), picks a Gimex module either
// by the file's extension (format 0x31/0x32), by asking every module to score the file (0x30/0x32; the best score
// wins) or directly (any other number), and forwards the frame calls to the library under a global lock.
// The Gimex library itself (GIMEX_find/get/set/is/open/info/read/close/wopen/write/wclose) is third-party code and is
// reached through IGimexLibrary.
// Layout: +8 file, +0x10 open flag, +0x14 (reader) / +0x20 (writer) module, +0x18 stream handle, refcount at +0x20 /
// +0x24.
#pragma once
#include <cstdint>
#include <mutex>

#include "gz/GimexFactory.h"

class IGimexLibrary {
public:
    virtual ~IGimexLibrary() = default;
    virtual int Find(const char* extension) = 0;                                  // GIMEX_find: module for an extension, 0 = none
    virtual int Get() = 0;                                                        // GIMEX_get: the current module
    virtual void Set(int module) = 0;                                             // GIMEX_set
    virtual int Is(cIGZFile* file) = 0;                                           // GIMEX_is: how well the current module knows the file
    virtual std::uint32_t Open(void** stream, cIGZFile* file) = 0;                // GIMEX_open(&stream, file, 0, 1): image count
    virtual bool Close(void* stream) = 0;                                         // GIMEX_close
    virtual cGZGimexInfo* Info(void* stream, std::uint32_t index) = 0;            // GIMEX_info
    virtual int Read(void* stream, cGZGimexInfo* info, void* pixels, std::uint32_t bytes) = 0;     // GIMEX_read
    virtual bool WriteOpen(void** stream, cIGZFile* file, std::uint32_t param) = 0;                // GIMEX_wopen
    virtual int Write(void* stream, const cGZGimexInfo* info, const void* pixels, std::uint32_t bytes) = 0;   // GIMEX_write
    virtual bool WriteClose(void* stream) = 0;                                    // GIMEX_wclose
    virtual void FreeInfo(cGZGimexInfo* info) = 0;                                // gfree
};
IGimexLibrary* GimexLibrary();
void SetGimexLibrary(IGimexLibrary* library);
std::recursive_mutex& GimexModuleLock();              // sGimexCurModuleCriticalSection

constexpr std::uint32_t kGZCLSID_cGZGimexReader = 0x2a316c02, kGZIID_cIGZGimexReader = 0xca316beb;
constexpr std::uint32_t kGZCLSID_cGZGimexWriter = 0xca316c55, kGZIID_cIGZGimexWriter = 0x2a316c43;

class cGZGimexReader final : public cIGZGimexReader {
public:
    cGZGimexReader() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool SetInput(cIGZFile* file) override;
    bool SetInput(cIGZPersistDBSerialRecord* record) override;
    bool BeginRead(std::uint32_t format, std::uint32_t& imageCount) override;
    bool BeginRead(const char* extension, std::uint32_t& imageCount) override;
    cGZGimexInfo* GetFrameInfo(std::uint32_t index) override;
    bool GetFramePixels(cGZGimexInfo* info, void* pixels, std::uint32_t bytes) override;
    void FreeGInfo(cGZGimexInfo* info) override;
    bool EndRead() override;
    void FinalRelease();

private:
    cIGZFile* file_ = nullptr;        // +8
    bool reading_ = false;            // +0x10
    int module_ = 0;                  // +0x14
    void* stream_ = nullptr;          // +0x18
    std::uint32_t refCount_ = 0;      // +0x20
};

class cGZGimexWriter final : public cIGZGimexWriter {
public:
    cGZGimexWriter() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool SetOutput(cIGZFile* file) override;
    bool SetOutput(cIGZPersistDBSerialRecord* record) override;
    bool BeginWrite(std::uint32_t format, std::uint32_t flags) override;
    bool BeginWrite(const char* extension, std::uint32_t flags) override;
    bool PutFramePixels(const cGZGimexInfo* info, const void* pixels, std::uint32_t bytes) override;
    bool EndWrite() override;
    void FinalRelease();

private:
    bool DoBeginWrite(std::uint32_t flags);

    cIGZFile* file_ = nullptr;        // +8
    bool writing_ = false;            // +0x10
    void* stream_ = nullptr;          // +0x18
    int module_ = 0;                  // +0x20
    std::uint32_t refCount_ = 0;      // +0x24
};
