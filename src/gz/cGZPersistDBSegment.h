// cGZPersistDBSegment — template base of database segments. Every public operation takes the
// segment's recursive lock and forwards to a protected Do* hook, exactly like the original
// (vtable 0x100aa0ce0); concrete segments implement the hooks.
#pragma once
#include <cstdint>
#include <mutex>

#include "gz/cIGZPersistDBSegment.h"

class cGZPersistDBSegment : public cIGZPersistDBSegment {
public:
    cGZPersistDBSegment() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;  // 1 or 0x656b8f0c
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;  // destroys through FinalRelease() when the count is <= 1

    bool Lock() override { mutex_.lock(); return true; }
    bool Unlock() override { mutex_.unlock(); return true; }

    bool TestForRecord(const cGZPersistResourceKey& key) override;
    std::uint32_t GetRecordSize(const cGZPersistResourceKey& key) override;
    bool OpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record, std::uint32_t flags) override;
    bool CreateNewRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record) override;
    bool CloseRecord(cIGZPersistDBRecord* record) override;
    bool CloseRecord(cIGZPersistDBRecord** record) override;   // also drops the caller's reference
    bool AbortRecord(cIGZPersistDBRecord* record) override;
    bool AbortRecord(cIGZPersistDBRecord** record) override;
    bool DeleteRecord(const cGZPersistResourceKey& key) override;
    std::uint32_t ReadRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t& size) override;
    bool WriteRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t size) override;
    std::uint32_t GetResourceKeyList(cIGZPersistResourceKeyList*, cIGZPersistResourceKeyFilter*) override { return 0; }
    bool GetResourceKeyList(cIGZPersistResourceKeyList& list) override;

protected:
    virtual ~cGZPersistDBSegment() = default;
    virtual void FinalRelease() { delete this; }
    virtual bool DoTestForRecord(const cGZPersistResourceKey& key) = 0;
    virtual std::uint32_t DoGetRecordSize(const cGZPersistResourceKey& key) = 0;
    virtual bool DoOpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record, std::uint32_t flags) = 0;
    virtual bool DoCreateNewRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record) = 0;
    virtual bool DoCloseRecord(cIGZPersistDBRecord* record) = 0;
    virtual bool DoAbortRecord(cIGZPersistDBRecord* record) = 0;
    virtual bool DoDeleteRecord(const cGZPersistResourceKey& key) = 0;
    virtual std::uint32_t DoReadRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t& size) = 0;
    virtual bool DoWriteRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t size) = 0;

    std::recursive_mutex mutex_;

private:
    std::uint32_t refCount_ = 0;
};
