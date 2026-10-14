#include "audio/SegmentDataSources.h"

#include <bit>
#include <cstring>

#include "rz/RZConvert.h"

namespace {

// XA tables read verbatim from the binary: xafilter2f @ 0x100a216f0 (4 floats), xafilter1f
// @ 0x100a21700 (4 floats), xatablef @ 0x100a21710 (16 x 16 floats, row = shift nibble).
// Adjacent in memory, so a filter nibble above 3 reads into the following tables like the original.
constexpr std::uint32_t kXaTableBits[264] = {
    0x00000000, 0x00000000, 0xbf500000, 0xbf5c0000, 0x00000000, 0x3f700000, 0x3fe60000, 0x3fc40000,
    0x00000000, 0x45800000, 0x46000000, 0x46400000, 0x46800000, 0x46a00000, 0x46c00000, 0x46e00000,
    0xc7000000, 0xc6e00000, 0xc6c00000, 0xc6a00000, 0xc6800000, 0xc6400000, 0xc6000000, 0xc5800000,
    0x00000000, 0x45000000, 0x45800000, 0x45c00000, 0x46000000, 0x46200000, 0x46400000, 0x46600000,
    0xc6800000, 0xc6600000, 0xc6400000, 0xc6200000, 0xc6000000, 0xc5c00000, 0xc5800000, 0xc5000000,
    0x00000000, 0x44800000, 0x45000000, 0x45400000, 0x45800000, 0x45a00000, 0x45c00000, 0x45e00000,
    0xc6000000, 0xc5e00000, 0xc5c00000, 0xc5a00000, 0xc5800000, 0xc5400000, 0xc5000000, 0xc4800000,
    0x00000000, 0x44000000, 0x44800000, 0x44c00000, 0x45000000, 0x45200000, 0x45400000, 0x45600000,
    0xc5800000, 0xc5600000, 0xc5400000, 0xc5200000, 0xc5000000, 0xc4c00000, 0xc4800000, 0xc4000000,
    0x00000000, 0x43800000, 0x44000000, 0x44400000, 0x44800000, 0x44a00000, 0x44c00000, 0x44e00000,
    0xc5000000, 0xc4e00000, 0xc4c00000, 0xc4a00000, 0xc4800000, 0xc4400000, 0xc4000000, 0xc3800000,
    0x00000000, 0x43000000, 0x43800000, 0x43c00000, 0x44000000, 0x44200000, 0x44400000, 0x44600000,
    0xc4800000, 0xc4600000, 0xc4400000, 0xc4200000, 0xc4000000, 0xc3c00000, 0xc3800000, 0xc3000000,
    0x00000000, 0x42800000, 0x43000000, 0x43400000, 0x43800000, 0x43a00000, 0x43c00000, 0x43e00000,
    0xc4000000, 0xc3e00000, 0xc3c00000, 0xc3a00000, 0xc3800000, 0xc3400000, 0xc3000000, 0xc2800000,
    0x00000000, 0x42000000, 0x42800000, 0x42c00000, 0x43000000, 0x43200000, 0x43400000, 0x43600000,
    0xc3800000, 0xc3600000, 0xc3400000, 0xc3200000, 0xc3000000, 0xc2c00000, 0xc2800000, 0xc2000000,
    0x00000000, 0x41800000, 0x42000000, 0x42400000, 0x42800000, 0x42a00000, 0x42c00000, 0x42e00000,
    0xc3000000, 0xc2e00000, 0xc2c00000, 0xc2a00000, 0xc2800000, 0xc2400000, 0xc2000000, 0xc1800000,
    0x00000000, 0x41000000, 0x41800000, 0x41c00000, 0x42000000, 0x42200000, 0x42400000, 0x42600000,
    0xc2800000, 0xc2600000, 0xc2400000, 0xc2200000, 0xc2000000, 0xc1c00000, 0xc1800000, 0xc1000000,
    0x00000000, 0x40800000, 0x41000000, 0x41400000, 0x41800000, 0x41a00000, 0x41c00000, 0x41e00000,
    0xc2000000, 0xc1e00000, 0xc1c00000, 0xc1a00000, 0xc1800000, 0xc1400000, 0xc1000000, 0xc0800000,
    0x00000000, 0x40000000, 0x40800000, 0x40c00000, 0x41000000, 0x41200000, 0x41400000, 0x41600000,
    0xc1800000, 0xc1600000, 0xc1400000, 0xc1200000, 0xc1000000, 0xc0c00000, 0xc0800000, 0xc0000000,
    0x00000000, 0x3f800000, 0x40000000, 0x40400000, 0x40800000, 0x40a00000, 0x40c00000, 0x40e00000,
    0xc1000000, 0xc0e00000, 0xc0c00000, 0xc0a00000, 0xc0800000, 0xc0400000, 0xc0000000, 0xbf800000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
};
inline float XaFilter2(unsigned index) { return std::bit_cast<float>(kXaTableBits[index]); }
inline float XaFilter1(unsigned index) { return std::bit_cast<float>(kXaTableBits[4 + index]); }
inline float XaTable(unsigned shift, unsigned nibble) { return std::bit_cast<float>(kXaTableBits[8 + shift * 16 + nibble]); }

// SUB42(value + 12582912.0f, 0): the low 16 bits of the float sum (round-to-nearest, wraps).
inline std::int16_t RoundToShort(float value) {
    return static_cast<std::int16_t>(std::bit_cast<std::uint32_t>(value + 12582912.0f) & 0xffffu);
}

}  // namespace

