// PORT-ONLY helpers (not recovered from the binary): in-memory implementations of the raw
// GetVoid/SetVoid layer so the recovered typed stream logic can be used and tested without files.
#pragma once
#include <cstring>
#include <vector>

#include "gz/cIStreamRemaining.h"
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"

class PortMemoryIStream final : public cRZIStream, public cIStreamRemaining {
public:
    PortMemoryIStream(const void* data, std::size_t size, bool dataIsLittleEndian = true)
        : cRZIStream(dataIsLittleEndian), data_(static_cast<const unsigned char*>(data)), size_(size) {}
    bool Skip(std::uint32_t n) override {
        if (error_ || n > size_ - pos_) { error_ = true; pos_ = size_; return false; }
        pos_ += n;
        return true;
    }
    bool GetVoid(void* dst, std::uint32_t n) override {
        if (error_ || n > size_ - pos_) { error_ = true; return false; }
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return true;
    }
    bool GetError() const override { return error_; }
    std::size_t Position() const { return pos_; }
    std::size_t RemainingBytes() const override { return size_ - pos_; }

private:
    const unsigned char* data_;
    std::size_t size_, pos_ = 0;
    bool error_ = false;
};

class PortMemoryOStream final : public cRZOStream {
public:
    explicit PortMemoryOStream(bool dataIsLittleEndian = true) : cRZOStream(dataIsLittleEndian) {}
    void Flush() override {}
    bool SetVoid(const void* src, std::uint32_t n) override {
        const auto* p = static_cast<const unsigned char*>(src);
        bytes_.insert(bytes_.end(), p, p + n);
        return true;
    }
    bool GetError() const override { return false; }
    const std::vector<unsigned char>& bytes() const { return bytes_; }

private:
    std::vector<unsigned char> bytes_;
};
