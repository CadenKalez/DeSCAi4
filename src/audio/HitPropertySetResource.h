// cHitPropertySetResource and cHitPropertySetTrackResource — persisted property sets of the Hit
// engine (sound/track definitions). Both are a cGZPropertySetT<uint32> that is also a persist
// resource; they differ only in how Read/Write choose a format. Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1 (vtables 0x100ab4748 / 0x100ab4a08, 0x48-byte objects).
//
// Formats by resource type:
//   property set  0xebfee345 -> XML, 0xebfee349 -> binary (both read through the auto-detecting
//                 reader, so either encoding is accepted for either type id).
//   track set     0x0b8d821a -> legacy comma-separated text (read), "TKDT,2,..." text (write);
//                 0x0b9eb87e -> XML write; other types read through the auto-detecting reader.
// Reading a track resource first records its own instance in register 0xff6f9546 (the
// recursion guard cHitTrackPlayer::UpdatePatch compares with the sound id).
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetSerializers.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIStreamRemaining.h"

template <bool kTrack>
class cHitPropertySetResourceT : public cGZPropertySet, public cIHitPersistResource {
public:
    cHitPropertySetResourceT() = default;
    explicit cHitPropertySetResourceT(const cGZPersistResourceKey& key) : key_(key) {}   // @ 0x1004c2d94 (track)

    // 1 and 0xebb10cdd -> the property set; 0x3b93c5e7 and 0x456b8f1d (cIHitResource) -> the persist
    // resource subobject (+0x30).
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100472ab8 / 0x100472f0a
    std::uint32_t AddRef() override { return cGZPropertySet::AddRef(); }
    std::uint32_t Release() override { return cGZPropertySet::Release(); }

    // --- cIHitPersistResource ---
    cGZPersistResourceKey GetKey() const override { return key_; }           // @ 0x100472c02
    bool SetKey(const cGZPersistResourceKey& key) override { key_ = key; return true; }  // @ 0x100472c1c
    std::uint32_t GetType() const override { return key_.type; }
    std::uint32_t GetGroup() const override { return key_.group; }
    std::uint32_t GetInstance() const override { return key_.instance; }
    bool SetType(std::uint32_t v) override { key_.type = v; return true; }
    bool SetGroup(std::uint32_t v) override { key_.group = v; return true; }
    bool SetInstance(std::uint32_t v) override { key_.instance = v; return true; }
    std::uint32_t GetRefCount() const override { return static_cast<std::uint32_t>(refCountValue()); }  // @ 0x100472c78
    bool Read(cIGZIStream& stream, std::uint32_t flags, cIGZUnknown* userData) override;
    bool Write(cIGZOStream& stream, std::uint32_t flags, cIGZUnknown* userData) override;
    bool CreatePrivateCopy(std::uint32_t iid, void** out) override;         // @ 0x100472c9a / 0x100473042

    // Format-specific entry points (as separate virtuals in the binary).
    bool ReadBytes(const std::uint8_t* data, std::size_t size);   // type-driven (what Read does after loading)
    bool ReadLegacyFormat(const std::uint8_t* data, std::size_t size);   // track: @ 0x10046ff0c
    bool WriteOldFormat(cIGZOStream& out);                               // track: @ 0x1004711c8
    bool WriteXmlFormat(cIGZOStream& out) { return PropertySetIO::WriteXml(out, *this); }

private:
    std::int32_t refCountValue() const;
    cGZPersistResourceKey key_{};
};

using cHitPropertySetResource = cHitPropertySetResourceT<false>;
using cHitPropertySetTrackResource = cHitPropertySetResourceT<true>;
