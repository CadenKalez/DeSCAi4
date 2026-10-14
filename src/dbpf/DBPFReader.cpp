#include "dbpf/DBPFReader.h"

#include <cstdio>
#include <cstring>

namespace dbpf {

static uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool QfsDecompress(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    // Records start with a 4-byte compressed size then 0x10FB and 24-bit big-endian size.
    if (n < 9) return false;
    size_t p = 4;
    if (src[p] != 0x10 || src[p + 1] != 0xFB) return false;
    uint32_t outSize = uint32_t(src[p + 2]) << 16 | uint32_t(src[p + 3]) << 8 | src[p + 4];
    p += 5;
    out.assign(outSize, 0);
    size_t o = 0;
    while (p < n && o < outSize) {
        uint8_t c = src[p++];
        size_t plain = 0, copy = 0, off = 0;
        if (c < 0x80) {
            if (p >= n) return false;
            uint8_t b1 = src[p++];
            plain = c & 3;
            copy = ((c & 0x1C) >> 2) + 3;
            off = ((uint32_t(c & 0x60) << 3) | b1) + 1;
        } else if (c < 0xC0) {
            if (p + 1 >= n) return false;
            uint8_t b1 = src[p++], b2 = src[p++];
            plain = (b1 >> 6) & 3;
            copy = (c & 0x3F) + 4;
            off = ((uint32_t(b1 & 0x3F) << 8) | b2) + 1;
        } else if (c < 0xE0) {
            if (p + 2 >= n) return false;
            uint8_t b1 = src[p++], b2 = src[p++], b3 = src[p++];
            plain = c & 3;
            copy = ((uint32_t(c & 0x0C) << 6) | b3) + 5;
            off = ((uint32_t(c & 0x10) << 12) | (uint32_t(b1) << 8) | b2) + 1;
        } else if (c < 0xFC) {
            plain = (size_t(c & 0x1F) << 2) + 4;
        } else {
            plain = c & 3;
        }
        if (plain) {
            if (p + plain > n || o + plain > outSize) return false;
            std::memcpy(&out[o], &src[p], plain);
            p += plain;
            o += plain;
        }
        if (copy) {
            if (off > o || o + copy > outSize) return false;
            for (size_t i = 0; i < copy; ++i, ++o) out[o] = out[o - off];  // overlapping copy
        }
    }
    return o == outSize;
}

bool Package::Open(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len < 96) { std::fclose(f); return false; }
    file_.resize(size_t(len));
    size_t got = std::fread(file_.data(), 1, file_.size(), f);
    std::fclose(f);
    if (got != file_.size() || std::memcmp(file_.data(), "DBPF", 4) != 0) return false;
    major_ = Rd32(&file_[4]);
    uint32_t count = Rd32(&file_[0x24]), off = Rd32(&file_[0x28]);
    if (uint64_t(off) + uint64_t(count) * 20 > file_.size()) return false;
    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = &file_[off + size_t(i) * 20];
        Entry en;
        en.id = {Rd32(e), Rd32(e + 4), Rd32(e + 8)};
        en.offset = Rd32(e + 12);
        en.size = Rd32(e + 16);
        entries_.push_back(en);
    }
    // DIR record: (type, group, instance, uncompressedSize) * n
    for (const Entry& d : entries_) {
        if (d.id.type != 0xe86b1eef) continue;
        for (uint32_t q = 0; q + 16 <= d.size; q += 16) {
            const uint8_t* r = &file_[d.offset + q];
            TypeGroupInstance k{Rd32(r), Rd32(r + 4), Rd32(r + 8)};
            for (Entry& t : entries_)
                if (t.id == k) { t.uncompressedSize = Rd32(r + 12); break; }
        }
    }
    for (size_t i = 0; i < entries_.size(); ++i) lookup_.emplace(entries_[i].id, i);
    return true;
}

const Entry* Package::Find(uint32_t t, uint32_t g, uint32_t i) const {
    auto it = lookup_.find({t, g, i});
    return it == lookup_.end() ? nullptr : &entries_[it->second];
}

const Entry* Package::FindFirstOfType(uint32_t t) const {
    for (const Entry& e : entries_) if (e.id.type == t) return &e;
    return nullptr;
}

bool Package::Read(const Entry& e, std::vector<uint8_t>& out) const {
    if (uint64_t(e.offset) + e.size > file_.size()) return false;
    const uint8_t* p = &file_[e.offset];
    if (e.uncompressedSize) return QfsDecompress(p, e.size, out);
    out.assign(p, p + e.size);
    return true;
}

}  // namespace dbpf
