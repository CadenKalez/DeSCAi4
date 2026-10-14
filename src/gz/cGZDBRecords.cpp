#include "gz/cGZDBRecords.h"

#include <cstdio>
#include <cstring>

#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

namespace {
constexpr std::uint32_t kInitialSpillSize = 2000001;   // constructor: records of at least this size
constexpr std::uint32_t kWriteSpillSize = 2000000;     // SetFieldVoid: position+length reaching this
}

// ------------------------------------------------------------------------------------------ RAM

cGZDBReadRecordRAM::cGZDBReadRecordRAM(Buffer data, std::uint32_t size, const cGZPersistResourceKey& key,
                                       cGZDBSegmentPackedFile* segment, bool littleEndian)
    : cGZPersistDBSerialRecord(key, segment ? static_cast<cIGZPersistDBSegment*>(segment) : nullptr, littleEndian),
      data_(data ? std::move(data) : std::make_shared<std::vector<std::uint8_t>>()),
      size_(size) {
    kind_ = kKindRAMRead;
    accessFlags_ = 1;
}

bool cGZDBReadRecordRAM::SeekAbsolute(std::uint32_t position) {
    if (accessFlags_ == 0 || position >= size_) return false;
    position_ = position;
    return true;
}

bool cGZDBReadRecordRAM::SeekRelative(std::int32_t delta) {
    std::int32_t target = delta + static_cast<std::int32_t>(position_);
    if (accessFlags_ == 0 || target < 0 || static_cast<std::uint32_t>(target) >= size_) return false;
    position_ = static_cast<std::uint32_t>(target);
    return true;
}

bool cGZDBReadRecordRAM::GetFieldVoid(void* destination, std::uint32_t byteCount) {
    if (accessFlags_ == 0) return false;
    std::uint32_t end = position_ + byteCount;
    if (static_cast<std::int32_t>(end) > static_cast<std::int32_t>(size_) || !data_) return false;
    std::memmove(destination, data_->data() + position_, byteCount);
    position_ = end;
    return true;
}

bool cGZDBReadRecordRAM::SetFieldVoid(const void* source, std::uint32_t byteCount) {
    if (accessFlags_ == 0) return false;
    std::uint32_t end = position_ + byteCount;
    if (static_cast<std::int32_t>(end) >= static_cast<std::int32_t>(size_) || !data_) return false;
    std::memcpy(data_->data() + position_, source, byteCount);
    position_ = end;
    return true;
}

void cGZDBReadRecordRAM::DoPostClose() {
    accessFlags_ = 0;
    if (segment_) {
        segment_->Release();
        segment_ = nullptr;
    }
    data_.reset();
    position_ = 0;
}

// ------------------------------------------------------------------------------- packed (lazy)

cGZDBReadRecordPackedFile::cGZDBReadRecordPackedFile(std::uint32_t fileOffset, std::uint32_t size,
                                                     const cGZPersistResourceKey& key,
                                                     cGZDBSegmentPackedFile* segment, bool littleEndian)
    : cGZPersistDBSerialRecord(key, segment ? static_cast<cIGZPersistDBSegment*>(segment) : nullptr, littleEndian),
      packed_(segment), fileOffset_(fileOffset), size_(size) {
    kind_ = kKindPackedRead;
    accessFlags_ = 1;
}

bool cGZDBReadRecordPackedFile::SeekAbsolute(std::uint32_t position) {
    if (accessFlags_ == 0 || position >= size_) return false;
    position_ = position;
    return true;
}

bool cGZDBReadRecordPackedFile::SeekRelative(std::int32_t delta) {
    std::uint32_t target = static_cast<std::uint32_t>(delta) + position_;
    if (accessFlags_ == 0 || target >= size_) return false;
    position_ = target;
    return true;
}

bool cGZDBReadRecordPackedFile::GetFieldVoid(void* destination, std::uint32_t byteCount) {
    if (accessFlags_ == 0 || !packed_ || position_ + byteCount > size_) return false;
    if (!packed_->ReadFileSpan(destination, position_ + fileOffset_, byteCount)) return false;
    position_ += byteCount;
    return true;
}

