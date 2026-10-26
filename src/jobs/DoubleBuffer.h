#pragma once
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace jobs {

template <typename T>
class DoubleBuffer {
public:
    DoubleBuffer(std::size_t width, std::size_t height, const T& initial = T())
        : width_(width), height_(height), bufferA_(width * height, initial), bufferB_(width * height, initial) {}

    std::size_t Width() const { return width_; }
    std::size_t Height() const { return height_; }

    // Main thread only: the published buffer (Buffer_A).
    const T& Read(std::size_t x, std::size_t y) const { return bufferA_[y * width_ + x]; }
    const std::vector<T>& ReadAll() const { return bufferA_; }

    // Any thread: runs `writer` on Buffer_B while holding the write lock.
    template <typename Writer>
    void Write(Writer&& writer) {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writer(bufferB_);
    }

    // Main thread only, end of frame: publishes Buffer_B and keeps the previous frame as the new Buffer_B.
    void Swap() {
        std::lock_guard<std::mutex> lock(writeMutex_);
        std::swap(bufferA_, bufferB_);
    }

private:
    std::size_t width_;
    std::size_t height_;
    std::vector<T> bufferA_;
    std::vector<T> bufferB_;
    std::mutex writeMutex_;
};

}  // namespace jobs