// ============================================================================================
// shared base
// ============================================================================================

bool cGZSndSegmentSourceBase::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZSndDataSource) {
        *out = static_cast<cIGZSndDataSource*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZSndSegmentSourceBase::Release() {
    if (refCount_ == 1) {
        refCount_ = 0;
        Close();
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

void cGZSndSegmentSourceBase::ReadReset() {
    position_ = 0;
    Close();
    Open();
}

bool cGZSndSegmentSourceBase::OpenRecord() {
    record_ = nullptr;
    cIGZPersistRecordService* service = Audio::PersistService();
    if (!service) return false;
    cIGZPersistDBRecord* opened = nullptr;
    if (!service->OpenRecord(key_, &opened, 0) || !opened) return false;
    record_ = opened;
    opened->Release();   // the call's reference
    return true;
}

void cGZSndSegmentSourceBase::CloseRecord() {
    access_ = nullptr;
    if (record_) {
        record_->Close();
        record_ = nullptr;
    }
}

// ============================================================================================
// wav
// ============================================================================================

// @ 0x10046e2a4: reads dwords until `fourCC` (0 matches immediately), then the dword after it.
bool cGZSndSegmentWavDataSource::FindChunk(std::uint32_t fourCC, std::uint32_t& size) {
    std::uint32_t word = 0;
    if (fourCC != 0) {
        bool ok;
        do {
            ok = access_->GetFieldUint32(&word, 1);
            if (word == fourCC) break;
        } while (ok);
        if (!ok) return false;
    }
    std::uint32_t chunkSize = 0;
    if (!access_->GetFieldUint32(&chunkSize, 1)) return false;
    size = chunkSize;
    return true;
}

// @ 0x10046e120
bool cGZSndSegmentWavDataSource::Open() {
    if (open_) {
        if (position_ == 0) return true;
        Close();
    }
    if (!OpenRecord()) return false;
    open_ = true;
    void* raw = nullptr;
    if (!record_->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw)) {
        Close();
        return false;
    }
    access_ = static_cast<cIGZPersistDBSerialRecord*>(raw);
    access_->Release();

    std::uint32_t formatSize = 0;
    if (!FindChunk(0x20746d66 /* 'fmt ' */, formatSize)) { Close(); return false; }
    // The original reads `formatSize` bytes into its 20-byte format block without bounds check.
    std::uint8_t formatBytes[256] = {};
    const std::uint32_t toRead = formatSize < sizeof(formatBytes) ? formatSize : sizeof(formatBytes);
    access_->GetFieldVoid(formatBytes, toRead);
    std::memset(&format_, 0, sizeof(format_));
    std::memcpy(&format_, formatBytes, toRead < sizeof(format_) ? toRead : sizeof(format_));
    format_.extra = 0;
    if (!FindChunk(0x61746164 /* 'data' */, dataSize_)) { Close(); return false; }
    position_ = 0;
    return true;
}

// @ 0x10046e318
bool cGZSndSegmentWavDataSource::Close() {
    if (open_) {
        if (record_) record_->Close();
        access_ = nullptr;
        open_ = false;
        record_ = nullptr;
    }
    return true;
}

bool cGZSndSegmentWavDataSource::Shutdown() {
    Close();
    if (lockCount_ != 0) {
        lockBuffer_.reset();
        lockCount_ = 0;
    }
    return true;
}

bool cGZSndSegmentWavDataSource::Lock(std::uint8_t** data, std::uint32_t*) {
    if (lockCount_++ == 0) lockBuffer_ = std::make_unique<std::uint8_t[]>(dataSize_);
    *data = lockBuffer_.get();
    return true;
}

bool cGZSndSegmentWavDataSource::Unlock() {
    if (--lockCount_ == 0) lockBuffer_.reset();
    return true;
}

std::int32_t cGZSndSegmentWavDataSource::ReadPart(std::uint8_t* destination, std::int32_t byteCount) {
    if (!open_ && !Open()) return 0;
    std::uint32_t count = dataSize_ - position_;
    if (static_cast<std::uint32_t>(byteCount) <= count) count = static_cast<std::uint32_t>(byteCount);
    if (lockCount_ == 0) access_->GetFieldVoid(destination, count);
    else std::memcpy(destination, lockBuffer_.get() + position_, count);
    position_ += count;
    return static_cast<std::int32_t>(count);
}

bool cGZSndSegmentWavDataSource::DataIsValid() {
    if (valid_) return true;
    if (validSet_) return false;
    SetDataIsValid(Open());
    Close();
    return valid_;
}

// ============================================================================================
// XA
// ============================================================================================

bool cGZSndSegmentDataSourceXA::Open() {
    if (open_) {
        if (position_ == 0) return true;
        Close();
    }
    if (!OpenRecord()) return false;
    open_ = true;
    void* raw = nullptr;
    if (!record_->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw)) return false;   // stays "open", like the original
    access_ = static_cast<cIGZPersistDBSerialRecord*>(raw);
    access_->Release();

    std::uint8_t header[8];
    if (!access_->GetFieldVoid(header, 8)) { lastError_ = 4; Close(); return false; }
    if (header[0] != 'X' || header[1] != 'A') { lastError_ = 3; Close(); return false; }
    std::memcpy(&dataSize_, header + 4, 4);

    std::uint8_t fmt[16];
    if (!access_->GetFieldVoid(fmt, 16)) { lastError_ = 4; Close(); return false; }
    std::uint16_t tag;
    std::memcpy(&tag, fmt, 2);
    if (tag != 1) { lastError_ = 3; Close(); return false; }
    format_.formatTag = 1;
    std::memcpy(&format_.channels, fmt + 2, 2);
    std::memcpy(&format_.samplesPerSecond, fmt + 4, 4);
    std::memcpy(&format_.averageBytesPerSecond, fmt + 8, 4);
    std::memcpy(&format_.blockAlign, fmt + 0xc, 2);
    std::memcpy(&format_.bitsPerSample, fmt + 0xe, 2);
    format_.extra = 0;

    remainingDecoded_ = dataSize_;
    consumed_ = 0x18;
    recordSize_ = access_->GetSize();
    position_ = 0;
    compressedIndex_ = 0xf00;
    outputIndex_ = 0;
    outputEnd_ = 0;
    return true;
}

bool cGZSndSegmentDataSourceXA::Close() {
    if (IsLocked()) Unlock();
    if (open_) {
        if (record_) record_->Close();
        access_ = nullptr;
        open_ = false;
        record_ = nullptr;
    }
    return true;
}

// @ 0x10046edd6: the locked buffer receives the raw record bytes (not decoded PCM).
bool cGZSndSegmentDataSourceXA::Lock(std::uint8_t** data, std::uint32_t* size) {
    if (lockCount_++ == 0) {
        lockBuffer_ = std::make_unique<std::uint8_t[]>(dataSize_);
        if (!Open()) {
            Unlock();
            return false;
        }
        if (!access_->GetFieldVoid(lockBuffer_.get(), dataSize_)) {
            Unlock();
            return false;
        }
        Close();
    }
    *data = lockBuffer_.get();
    *size = dataSize_;
    return true;
}

bool cGZSndSegmentDataSourceXA::Unlock() {
    if (--lockCount_ == 0) lockBuffer_.reset();
    return true;
}

bool cGZSndSegmentDataSourceXA::DataIsValid() {
    if (valid_) return true;
    if (validSet_) return false;
    SetDataIsValid(Open());
    Close();
    return valid_;
}

// @ 0x10046f2de: one 15-byte block (header + 14 data bytes) -> 28 mono samples (56 bytes).
bool cGZSndSegmentDataSourceXA::DecompressFrameMono() {
    if (compressedIndex_ > 0xeff) {
        std::int32_t count = 0xf00;
        if (recordSize_ < consumed_ + 0xf00u) count = static_cast<std::int32_t>(recordSize_ - consumed_);
        if (!access_->GetFieldVoid(compressed_, static_cast<std::uint32_t>(count))) {
            lastError_ = 4;
            return false;
        }
        consumed_ += static_cast<std::uint32_t>(count);
        compressedIndex_ = 0;
    }
    const std::uint8_t* block = compressed_ + compressedIndex_;
    const float f2 = XaFilter2(block[0] >> 4);
    const float f1 = XaFilter1(block[0] >> 4);
    const unsigned shift = block[0] & 0xf;
    float a = history_[0];   // +0xc
    float b = history_[1];   // +0x10
    for (int i = 0; i < 14; ++i) {
        const std::uint8_t byte = block[1 + i];
        b = b * f2 + f1 * a + XaTable(shift, byte >> 4);
        a = a * f2 + f1 * b + XaTable(shift, byte & 0xf);
        const std::int16_t s0 = RoundToShort(b);
        const std::int16_t s1 = RoundToShort(a);
        std::memcpy(decoded_ + i * 4, &s0, 2);
        std::memcpy(decoded_ + i * 4 + 2, &s1, 2);
    }
    history_[1] = b;
    history_[0] = a;
    compressedIndex_ += 0xf;
    outputIndex_ = 0;
    outputEnd_ = 0x38;
    return true;
}

// @ 0x10046f430: one 30-byte block (two headers + 14 byte pairs) -> 28 stereo frames (112 bytes).
bool cGZSndSegmentDataSourceXA::DecompressFrameStereo() {
    if (compressedIndex_ > 0xeff) {
        std::int32_t count = 0xf00;
        if (recordSize_ < consumed_ + 0xf00u) count = static_cast<std::int32_t>(recordSize_ - consumed_);
        if (!access_->GetFieldVoid(compressed_, static_cast<std::uint32_t>(count))) {
            lastError_ = 4;
            return false;
        }
        consumed_ += static_cast<std::uint32_t>(count);
        compressedIndex_ = 0;
    }
    const std::uint8_t* block = compressed_ + compressedIndex_;
    const float f2L = XaFilter2(block[0] >> 4), f1L = XaFilter1(block[0] >> 4);
    const float f2R = XaFilter2(block[1] >> 4), f1R = XaFilter1(block[1] >> 4);
    const unsigned shiftL = block[0] & 0xf, shiftR = block[1] & 0xf;
    float aL = history_[0], bL = history_[1];   // +0xc / +0x10
    float bR = history_[3], aR = history_[2];   // +0x18 / +0x14
    for (int i = 0; i < 14; ++i) {
        const std::uint8_t left = block[2 + i * 2];
        const std::uint8_t right = block[3 + i * 2];
        bL = bL * f2L + f1L * aL + XaTable(shiftL, left >> 4);
        aL = aL * f2L + f1L * bL + XaTable(shiftL, left & 0xf);
        bR = bR * f2R + f1R * aR + XaTable(shiftR, right >> 4);
        aR = aR * f2R + f1R * bR + XaTable(shiftR, right & 0xf);
        const std::int16_t s[4] = {RoundToShort(bL), RoundToShort(bR), RoundToShort(aL), RoundToShort(aR)};
        std::memcpy(decoded_ + i * 8, s, 8);
    }
    history_[1] = bL;
    history_[0] = aL;
    history_[3] = bR;
    history_[2] = aR;
    compressedIndex_ += 0x1e;
    outputIndex_ = 0;
    outputEnd_ = 0x70;
    return true;
}

// @ 0x10046f1d4: the position advances by the clamped request even when decoding fails midway.
std::int32_t cGZSndSegmentDataSourceXA::ReadPart(std::uint8_t* destination, std::int32_t byteCount) {
    std::int32_t count = static_cast<std::int32_t>(dataSize_ - position_);
    if (static_cast<std::uint32_t>(position_ + static_cast<std::uint32_t>(byteCount)) <= dataSize_) count = byteCount;

    if (!IsLocked()) {
        if (static_cast<std::int32_t>(remainingDecoded_) > 0) {
            const std::int32_t want = count < static_cast<std::int32_t>(remainingDecoded_) ? count : static_cast<std::int32_t>(remainingDecoded_);
            bool (cGZSndSegmentDataSourceXA::*decode)() = nullptr;
            if (format_.channels == 1) decode = &cGZSndSegmentDataSourceXA::DecompressFrameMono;
            else if (format_.channels == 2) decode = &cGZSndSegmentDataSourceXA::DecompressFrameStereo;
            if (decode) {
                bool failed = false;
                if (want > 0) {
                    std::int32_t left = want;
                    std::uint8_t* out = destination;
                    while (left > 0) {
                        std::int32_t available = outputEnd_ - outputIndex_;
                        if (available == 0 || outputIndex_ > outputEnd_) {
                            if (!(this->*decode)()) { failed = true; break; }
                            continue;
                        }
                        if (left < available) available = left;
                        std::memcpy(out, decoded_ + outputIndex_, static_cast<std::size_t>(available));
                        outputIndex_ += available;
                        out += available;
                        left -= available;
                    }
                    if (!failed) remainingDecoded_ -= static_cast<std::uint32_t>(want);
                }
            }
        }
    } else {
        std::memcpy(destination, lockBuffer_.get() + position_, static_cast<std::size_t>(count));
    }
    position_ += static_cast<std::uint32_t>(count);
    return count;
}

// ============================================================================================
// UTalk
// ============================================================================================

// @ 0x10046e71a: opens the record and parses its header ('UTM0', data size, 0x14, format block).
bool cGZSndSegmentDataSourceUTalk::OpenRecords() {
    if (compressed_) return true;
    if (!OpenRecord()) {
        access_ = nullptr;
        record_ = nullptr;
        return false;
    }
    access_ = nullptr;
    void* raw = nullptr;
    if (!record_->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw)) {
        CloseRecords();
        return false;
    }
    access_ = static_cast<cIGZPersistDBSerialRecord*>(raw);
    access_->Release();

    std::uint32_t word = 0;
    if (!access_->GetFieldUint32(&word, 1) || word != 0x304d5455) { CloseRecords(); return false; }
    if (!access_->GetFieldUint32(&dataSize_, 1)) { CloseRecords(); return false; }
    word = 0;
    access_->GetFieldUint32(&word, 1);
    if (word != 0x14) { CloseRecords(); return false; }
    if (!access_->GetFieldVoid(&format_, 0x14)) { CloseRecords(); return false; }
    const std::uint32_t total = access_->GetSize();
    if (total == 0) { CloseRecords(); return false; }
    compressedSize_ = (total - 0xc) - word;
    return true;
}

