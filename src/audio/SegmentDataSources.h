// Sound data sources that read a segment of a packed database (type 0x2026960b records):
// cGZSndSegmentWavDataSource (RIFF wav), cGZSndSegmentDataSourceXA (EA XA ADPCM),
// cGZSndSegmentDataSourceUTalk (EA UTalk speech) and the creator that picks the first one that
// accepts a record. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (creator @ 0x10046d8a0, wav @ 0x10046de00, UTalk @ 0x10046e410, XA @ 0x10046ebd0).
//
// All three share the reference-count contract (Release at count 1 closes and destroys; a source
// released at count 0 underflows) and the Open/Lock/ReadPart state machine described per class.
#pragma once
#include <cstdint>
#include <list>
#include <memory>

#include "audio/AudioServices.h"
#include "gz/cIGZPersistDBSerialRecord.h"
#include "audio/interfaces/cIGZSndDataSource.h"
#include "audio/interfaces/cIGZSndDataSourceCreator.h"
#include "rz/cRZAutoRefCount.h"

constexpr std::uint32_t kGZResourceTypeSndSegment = 0x2026960b;

class cGZSndSegmentSourceBase : public cIGZSndDataSource {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;   // ids 1 and 0x3afd825f
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;                              // at 1: count = 0, Close(), destroy

    bool SetPath(const char*) override { return false; }
    bool GetPath(cIGZString&) override { return false; }
    bool SetKey(const cGZPersistResourceKey& key) override { key_ = key; return true; }
    bool GetKey(cGZPersistResourceKey& key) override { key = key_; return true; }
    bool IsLocked() const override { return lockCount_ != 0; }
    bool SetDataIsValid(bool valid) override { valid_ = valid; validSet_ = true; return true; }
    std::uint32_t GetPosition() const override { return position_; }
    bool SetPosition(std::uint32_t position) override { position_ = position; return true; }
    void ReadReset() override;
    const GZSndWaveFormat* GetWaveFormat() const override { return &format_; }

protected:
    explicit cGZSndSegmentSourceBase(const cGZPersistResourceKey& key) : key_(key) {}
    // Opens the record and its sequential access view; false (without closing) on failure.
    bool OpenRecord();
    void CloseRecord();                                            // closes and drops record + view

    std::int32_t refCount_ = 0;                                    // +8
    cGZPersistResourceKey key_;
    cRZAutoRefCount<cIGZPersistDBRecord> record_;
    cRZAutoRefCount<cIGZPersistDBSerialRecord> access_;
    GZSndWaveFormat format_;
    std::uint32_t position_ = 0;
    std::uint32_t dataSize_ = 0;
    std::int32_t lockCount_ = 0;
    std::unique_ptr<std::uint8_t[]> lockBuffer_;
    bool valid_ = false;
    bool validSet_ = false;
};

// RIFF wav: scans dword by dword for the 'fmt ' and 'data' chunks.
class cGZSndSegmentWavDataSource final : public cGZSndSegmentSourceBase {
public:
    cGZSndSegmentWavDataSource(std::uint32_t type, std::uint32_t group, std::uint32_t instance)
        : cGZSndSegmentSourceBase({type, group, instance}) {}
    ~cGZSndSegmentWavDataSource() override = default;

    bool Open() override;                                          // @ 0x10046e120
    bool Close() override;                                         // @ 0x10046e318
    bool IsOpen() const override { return open_; }
    std::uint32_t GetDataSize() const override { return dataSize_; }
    // Allocates a zeroed buffer of the data size (the original leaves it uninitialised and never
    // fills it); ReadPart then reads from it while locked.
    bool Lock(std::uint8_t** data, std::uint32_t* size) override;  // @ 0x10046dfe2
    bool Unlock() override;                                        // @ 0x10046e018
    std::int32_t ReadPart(std::uint8_t* destination, std::int32_t byteCount) override;   // @ 0x10046e046
    bool DataIsValid() override;                                   // @ 0x10046e0c6
    bool Shutdown();                                               // @ 0x10046dfa6 (vtable slot 23)

private:
    bool FindChunk(std::uint32_t fourCC, std::uint32_t& size);     // @ 0x10046e2a4
    bool open_ = false;                                            // +0x50
};

// EA XA ADPCM (mono or stereo, 15 / 30 compressed bytes per 56 / 112 PCM bytes).
class cGZSndSegmentDataSourceXA final : public cGZSndSegmentSourceBase {
public:
    cGZSndSegmentDataSourceXA(std::uint32_t type, std::uint32_t group, std::uint32_t instance)
        : cGZSndSegmentSourceBase({type, group, instance}) {}
    ~cGZSndSegmentDataSourceXA() override = default;

