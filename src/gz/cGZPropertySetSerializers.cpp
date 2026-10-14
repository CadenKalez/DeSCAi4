#include "gz/cGZPropertySetSerializers.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "gz/XmlSax.h"
#include "gz/cGZAny.h"
#include "rz/PortMemoryStreams.h"
#include "rz/RZConvert.h"
#include "rz/nRZHash.h"

namespace {

using namespace PropertySetIO;

// Type id tags (cGZAny<T>::TypeID).
constexpr std::uint32_t kTypeFloat = kAnyTypeFloat32, kTypeBool = kAnyTypeBoolean, kTypeUint = kAnyTypeUint32,
                        kTypeString = kAnyTypeString, kTypeSint = kAnyTypeSint32;

}  // namespace

// ---- binary ------------------------------------------------------------------------------------------------

bool PropertySetIO::ReadBinary(const std::uint8_t* data, std::size_t size, cIGZPropertySet& set) {
    PortMemoryIStream in(data, size);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint32_t count = 0;
    if (!in.GetUint32(magic) || magic != kBinaryMagic) return false;
    if (!in.GetUint16(version) || version >= 2) return false;
    if (!in.GetUint32(count)) return false;
    while (count != 0) {
        --count;
        std::uint32_t type = 0, key = 0;
        if (!in.GetUint32(type) || !in.GetUint32(key)) return false;
        // The original branches on the *signed* type id; ids that are neither of the four handled
        // ones fall through without reading a payload.
        if (type == kTypeFloat) {
            float v = 0;
            if (!in.GetFloat32(v)) return false;
            set.SetValue(key, AnyFloat32(v));
        } else if (type == kTypeBool) {
            std::uint8_t v = 0;
            if (!in.GetUint8(v)) return false;
            set.SetValue(key, AnyBoolean(v != 0));
        } else if (type == kTypeString) {
            cRZString s;
            if (!in.GetGZStr(s)) return false;
            set.SetValue(key, AnyString(s));
        } else if (type == kTypeUint) {
            std::uint32_t v = 0;
            if (!in.GetUint32(v)) return false;
            set.SetValue(key, AnyUint32(v));
        }
    }
    return true;
}

bool PropertySetIO::WriteBinary(cIGZOStream& out, cIGZPropertySet& set) {
    std::uint32_t key = 0;
    cIGZAny* value = nullptr;
    if (!set.FirstKeyAndValue(&key, &value)) return false;
    if (!out.SetUint32(kBinaryMagic) || !out.SetUint16(1) || !out.SetUint32(set.NumValues())) return false;
    do {
        const std::uint32_t type = value->TypeID();
        if (!out.SetUint32(type) || !out.SetUint32(key)) return false;
        bool ok;
        switch (type) {
            case kTypeFloat: ok = out.SetFloat32(static_cast<AnyFloat32*>(value)->get()); break;
            case kTypeBool: ok = out.SetUint8(static_cast<AnyBoolean*>(value)->value() ? 1 : 0); break;
            case kTypeString: {
                const cRZString* s = static_cast<AnyString*>(value)->string();
                ok = out.SetGZStr(s ? *s : cRZString());
                break;
            }
            case kTypeUint: ok = out.SetUint32(static_cast<AnyUint32*>(value)->get()); break;
            default: return false;  // Sint32 and unknown types cannot be written
        }
        if (!ok) return false;
    } while (set.NextKeyAndValue(&key, &value));
    return true;
}

// ---- XML -----------------------------------------------------------------------------------------------------

namespace {

std::string Format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string Format(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    return buffer;
}

// Handler of cGZPropertySetSerializerXmlT: HandleStartElement @ 0x100476a58, HandleEndElement
// @ 0x100476f3e, HandleCharData @ 0x100477210. Quirk kept: the nesting counter starts at 0 and is
// only decremented, so *every* end tag — the root's included — is processed as an entry.
class XmlSetReader final : public xml::Handler {
public:
    explicit XmlSetReader(cIGZPropertySet& set) : set_(set) {}

    bool StartElement(const std::string& name, const std::vector<std::pair<std::string, std::string>>& attrs) override {
        if (started_) {
            for (const auto& [attr, value] : attrs) {
                if (attr == "key") key_ = value;
                else if (attr == "type") type_ = value;
            }
            chars_.clear();
            return true;
        }
        if (name != kXmlRootName) return false;
        started_ = true;
        return true;
    }

    bool EndElement(const std::string&) override {
        if (--depth_ == 0) return true;
        const std::uint32_t key = RZ::ConvertHexToUint(key_.c_str());
        switch (RZ::ConvertHexToUint(type_.c_str())) {
            case kTypeFloat: set_.SetValue(key, AnyFloat32(static_cast<float>(std::strtod(chars_.c_str(), nullptr)))); break;
            case kTypeBool: set_.SetValue(key, AnyBoolean(chars_ == "true")); break;
            case kTypeSint: set_.SetValue(key, AnySint32(static_cast<std::int32_t>(RZ::ConvertToUint(chars_.c_str())))); break;
            case kTypeString: set_.SetValue(key, AnyString(chars_.c_str())); break;
            case kTypeUint: set_.SetValue(key, AnyUint32(RZ::ConvertToUint(chars_.c_str()))); break;
            default: return false;
        }
        return true;
    }