void cGZSndSegmentDataSourceUTalk::CloseRecords() {
    access_ = nullptr;
    if (record_) {
        record_->Close();
        record_ = nullptr;
    }
    compressedSize_ = 0;
    producedPadding_ = 0;
}

bool cGZSndSegmentDataSourceUTalk::Open() {
    dataSize_ = 0;
    if (OpenRecords()) {
        compressed_ = std::make_unique<std::uint8_t[]>(compressedSize_);
        access_->GetFieldVoid(compressed_.get(), compressedSize_);
        if (Audio::IUTalkCodec* codec = Audio::UTalkCodec()) codec->Init(compressed_.get(), state_);
        position_ = 0;
        SetDataIsValid(true);
    }
    return compressedSize_ != 0;
}

bool cGZSndSegmentDataSourceUTalk::Close() {
    if (IsLocked()) Unlock();
    compressed_.reset();
    CloseRecords();
    return true;
}

bool cGZSndSegmentDataSourceUTalk::Lock(std::uint8_t** data, std::uint32_t* size) {
    if (lockCount_++ == 0) {
        if (!Open()) {
            Unlock();
            return false;
        }
        lockBuffer_ = std::make_unique<std::uint8_t[]>(dataSize_);
        if (!access_->GetFieldVoid(lockBuffer_.get(), access_->GetSize())) {
            Unlock();
            return false;
        }
        Close();
    }
    *data = lockBuffer_.get();
    *size = access_ ? access_->GetSize() : 0;
    return true;
}