void cGZDBReadRecordPackedFile::DoPostClose() {
    accessFlags_ = 0;
    if (segment_) {
        segment_->Release();
        segment_ = nullptr;
    }
    packed_ = nullptr;
    position_ = 0;
}

// ------------------------------------------------------------------------------------- writer

cGZDBWriteRecordPackedFile::cGZDBWriteRecordPackedFile(std::uint32_t fileOffset, std::uint32_t size,
                                                       const cGZPersistResourceKey& key,
                                                       cGZDBSegmentPackedFile* segment, bool littleEndian)
    : cGZPersistDBSerialRecord(key, segment ? static_cast<cIGZPersistDBSegment*>(segment) : nullptr, littleEndian),
      packed_(segment) {
    kind_ = kKindWrite;
    accessFlags_ = 3;
    if (size == 0) return;
    if (size >= kInitialSpillSize && !triedSpill_) {
        triedSpill_ = true;
        char name[64];
        std::snprintf(name, sizeof name, "Record_%08x_", key.type);
        cRZString path(name);
        if (cRZFile::MakeTempPathName(path, nullptr)) {
            tempFile_ = new cRZFile(path);
            tempFile_->AddRef();
            bool ok = tempFile_->Open(3, 3, 1);
            if (ok) {
                std::vector<std::uint8_t> chunk(0x4000);
                tempFile_->SeekToPosition(0);
                std::uint32_t done = 0;
                bool wrote = true;
                while (done < size && wrote) {
                    std::uint32_t n = std::min<std::uint32_t>(size - done, 0x4000);
                    if (!packed_->ReadFileSpan(chunk.data(), fileOffset + done, n)) { ok = false; break; }
                    wrote = tempFile_->Write(chunk.data(), n);
                    done += n;
                }
                if (ok && wrote) tempFile_->SeekToPosition(0);
                else ok = false;
            }
            if (!ok) DiscardTemporaryFile();
        }
    }
    if (!tempFile_) {
        buffer_.resize(size);
        packed_->ReadFileSpan(buffer_.data(), fileOffset, size);
    }
}

cGZDBWriteRecordPackedFile::cGZDBWriteRecordPackedFile(const std::uint8_t* data, std::uint32_t size,
                                                       const cGZPersistResourceKey& key,
                                                       cGZDBSegmentPackedFile* segment, bool littleEndian)
    : cGZPersistDBSerialRecord(key, segment ? static_cast<cIGZPersistDBSegment*>(segment) : nullptr, littleEndian),
      packed_(segment) {
    kind_ = kKindWrite;
    accessFlags_ = 3;
    if (size) buffer_.assign(data, data + size);
}

cGZDBWriteRecordPackedFile::~cGZDBWriteRecordPackedFile() { DiscardTemporaryFile(); }

void cGZDBWriteRecordPackedFile::DiscardTemporaryFile() {
    if (!tempFile_) return;
    if (tempFile_->IsOpen()) {
        tempFile_->Close();
        cRZFile::Remove(*tempFile_->GetPath());
    }
    tempFile_->Release();
    tempFile_ = nullptr;
}

bool cGZDBWriteRecordPackedFile::SpillToTemporaryFile() {
    triedSpill_ = true;
    char name[64];
    std::snprintf(name, sizeof name, "Record_%08x_", key_.type);
    cRZString path(name);
    if (!cRZFile::MakeTempPathName(path, nullptr)) return false;
    tempFile_ = new cRZFile(path);
    tempFile_->AddRef();
    if (!tempFile_->Open(3, 3, 1)) { DiscardTemporaryFile(); return false; }
    tempFile_->SeekToPosition(0);
    tempFile_->SetLength(0);
    if (!tempFile_->Write(buffer_.data(), static_cast<std::uint32_t>(buffer_.size()))) {
        DiscardTemporaryFile();
        return false;
    }
    return true;
}

