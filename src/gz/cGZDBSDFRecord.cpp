#include "gz/cGZDBSDFRecord.h"

namespace {
constexpr std::uint32_t kSeekFailed = 0xffffffffu;
// sDBSDFRecordSeekTranslation (0x100a228e0): eSeekMethod -> cRZFile origin.
constexpr int kSeekTranslation[3] = {0, 1, 2};
}  // namespace

cGZDBSDFRecord::~cGZDBSDFRecord() {
    if (file_.IsOpen() || backup_.IsOpen()) AbortInternal();
}

bool cGZDBSDFRecord::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZPersistDBSDFRecord) {
        *out = static_cast<cIGZPersistDBSDFRecord*>(this);
        AddRef();
        return true;
    }
    return cGZPersistDBSerialRecord::QueryInterface(iid, out);
}

bool cGZDBSDFRecord::Init(const cGZPersistResourceKey& key, const cIGZString& path, dbsdfFlags mode) {
    mode_ = mode;
    if (mode == kModeRead) accessFlags_ = 1;
    else if (mode == kModeRewrite || mode == kModeCreate) accessFlags_ = 2;
    if (initialized_) return false;

    file_.SetPath(path);
    file_.SetBuffering(0x1000, 0x1000);
    if (mode_ == kModeRewrite) {
        cRZString temp;
        if (!cRZFile::MakeTempPathName(temp, nullptr)) return false;
        backup_.SetPath(temp);
        if (!backup_.Copy(static_cast<const cIGZFile&>(file_), true)) return false;
    }
    int access = 0, creation = 0;
    bool known = true;
    if (mode_ == kModeRead) { access = 1; creation = 2; }
    else if (mode_ == kModeCreate) { access = 2; creation = 0; }
    else if (mode_ == kModeRewrite) { access = 2; creation = 4; }
    else known = false;
    if (known && file_.Open(access, creation, 1)) {
        key_ = key;
        initialized_ = true;
        return true;
    }
    AbortInternal();
    return false;
}

bool cGZDBSDFRecord::CloseInternal() {
    if (!initialized_) return false;
    initialized_ = false;
    if (file_.IsOpen()) file_.Close();
    if (mode_ == kModeRewrite) {
        backup_.Remove();
        mode_ = 0;
    }
    return true;
}

bool cGZDBSDFRecord::AbortInternal() {
    if (!initialized_) return false;
    initialized_ = false;
    if (file_.IsOpen()) file_.Close();
    if (mode_ == kModeRewrite) {
        bool restored = file_.Copy(static_cast<const cIGZFile&>(backup_), true);
        mode_ = 0;
        return restored;
    }
    return false;
}

std::uint32_t cGZDBSDFRecord::GetSize() { return initialized_ ? file_.Length() : kSeekFailed; }

std::uint32_t cGZDBSDFRecord::GetPosition() {
    return initialized_ ? static_cast<std::uint32_t>(file_.Position()) : kSeekFailed;
}

bool cGZDBSDFRecord::GetFieldVoid(void* destination, std::uint32_t byteCount) {
    return initialized_ && mode_ == kModeRead && file_.Read(destination, byteCount);
}

bool cGZDBSDFRecord::SetFieldVoid(const void* source, std::uint32_t byteCount) {
    return initialized_ && (mode_ & ~1u) == kModeRewrite && file_.Write(source, byteCount);
}

std::uint32_t cGZDBSDFRecord::Read(void* destination, std::uint32_t byteCount) {
    std::uint32_t count = 0;
    if (initialized_ && mode_ == kModeRead) {
        count = byteCount;
        file_.ReadWithCount(destination, count);
    }
    return count;
}

std::uint32_t cGZDBSDFRecord::Write(const void* source, std::uint32_t byteCount) {
    std::uint32_t count = 0;
    if (initialized_ && mode_ != kModeRead) {
        count = byteCount;
        file_.WriteWithCount(source, count);
    }
    return count;
}

void cGZDBSDFRecord::Flush() {
    if (initialized_ && mode_ != kModeRead) file_.Flush();
}

bool cGZDBSDFRecord::SeekAbsolute(std::uint32_t position) {
    return initialized_ && mode_ == kModeRead && file_.Seek(static_cast<int>(position), 0) != kSeekFailed;
}

bool cGZDBSDFRecord::SeekRelative(std::int32_t delta) {
    return initialized_ && mode_ == kModeRead && file_.Seek(delta, 1) != kSeekFailed;
}

bool cGZDBSDFRecord::Tell(std::uint32_t& position) {
    if (!initialized_) {
        position = 0;
        return false;
    }
    position = static_cast<std::uint32_t>(file_.Position());
    return true;
}

bool cGZDBSDFRecord::Seek(std::uint32_t offset, eSeekMethod method) {
    // The offset must lie within the file; the method indexes the translation table unchecked.
    if (!initialized_ || offset > file_.Length()) return false;
    return file_.Seek(static_cast<int>(offset), kSeekTranslation[method]) != kSeekFailed;
}

bool cGZDBSDFRecord::GetPath(cIGZString** out) {
    if (!initialized_) return false;
    auto* path = new cRZString();
    path->AddRef();
    file_.GetPath(*path);
    *out = path;
    return true;
}

bool cGZDBSDFRecord::GetName(cIGZString** out) {
    if (!initialized_) return false;
    auto* name = new cRZString();
    name->AddRef();
    cRZString path;
    file_.GetPath(path);
    cRZFile::GetPathName(path, *name, true);
    *out = name;
    return true;
}

bool cGZDBSDFRecord::GetExtension(cIGZString** out) {
    if (!initialized_) return false;
    auto* extension = new cRZString();
    extension->AddRef();
    cRZString path;
    file_.GetPath(path);
    cRZFile::GetPathExtension(path, *extension);
    *out = extension;
    return true;
}
