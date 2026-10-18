// cGZDBSDFRecord — a database record backed by a plain file (the "SDF" segment stores one resource per file; vtable
// 0x100ab80f8, 27 functions at 0x1004d7dc6..0x1004d8a80). It owns two cRZFiles: the record file (+0x68) and a backup
// copy (+0x138) used while a record is being rewritten, so an aborted rewrite can restore the original.
//
// Modes (+0x208): 1 = read an existing file, 2 = rewrite (the original is copied to a temp file first and the record
// file is truncated), 3 = write to a freshly created (empty) file, opened exclusive without a create bit. Reading works only in mode 1, writing in modes 2 and 3.
// Init sets the record's access flags from the mode (1 -> read, 2/3 -> write) before anything can fail.
// AbortInternal restores the backup over the record file in mode 2; CloseInternal deletes the backup instead.
//
// The port keeps the record on cGZPersistDBSerialRecord; the SDF segment that calls Init / CloseInternal /
// AbortInternal is not recovered, so DoPostClose runs CloseInternal for the packed-file style close protocol.
#pragma once
#include <cstdint>

#include "gz/cGZPersistDBSerialRecord.h"
#include "rz/cRZFile.h"

constexpr std::uint32_t kGZIID_cIGZPersistDBSDFRecord = 0x856c8d04;

class cIGZPersistDBSDFRecord : public cIGZUnknown {
public:
    enum eSeekMethod : std::uint32_t { kSeekBegin = 0, kSeekCurrent = 1, kSeekEnd = 2 };
    enum dbsdfFlags : std::uint32_t { kModeNone = 0, kModeRead = 1, kModeRewrite = 2, kModeCreate = 3 };

    virtual std::uint32_t GetSize() = 0;                                              // +0x18
    virtual bool GetFieldVoid(void* destination, std::uint32_t byteCount) = 0;        // +0x20
    virtual bool SetFieldVoid(const void* source, std::uint32_t byteCount) = 0;       // +0x28
    virtual std::uint32_t Read(void* destination, std::uint32_t byteCount) = 0;       // +0x30 (bytes read)
    virtual std::uint32_t Write(const void* source, std::uint32_t byteCount) = 0;     // +0x38 (bytes written)
    virtual bool Tell(std::uint32_t& position) = 0;                                   // +0x40
    virtual bool Seek(std::uint32_t offset, eSeekMethod method) = 0;                  // +0x48
    virtual void Flush() = 0;                                                         // +0x50
    virtual bool GetPath(cIGZString** out) = 0;                                       // +0x58
    virtual bool GetName(cIGZString** out) = 0;                                       // +0x60
    virtual bool GetExtension(cIGZString** out) = 0;                                  // +0x68
    virtual bool Init(const cGZPersistResourceKey& key, const cIGZString& path, dbsdfFlags mode) = 0;   // +0x70
    virtual bool CloseInternal() = 0;                                                 // +0x78
    virtual bool AbortInternal() = 0;                                                 // +0x80
};

class cGZDBSDFRecord final : public cGZPersistDBSerialRecord, public cIGZPersistDBSDFRecord {
public:
    explicit cGZDBSDFRecord(cIGZPersistDBSegment* segment) : cGZPersistDBSerialRecord(segment, true) {}   // @ 0x1004d7dc6
    ~cGZDBSDFRecord() override;                                                            // @ 0x1004d7ee2

    bool QueryInterface(std::uint32_t iid, void** out) override;                          // @ 0x1004d805e
    std::uint32_t AddRef() override { return cGZPersistDBSerialRecord::AddRef(); }        // @ 0x1004d8118
    std::uint32_t Release() override { return cGZPersistDBSerialRecord::Release(); }      // @ 0x1004d815a

    bool Init(const cGZPersistResourceKey& key, const cIGZString& path, dbsdfFlags mode) override;   // @ 0x1004d819c
    bool CloseInternal() override;                                                        // @ 0x1004d8328
    bool AbortInternal() override;                                                        // @ 0x1004d837e
    void DoPostClose() override { CloseInternal(); }

    std::uint32_t GetSize() override;                                                     // @ 0x1004d83d8
    bool SetSize(std::uint32_t) override { return false; }                                // @ 0x1004d8434
    std::uint32_t GetPosition() override;                                                 // @ 0x1004d844c
    bool GetFieldVoid(void* destination, std::uint32_t byteCount) override;              // @ 0x1004d84a8
    bool SetFieldVoid(const void* source, std::uint32_t byteCount) override;             // @ 0x1004d8516
    std::uint32_t Read(void* destination, std::uint32_t byteCount) override;             // @ 0x1004d8590
    std::uint32_t Write(const void* source, std::uint32_t byteCount) override;           // @ 0x1004d85c2
    bool SeekAbsolute(std::uint32_t position) override;                                  // @ 0x1004d8658
    bool SeekRelative(std::int32_t delta) override;                                      // @ 0x1004d86d8
    bool Tell(std::uint32_t& position) override;                                         // @ 0x1004d8758
    bool Seek(std::uint32_t offset, eSeekMethod method) override;                        // @ 0x1004d8784
    void Flush() override;                                                               // @ 0x1004d85f4
    bool GetPath(cIGZString** out) override;                                             // @ 0x1004d87dc
    bool GetName(cIGZString** out) override;                                             // @ 0x1004d884c
    bool GetExtension(cIGZString** out) override;                                        // @ 0x1004d8910
    cIGZPersistDBRecord* AsIGZPersistDBRecord() override { return static_cast<cIGZPersistDBRecord*>(this); }   // @ 0x1004d8a52

private:
    bool initialized_ = false;      // +0x61
    cRZFile file_;                  // +0x68 the record file
    cRZFile backup_;                // +0x138 copy of the original while rewriting
    std::uint32_t mode_ = 0;        // +0x208
};