bool cGZSndSegmentDataSourceUTalk::Unlock() {
    if (--lockCount_ == 0) lockBuffer_.reset();
    return true;
}

bool cGZSndSegmentDataSourceUTalk::DataIsValid() {
    if (!validSet_) {
        SetDataIsValid(OpenRecords());
        CloseRecords();
    }
    return valid_;
}

// @ 0x10046ea48. Decodes 432-sample frames into 16-bit samples. Once a sample outside the 16-bit
// range appears the stream is finished; the whole destination is then zeroed (the original's
// behaviour, including samples decoded earlier in the same call) and only the bytes produced
// before the marker are reported.
std::int32_t cGZSndSegmentDataSourceUTalk::ReadPart(std::uint8_t* destination, std::int32_t byteCount) {
    std::int32_t notProduced = byteCount;
    if (!finished_) {
        if (byteCount == 0) {
            notProduced = 0;
        } else {
            Audio::IUTalkCodec* codec = Audio::UTalkCodec();
            std::int32_t remaining = byteCount;
            std::uint8_t* cursor = destination;
            bool done = false;
            while (!done) {
                if (codec) codec->Decode(state_);
                const std::uint32_t* samples = codec ? codec->Samples(state_) : nullptr;
                std::int32_t offset = 0;
                for (std::uint32_t index = 0; index < 0x1b0; ++index, offset += 2) {
                    std::uint32_t value = samples ? (samples[index] & 0x1ffff) : 0x8000;
                    if (value - 0x8000 < 0x10000) {
                        finished_ = true;
                        value = 0;
                    }
                    const auto sample = static_cast<std::uint16_t>(value);
                    std::memcpy(cursor + offset, &sample, 2);
                    if (remaining - 2 == offset) {
                        notProduced = 0;
                        done = true;
                        break;
                    }
                    if (finished_) {
                        notProduced = (remaining - 2) - offset;
                        done = true;
                        break;
                    }
                }
                if (!done) {
                    remaining -= 0x360;
                    cursor += 0x360;
                    if (remaining < 2) {   // odd request length: the original would run on
                        notProduced = 0;
                        done = true;
                    }
                }
            }
            if (!finished_) {
                producedPadding_ += static_cast<std::uint32_t>(notProduced);
                return byteCount - notProduced;
            }
        }
        if (!finished_) {
            producedPadding_ += static_cast<std::uint32_t>(notProduced);
            return byteCount - notProduced;
        }
    }
    std::memset(destination, 0, static_cast<std::size_t>(byteCount));
    producedPadding_ += static_cast<std::uint32_t>(notProduced);
    return byteCount - notProduced;
}

