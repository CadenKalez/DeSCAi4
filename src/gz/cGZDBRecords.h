// Concrete database records of cGZDBSegmentPackedFile (kind 2 = RAM reader, kind 1 = lazy file
// reader, kind 3 = writer). Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (cGZDBReadRecordRAM 13, cGZDBReadRecordPackedFile 10, cGZDBWriteRecordPackedFile 14 functions).
//
// Quirks kept: a read record cannot seek to (or relative-seek onto) its last-byte+1 position; RAM
// records accept writes only strictly inside the buffer; write records are not bounds-checked on
// seeks and spill to a temporary file once they would reach 2,000,000 bytes.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "gz/cGZPersistDBSerialRecord.h"
#include "rz/cRZFile.h"

class cGZDBSegmentPackedFile;

class cGZDBReadRecordRAM final : public cGZPersistDBSerialRecord {
public:
    using Buffer = std::shared_ptr<std::vector<std::uint8_t>>;
    cGZDBReadRecordRAM(Buffer data, std::uint32_t size, const cGZPersistResourceKey& key,
                       cGZDBSegmentPackedFile* segment, bool littleEndian);

    bool GetFieldVoid(void* destination, std::uint32_t byteCount) override;
    bool SetFieldVoid(const void* source, std::uint32_t byteCount) override;
    std::uint32_t GetSize() override { return size_; }
    bool SetSize(std::uint32_t) override { return false; }
    std::uint32_t GetPosition() override { return position_; }
    bool SeekAbsolute(std::uint32_t position) override;
    bool SeekRelative(std::int32_t delta) override;
    void DoPostClose() override;

    const Buffer& SharedBuffer() const { return data_; }

private:
    Buffer data_;
    std::uint32_t position_ = 0;
    std::uint32_t size_ = 0;
};

class cGZDBReadRecordPackedFile final : public cGZPersistDBSerialRecord {
public:
    cGZDBReadRecordPackedFile(std::uint32_t fileOffset, std::uint32_t size, const cGZPersistResourceKey& key,
                              cGZDBSegmentPackedFile* segment, bool littleEndian);

    bool GetFieldVoid(void* destination, std::uint32_t byteCount) override;
    bool SetFieldVoid(const void*, std::uint32_t) override { return false; }
    std::uint32_t GetSize() override { return size_; }
    bool SetSize(std::uint32_t) override { return false; }
    std::uint32_t GetPosition() override { return position_; }
    bool SeekAbsolute(std::uint32_t position) override;
    bool SeekRelative(std::int32_t delta) override;
    void DoPostClose() override;

    std::uint32_t FileOffset() const { return fileOffset_; }

private:
    cGZDBSegmentPackedFile* packed_;
    std::uint32_t fileOffset_, position_ = 0, size_;
};

class cGZDBWriteRecordPackedFile final : public cGZPersistDBSerialRecord {
public:
    // Loads the existing record bytes [fileOffset, fileOffset+size) of the segment (size 0: empty).
    cGZDBWriteRecordPackedFile(std::uint32_t fileOffset, std::uint32_t size, const cGZPersistResourceKey& key,
                               cGZDBSegmentPackedFile* segment, bool littleEndian);
    // Starts from caller-supplied bytes (a decompressed record).
    cGZDBWriteRecordPackedFile(const std::uint8_t* data, std::uint32_t size, const cGZPersistResourceKey& key,
                               cGZDBSegmentPackedFile* segment, bool littleEndian);
    ~cGZDBWriteRecordPackedFile() override;

    bool GetFieldVoid(void* destination, std::uint32_t byteCount) override;
    bool SetFieldVoid(const void* source, std::uint32_t byteCount) override;
    std::uint32_t GetSize() override;
    bool SetSize(std::uint32_t size) override;
    std::uint32_t GetPosition() override;
    bool SeekAbsolute(std::uint32_t position) override;
    bool SeekRelative(std::int32_t delta) override;
    void DoPostClose() override;

    // Copies the record into the segment at [fileOffset, fileOffset+size).
    bool WriteRecordToParent(std::uint32_t fileOffset, std::uint32_t size);

private:
    bool SpillToTemporaryFile();
    void DiscardTemporaryFile();

    cGZDBSegmentPackedFile* packed_;
    std::vector<std::uint8_t> buffer_;
    std::uint32_t position_ = 0;
    cRZFile* tempFile_ = nullptr;
    bool triedSpill_ = false;
};
