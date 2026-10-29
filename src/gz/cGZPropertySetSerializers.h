// Property-set serialisation: binary ("cGZPropertySetSerializerBinaryT"), XML
// ("cGZPropertySetSerializerXmlT"), the generic dispatcher ("cGZPropertySetSerializerT") and the
// legacy name-hashed XML reader ("cPropertySetUint32XmlReader"). Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1 (Binary::Read @ 0x100475a08, Write @ 0x100475ce0, Xml::Read @ 0x100475ffe,
// Write @ 0x10047617a, Xml handlers @ 0x100476a58.., generic Read @ 0x10046fd52).
//
// The readers take the whole record as a byte span (the binary reaches the size through the
// record-access stream view, which is not recovered); see notes/audio-integration.md section 8.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "gz/cIGZOStream.h"
#include "gz/cIGZPropertySet.h"

namespace PropertySetIO {

constexpr std::uint32_t kBinaryMagic = 0x2bea4862;        // first dword of a binary property set
constexpr std::uint32_t kLegacyBinaryMagic = 0xcbe750e0;  // also routed to the binary reader
constexpr const char* kXmlRootName = "cGZPropertySetUint32";

// Binary: u32 magic, u16 version (< 2), u32 count, then (u32 typeId, u32 key, payload) entries.
// Payloads: Uint32 u32, Float32 f32, Boolean u8, String length-prefixed. Entries of other types
// (e.g. Sint32) are skipped without consuming a payload, exactly like the original.
bool ReadBinary(const std::uint8_t* data, std::size_t size, cIGZPropertySet& set);
// Fails when the set is empty or holds a Sint32 value (not representable in the binary format).
bool WriteBinary(cIGZOStream& out, cIGZPropertySet& set);

// XML: <cGZPropertySetUint32> with <AnyType key="0x.." type="0x..">value</AnyType> children.
bool ReadXml(const std::uint8_t* data, std::size_t size, cIGZPropertySet& set);
bool WriteXml(cIGZOStream& out, cIGZPropertySet& set);

// Legacy XML: <PropertyUint32Uint32 Name="..." Value="..."/> etc; the key is CRC24(Name)|0xff000000.
bool ReadLegacyXml(const std::uint8_t* data, std::size_t size, cIGZPropertySet& set);

// cGZPropertySetSerializerT::Read: binary when the first dword is a binary magic, XML otherwise.
bool ReadAuto(const std::uint8_t* data, std::size_t size, cIGZPropertySet& set);
inline bool WriteAuto(cIGZOStream& out, cIGZPropertySet& set, bool binary) {
    return binary ? WriteBinary(out, set) : WriteXml(out, set);
}

// SAX callbacks that carry no state in this function-based port. The legacy reader's six ignored handlers
// (StartCdataSection 0x100472e66, EndCdataSection 0x100472e6e, StartNamespaceDecl 0x100472e76, EndNamespaceDecl 0x100472e7e,
// InternalEntityReference 0x100472e86, NotationDecl 0x100472e8e) all return true; the serializer's six
// (0x10047724a, 0x100477252, 0x10047725a, 0x100477262, 0x10047726a, 0x100477272) all return false.
inline bool LegacyReaderIgnoredCallback() { return true; }
inline bool SerializerIgnoredCallback() { return false; }
// cGZPropertySetSerializerXmlT::GetPropertySetTypeString @ 0x100477438: the root element name.
inline const char* GetPropertySetTypeString() { return kXmlRootName; }

}  // namespace PropertySetIO
