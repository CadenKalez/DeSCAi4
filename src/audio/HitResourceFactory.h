// cHitResourceFactory — the persistence factory for every Hit resource type (12 functions, 0x10046f6b0..
// 0x1004731c4, vtable 0x100ab46e0, interface id 0xa56b8f17). Registered with the resource manager by the hit
// manager; builds the right payload object for a resource type:
//   0xa3cd96cf        cHitResource<cHitTrackPlayer>   (a playable track)
//   0x7b1acfcd        cHitResource<cHitList>
//   0xebfee345/…349   cHitPropertySetResource        (property set, XML / binary)
//   0x0b8d821a/…b9eb87e  cHitPropertySetTrackResource, created with register 0xff4c97be = 0xab903a1b (the parent
//                     track sound) and 0xff1cdf6a = "Track" (the type name)
// Reading an existing record creates an empty instance of the record's type, gives it the record key and reads
// it from the record's input stream (iid 0x051b11bd); Write streams the resource into the record's output stream
// (iid 0xc51b11cb). The factory's own Read hook is a stub that reports failure.
#pragma once
#include <cstdint>

#include "gz/cIGZPersistResourceManager.h"

namespace sc4audio {

class cHitResourceFactory : public cIGZPersistResourceFactory {
public:
    cHitResourceFactory() = default;
    ~cHitResourceFactory() override = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 1 and 0xa56b8f17
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;                              // deletes at zero

    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out, std::uint32_t flags, cIGZUnknown* owner) override;
    bool CreateInstance(cIGZPersistDBRecord& record, std::uint32_t iid, void** out, std::uint32_t flags, cIGZUnknown* owner) override;
    bool Read(cIGZPersistResource&, cIGZPersistDBRecord&) override { return false; }
    bool Write(const cIGZPersistResource& resource, cIGZPersistDBRecord& record) override;
    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) override { return CreateInstance(type, iid, out, 0, nullptr); }
    bool CreateInstance(cIGZPersistDBRecord& record, std::uint32_t iid, void** out) override { return CreateInstance(record, iid, out, 0, nullptr); }
    bool SerialRead(cIGZPersistResource&, cIGZPersistDBSerialRecord&) override { return false; }
    bool SerialWrite(cIGZPersistResource&, cIGZPersistDBSerialRecord&) override { return false; }

private:
    std::int32_t refCount_ = 0;
};

}  // namespace sc4audio
