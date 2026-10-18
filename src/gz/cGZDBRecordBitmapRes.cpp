#include "gz/cGZDBRecordBitmapRes.h"

#include <cstring>

bool cGZDBRecordBitmapRes::Init(void*, std::uint32_t) { return false; }

bool cGZDBRecordBitmapRes::GetFieldVoid(void* destination, std::uint32_t byteCount) {
    if (!data_ || size_ < position_ + byteCount) return false;
    std::memcpy(destination, data_.get() + position_, byteCount);
    position_ += byteCount;
    return true;
}

bool cGZDBRecordBitmapRes::SkipBytes(std::uint32_t byteCount) {
    if (data_) position_ += byteCount;
    return data_ != nullptr;
}