// ============================================================================================
// creator
// ============================================================================================

cGZSndSegmentDataSourceCreator::cGZSndSegmentDataSourceCreator() { persist_ = Audio::PersistService(); }

bool cGZSndSegmentDataSourceCreator::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZSndDataSourceCreator) {
        *out = static_cast<cIGZSndDataSourceCreator*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZSndSegmentDataSourceCreator::Release() {
    if (refCount_ == 1) {
        refCount_ = 0;
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

// @ 0x10046da54: removes entries equal to `id`, then appends the numeric value of `definition`.
bool cGZSndSegmentDataSourceCreator::AddSndDataSourceDef(std::uint32_t id, cIGZString& definition) {
    RemoveSndDataSourceDef(id);
    groups_.push_back(RZ::ConvertToUint(definition.ToChar()));
    return true;
}

void cGZSndSegmentDataSourceCreator::RemoveSndDataSourceDef(std::uint32_t id) {
    for (auto it = groups_.begin(); it != groups_.end(); ++it) {
        if (*it == id) {
            groups_.erase(it);
            return;
        }
    }
}

// @ 0x10046dbc2: finds the first registered group that holds the record, then asks the XA,
// UTalk and wav sources (in that order) for one that considers the data valid.
bool cGZSndSegmentDataSourceCreator::CreateSndDataSource(std::uint32_t id, std::uint32_t, cIGZVariant*,
                                                         std::uint32_t iid, void** out) {
    cIGZPersistRecordService* service = persist_.get();
    if (!service) return false;
    std::uint32_t group = 0;
    bool found = false;
    for (std::uint32_t candidate : groups_) {
        const cGZPersistResourceKey key{kGZResourceTypeSndSegment, candidate, id};
        cIGZPersistDBRecord* record = nullptr;
        const bool ok = service->FindRecord(key, &record);
        if (record) record->Release();
        if (ok) {
            group = candidate;
            found = true;
            break;
        }
    }
    if (!found) return false;

    cIGZSndDataSource* candidates[3] = {
        new cGZSndSegmentDataSourceXA(kGZResourceTypeSndSegment, group, id),
        nullptr,
        nullptr,
    };
    if (candidates[0]->DataIsValid()) return candidates[0]->QueryInterface(iid, out);
    candidates[0]->Close();
    delete candidates[0];
    candidates[1] = new cGZSndSegmentDataSourceUTalk(kGZResourceTypeSndSegment, group, id);
    if (candidates[1]->DataIsValid()) return candidates[1]->QueryInterface(iid, out);
    candidates[1]->Close();
    delete candidates[1];
    candidates[2] = new cGZSndSegmentWavDataSource(kGZResourceTypeSndSegment, group, id);
    if (candidates[2]->DataIsValid()) return candidates[2]->QueryInterface(iid, out);
    candidates[2]->Close();
    delete candidates[2];
    return false;
}