    bool CharData(const std::string& text) override {
        for (char c : text) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                chars_ = text;  // the whole chunk replaces the previous text
                break;
            }
        }
        return true;
    }

private:
    cIGZPropertySet& set_;
    bool started_ = false;
    int depth_ = 0;
    std::string key_, type_, chars_;
};

// cPropertySetUint32XmlReader (handlers @ 0x100472178 / 0x100472562 / 0x1004729c0).
class LegacyXmlReader final : public xml::Handler {
public:
    explicit LegacyXmlReader(cIGZPropertySet& set) : set_(set) {}

    bool StartElement(const std::string&, const std::vector<std::pair<std::string, std::string>>& attrs) override {
        chars_.clear();
        for (const auto& [name, value] : attrs) attrs_[name] = value;
        return true;
    }
    bool CharData(const std::string& text) override { chars_ += text; return true; }

    bool EndElement(const std::string& element) override {
        std::string name;
        std::string value = chars_;
        for (const auto& [attr, v] : attrs_) {
            if (attr == "Name") name = v;
            else if (attr == "Value") value = v;
        }
        bool ok = true;
        if (element == "PropertyStringString") {
            // accepted, nothing stored
        } else if (element == "PropertyUint32String") {
            if (!name.empty() || !value.empty()) set_.SetValue(KeyOf(name), AnyString(value.c_str()));
        } else if (element == "PropertyUint32Uint32") {
            if (value.empty() || !std::isdigit(static_cast<unsigned char>(value[0]))) ok = false;
            else if (!name.empty() || !value.empty()) set_.SetValue(KeyOf(name), AnyUint32(RZ::ConvertToUint(value.c_str())));
        } else if (element == "PropertyUint32Float32") {
            if (value.empty() || !std::isdigit(static_cast<unsigned char>(value[0]))) ok = false;
            else if (!name.empty() || !value.empty()) {
                set_.SetValue(KeyOf(name), AnyFloat32(static_cast<float>(std::strtod(value.c_str(), nullptr))));
            }
        }
        attrs_.clear();
        return ok;
    }

private:
    static std::uint32_t KeyOf(const std::string& name) { return nRZHash::CRC24(name.c_str(), false) | 0xff000000u; }
    cIGZPropertySet& set_;
    std::map<std::string, std::string> attrs_;
    std::string chars_;
};

}  // namespace

bool PropertySetIO::ReadXml(const std::uint8_t* data, std::size_t size, cIGZPropertySet& set) {
    XmlSetReader reader(set);
    return xml::Parse(reinterpret_cast<const char*>(data), size, reader);
}

bool PropertySetIO::ReadLegacyXml(const std::uint8_t* data, std::size_t size, cIGZPropertySet& set) {
    LegacyXmlReader reader(set);
    return xml::Parse(reinterpret_cast<const char*>(data), size, reader);
}

bool PropertySetIO::WriteXml(cIGZOStream& out, cIGZPropertySet& set) {
    std::uint32_t key = 0;
    cIGZAny* value = nullptr;
    if (!set.FirstKeyAndValue(&key, &value)) return false;
    auto put = [&out](const std::string& s) { return out.SetVoid(s.data(), static_cast<std::uint32_t>(s.size())); };
    if (!put(Format("<?xml version=\"%d.%d\"?>\n", 1, 0))) return false;
    if (!put(Format("<%s>\n", kXmlRootName))) return false;
    do {
        const std::uint32_t type = value->TypeID();
        const char* tag = nullptr;
        std::string text;
        switch (type) {
            case kTypeFloat: tag = "AnyFloat32"; text = Format("%f", static_cast<double>(static_cast<AnyFloat32*>(value)->get())); break;
            case kTypeBool: tag = "AnyBoolean"; text = static_cast<AnyBoolean*>(value)->value() ? "true" : "false"; break;
            case kTypeUint: tag = "AnyUint32"; text = Format("%d", static_cast<std::int32_t>(static_cast<AnyUint32*>(value)->get())); break;
            case kTypeString: {
                tag = "AnyString";
                const cRZString* s = static_cast<AnyString*>(value)->string();
                text = s ? s->str() : std::string();
                break;
            }
            case kTypeSint: tag = "AnySint32"; text = Format("%d", static_cast<std::int32_t>(static_cast<AnySint32*>(value)->get())); break;
            default: return true;  // unknown types end the loop successfully in the binary
        }
        const std::string line = "   " + Format("<%s key=\"0x%08x\" type=\"0x%08x\">", tag, key, type) + text +
                                 Format("</%s>\n", tag);
        if (!put(line)) return false;
    } while (set.NextKeyAndValue(&key, &value));
    return put(Format("</%s>\n", kXmlRootName));
}

bool PropertySetIO::ReadAuto(const std::uint8_t* data, std::size_t size, cIGZPropertySet& set) {
    PortMemoryIStream in(data, size);
    std::uint32_t magic = 0;
    if (!in.GetUint32(magic)) return false;
    if (magic == kBinaryMagic || magic == kLegacyBinaryMagic) return ReadBinary(data, size, set);
    return ReadXml(data, size, set);
}
