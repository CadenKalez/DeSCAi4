#include "audio/HitTrackDataBlock.h"

#include <filesystem>

// cHitTrackDataBlock::QueryInterface(unsigned int, void**) @ 0x1004c23ea
bool cHitTrackDataBlock::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIHitTrackDataBlock) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// cHitTrackDataBlock::Release() @ 0x1004c241a
std::uint32_t cHitTrackDataBlock::Release() {
    if (refCount_ > 1) {
        return static_cast<std::uint32_t>(--refCount_);
    }
    delete this;
    return 0;
}

// cHitTrackDataBlock::GetEntryPointOffset(unsigned int, unsigned int&) @ 0x1004c244a
bool cHitTrackDataBlock::GetEntryPointOffset(std::uint32_t entryKey, std::uint32_t& outOffset) {
    const auto it = entryPoints_.find(entryKey);
    if (it == entryPoints_.end()) return false;
    outOffset = it->second;
    return true;
}

// cHitTrackDataBlock::SetDataBlock(unsigned char*, unsigned int) @ 0x1004c2498
bool cHitTrackDataBlock::SetDataBlock(const std::uint8_t* data, std::uint32_t size) {
    data_ = data;
    dataSize_ = size;
    return true;
}

// cHitTrackDataBlock::AddEntryPoint(unsigned int, unsigned int) @ 0x1004c24a8
// Later calls for the same key overwrite the offset.
bool cHitTrackDataBlock::AddEntryPoint(std::uint32_t entryKey, std::uint32_t offset) {
    entryPoints_[entryKey] = offset;
    return true;
}

// cHitTrackDataBlock::SetFilename(cIGZString&) @ 0x1004c1030
// Approximation of cRZPath::GetDrive()+Fullpath(): a POSIX-absolute name is taken as having a
// "drive" and left alone, anything else is resolved against the application directory.
void cHitTrackDataBlock::SetFilename(const cIGZString& filename, const std::string& applicationDirectory) {
    filename_.Copy(filename);
    const std::string name = filename_.str();
    if (!name.empty() && name.front() != '/') {
        const auto resolved = (std::filesystem::path(applicationDirectory) / name).lexically_normal();
        filename_.AssignFromCString(resolved.string().c_str());
    }
    filename_.MakeUpper();
}