    bool Open() override;                                          // @ 0x10046ef2c
    bool Close() override;                                         // @ 0x10046f16c
    bool IsOpen() const override { return open_; }
    std::uint32_t GetDataSize() const override { return dataSize_; }
    bool Lock(std::uint8_t** data, std::uint32_t* size) override;  // @ 0x10046edd6
    bool Unlock() override;                                        // @ 0x10046ee98
    std::int32_t ReadPart(std::uint8_t* destination, std::int32_t byteCount) override;   // @ 0x10046f1d4
    bool DataIsValid() override;                                   // @ 0x10046eed2
    std::uint32_t lastError() const { return lastError_; }         // +0x90: 3 bad header, 4 read failure

private:
    bool DecompressFrameMono();                                    // @ 0x10046f2de
    bool DecompressFrameStereo();                                  // @ 0x10046f430
    bool open_ = false;                                            // +0x5c
    std::uint32_t lastError_ = 0;                                  // +0x90
    std::uint32_t remainingDecoded_ = 0;                           // +0x94
    std::uint32_t recordSize_ = 0;                                 // +0x78
    std::uint32_t consumed_ = 0;                                   // +0x7c
    float history_[4] = {0, 0, 0, 0};                              // +0xc, +0x10, +0x14, +0x18
    std::int32_t compressedIndex_ = 0;                             // +0x1c
    std::int32_t outputIndex_ = 0;                                 // +0x20
    std::int32_t outputEnd_ = 0;                                   // +0x24
    std::uint8_t decoded_[0x70] = {};                              // +0x98
    std::uint8_t compressed_[0xf00] = {};                          // +0x108
};

// EA UTalk (decoded by the SND library's _utalkinit / _utalkdecode).
class cGZSndSegmentDataSourceUTalk final : public cGZSndSegmentSourceBase {
public:
    cGZSndSegmentDataSourceUTalk(std::uint32_t type, std::uint32_t group, std::uint32_t instance)
        : cGZSndSegmentSourceBase({type, group, instance}) {}
    ~cGZSndSegmentDataSourceUTalk() override = default;

    bool Open() override;                                          // @ 0x10046e996
    bool Close() override;                                         // @ 0x10046ea04
    bool IsOpen() const override { return compressed_ != nullptr; }
    std::uint32_t GetDataSize() const override { return dataSize_; }
    bool Lock(std::uint8_t** data, std::uint32_t* size) override;  // @ 0x10046e5d6
    bool Unlock() override;                                        // @ 0x10046e6a4
    std::int32_t ReadPart(std::uint8_t* destination, std::int32_t byteCount) override;   // @ 0x10046ea48
    void ReadReset() override { Open(); }                          // @ 0x10046ebae (does not rewind first)
    bool DataIsValid() override;                                   // @ 0x10046e6de

private:
    bool OpenRecords();                                            // @ 0x10046e71a
    void CloseRecords();                                           // @ 0x10046e92a
    std::unique_ptr<std::uint8_t[]> compressed_;                   // +0x50
    std::uint32_t compressedSize_ = 0;                             // +0x58
    std::uint32_t producedPadding_ = 0;                            // +0x60
    bool finished_ = false;                                        // +0x70
    Audio::UTalkState state_{};                                    // +0x78
};

class cGZSndSegmentDataSourceCreator final : public cIGZSndDataSourceCreator {
public:
    cGZSndSegmentDataSourceCreator();                              // @ 0x10046d8a0
    static cGZSndSegmentDataSourceCreator* Create() { return new cGZSndSegmentDataSourceCreator(); }   // @ 0x10043ca8a
    ~cGZSndSegmentDataSourceCreator() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;   // ids 1, 0x9b75709e @ 0x10046da32
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;                              // destroys at count 1

    bool CreateSndDataSource(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) override;   // @ 0x10046dbc2
    bool AddSndDataSourceDef(std::uint32_t id, cIGZString& definition) override;   // @ 0x10046da54
    void RemoveSndDataSourceDef(std::uint32_t id) override;        // @ 0x10046db80

    const std::list<std::uint32_t>& groups() const { return groups_; }

private:
    std::int32_t refCount_ = 0;                                    // +8
    cRZAutoRefCount<cIGZPersistRecordService> persist_;            // +0x10
    std::list<std::uint32_t> groups_;                              // +0x18 database group ids to search
};