std::uint32_t cGZDBWriteRecordPackedFile::GetSize() {
    if (accessFlags_ == 0) return 0;
    if (tempFile_) return tempFile_->Length();
    return static_cast<std::uint32_t>(buffer_.size());
}

bool cGZDBWriteRecordPackedFile::SetSize(std::uint32_t size) {
    if (accessFlags_ == 0) return false;
    if (tempFile_) return tempFile_->SetLength(size);
    buffer_.resize(size);
    return true;
}

std::uint32_t cGZDBWriteRecordPackedFile::GetPosition() {
    if (accessFlags_ == 0) return 0;
    if (tempFile_) return static_cast<std::uint32_t>(tempFile_->Position());
    return position_;
}

bool cGZDBWriteRecordPackedFile::SeekAbsolute(std::uint32_t position) {
    if (accessFlags_ == 0) return false;
    if (tempFile_) return tempFile_->Seek(static_cast<int>(position), 0) == position;
    position_ = position;
    return true;
}

bool cGZDBWriteRecordPackedFile::SeekRelative(std::int32_t delta) {
    std::int64_t target = static_cast<std::int64_t>(delta) + GetPosition();
    if (target < 0) return false;
    return SeekAbsolute(static_cast<std::uint32_t>(target));
}

bool cGZDBWriteRecordPackedFile::GetFieldVoid(void* destination, std::uint32_t byteCount) {
    if (accessFlags_ == 0) return false;
    if (tempFile_) return tempFile_->Read(destination, byteCount);
    if (static_cast<std::uint64_t>(position_) + byteCount > buffer_.size()) return false;
    std::memmove(destination, buffer_.data() + position_, byteCount);
    position_ += byteCount;
    return true;
}

bool cGZDBWriteRecordPackedFile::SetFieldVoid(const void* source, std::uint32_t byteCount) {
    if (accessFlags_ == 0) return false;
    if (tempFile_) return tempFile_->Write(source, byteCount);
    if (static_cast<std::int32_t>(position_ + byteCount) >= static_cast<std::int32_t>(kWriteSpillSize) &&
        !triedSpill_) {
        if (SpillToTemporaryFile()) {
            tempFile_->Write(source, byteCount);
            buffer_.clear();
            position_ = 0;
            return true;
        }
    }
    const auto* bytes = static_cast<const std::uint8_t*>(source);
    if (byteCount < 0x40 && position_ == buffer_.size()) {
        buffer_.insert(buffer_.end(), bytes, bytes + byteCount);
    } else {
        if (buffer_.size() < static_cast<std::size_t>(position_) + byteCount)
            buffer_.resize(static_cast<std::size_t>(position_) + byteCount, 0);
        std::memmove(buffer_.data() + position_, bytes, byteCount);
    }
    position_ += byteCount;
    return true;
}

bool cGZDBWriteRecordPackedFile::WriteRecordToParent(std::uint32_t fileOffset, std::uint32_t size) {
    if (accessFlags_ == 0) return false;
    if (!tempFile_) {
        std::uint32_t n = std::min<std::uint32_t>(size, static_cast<std::uint32_t>(buffer_.size()));
        return packed_->WriteFileSpan(buffer_.data(), fileOffset, n);
    }
    std::uint32_t saved = static_cast<std::uint32_t>(tempFile_->Position());
    std::vector<std::uint8_t> chunk(0x4000);
    tempFile_->SeekToPosition(0);
    bool ok = true;
    std::uint32_t done = 0;
    while (done < size) {
        std::uint32_t n = std::min<std::uint32_t>(size - done, 0x4000);
        if (!tempFile_->Read(chunk.data(), n)) { ok = false; break; }
        ok = packed_->WriteFileSpan(chunk.data(), fileOffset + done, n);
        done += n;
        if (!ok) break;
    }
    tempFile_->SeekToPosition(saved);
    return ok;
}

void cGZDBWriteRecordPackedFile::DoPostClose() {
    accessFlags_ = 0;
    if (segment_) {
        segment_->Release();
        segment_ = nullptr;
    }
    DiscardTemporaryFile();
    buffer_.clear();
    position_ = 0;
    triedSpill_ = false;
}
