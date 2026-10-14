// DBPF 1.0 package reader (SimCity 4 .dat/.sc4) with QFS (RefPack) decompression.
// Index format is the 5-dword (type, group, instance, offset, size) layout; compressed
// entries are listed by the DIR record (type 0xe86b1eef) with their uncompressed size.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dbpf {

struct TypeGroupInstance {
    uint32_t type = 0, group = 0, instance = 0;
    bool operator==(const TypeGroupInstance& o) const {
        return type == o.type && group == o.group && instance == o.instance;
    }
};

struct TGIHash {
    size_t operator()(const TypeGroupInstance& k) const noexcept {
        uint64_t h = (uint64_t(k.type) * 0x9E3779B185EBCA87ull) ^ (uint64_t(k.group) << 17) ^ k.instance;
        return size_t(h ^ (h >> 31));
    }
};

struct Entry {
    TypeGroupInstance id;
    uint32_t offset = 0;
    uint32_t size = 0;           // stored size
    uint32_t uncompressedSize = 0;  // 0 = not compressed (per DIR)
};

// Decompress a QFS stream (10FB header, 3-byte big-endian size). Returns false on malformed data.
bool QfsDecompress(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out);

class Package {
public:
    bool Open(const std::string& path);
    const std::vector<Entry>& Entries() const { return entries_; }
    const Entry* Find(uint32_t type, uint32_t group, uint32_t instance) const;
    const Entry* FindFirstOfType(uint32_t type) const;
    // Returns the (decompressed) record bytes.
    bool Read(const Entry& e, std::vector<uint8_t>& out) const;
    uint32_t MajorVersion() const { return major_; }

private:
    std::vector<uint8_t> file_;     // whole file (maps are small relative to RAM; see Open)
    std::vector<Entry> entries_;
    std::unordered_map<TypeGroupInstance, size_t, TGIHash> lookup_;
    uint32_t major_ = 0;
};

}  // namespace dbpf
