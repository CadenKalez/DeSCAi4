#include "gz/GimexReaderWriter.h"

#include "gz/GZCOMDirector.h"
#include "rz/cRZPath.h"

namespace {
IGimexLibrary* g_library = nullptr;
constexpr std::uint32_t kSerialRecordAsFileClass = 0x0a32c0da, kSerialRecordAsFileIid = 0x8a32c0ac;

// The extension of the file's path, as Gimex wants it.
std::string ExtensionOf(cIGZFile* file) {
    const cIGZString* path = file->GetPath();
    if (!path || path->Strlen() == 0) return std::string();
    return cRZPath(std::string(path->Data(), path->Strlen())).GetExtension();
}

// The record adapter answers a cIGZFile when asked at slot 0x28 (GetFile) after SetRecord (0x18).
class IRecordFile : public cIGZUnknown {
public:
    virtual void SetRecord(cIGZPersistDBSerialRecord* record) = 0;     // +0x18
    virtual void Reserved20() {}
    virtual cIGZFile* AsFile() = 0;                                    // +0x28
};
}  // namespace

IGimexLibrary* GimexLibrary() { return g_library; }
void SetGimexLibrary(IGimexLibrary* library) { g_library = library; }
std::recursive_mutex& GimexModuleLock() {
    static std::recursive_mutex lock;
    return lock;
}

// ---- reader ---------------------------------------------------------------------------------------------

bool cGZGimexReader::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cIGZGimexReader) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZGimexReader::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    delete this;
    return 0;
}

void cGZGimexReader::FinalRelease() {
    if (file_) {
        if (file_->IsOpen()) file_->Close();
        SetInput(static_cast<cIGZFile*>(nullptr));
    }
}

bool cGZGimexReader::SetInput(cIGZFile* file) {
    if (reading_) EndRead();
    bool ok = false;
    if (file && file->IsOpen()) {
        int access = 0, creation = 0, share = 0;
        file->GetOpenModes(access, creation, share);
        if (access & 1) {
            file->AddRef();
            ok = true;
        }
    }
    if (!ok) file = nullptr;
    if (file_) file_->Release();
    file_ = file;
    return ok;
}

bool cGZGimexReader::SetInput(cIGZPersistDBSerialRecord* record) {
    IRecordFile* adapter = nullptr;
    cIGZCOM* com = GZCOM();
    if (!com || !com->CreateInstance(kSerialRecordAsFileClass, kSerialRecordAsFileIid, reinterpret_cast<void**>(&adapter)) || !adapter) return false;
    adapter->SetRecord(record);
    const bool ok = SetInput(adapter->AsFile());
    adapter->Release();
    return ok;
}

bool cGZGimexReader::BeginRead(std::uint32_t format, std::uint32_t& imageCount) {
    IGimexLibrary* lib = g_library;
    if (reading_ || !file_ || !lib) return false;
    if (format - 0x30 < 3) {
        bool found = false;
        if (format - 0x31 < 2) {                               // by the extension of the file's name
            const std::string extension = ExtensionOf(file_);
            if (!extension.empty()) {
                std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
                if (lib->Find(extension.c_str()) != 0) {
                    module_ = lib->Get();
                    found = true;
                }
            }
        }
        if ((format | 2) == 0x32 && !found) {                  // ask every module how well it knows the file
            int best = 0, bestScore = 0;
            for (int candidate = 1; candidate != 0x30; ++candidate) {
                std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
                lib->Set(candidate);
                const int score = lib->Is(file_);
                if (bestScore < score) {
                    bestScore = score;
                    best = candidate;
                }
            }
            if (best != 0) module_ = best;
            else if (!found) return false;
        } else if (!found) {
            return false;
        }
    } else {
        module_ = static_cast<int>(format);
    }
    std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
    lib->Set(module_);
    imageCount = lib->Open(&stream_, file_);
    reading_ = true;
    return true;
}

bool cGZGimexReader::BeginRead(const char* extension, std::uint32_t& imageCount) {
    IGimexLibrary* lib = g_library;
    if (reading_ || !file_ || !lib) return false;
    std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
    const int module = lib->Find(extension);
    if (module == 0) return false;
    module_ = module;
    imageCount = lib->Open(&stream_, file_);
    reading_ = true;
    return true;
}

bool cGZGimexReader::EndRead() {
    if (!reading_) return false;
    bool ok = false;
    if (stream_) {
        std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
        if (g_library) {
            g_library->Set(module_);
            ok = g_library->Close(stream_);
        }
    }
    stream_ = nullptr;
    reading_ = false;
    module_ = 0;
    return ok;
}

cGZGimexInfo* cGZGimexReader::GetFrameInfo(std::uint32_t index) {
    if (!g_library) return nullptr;
    std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
    g_library->Set(module_);
    return g_library->Info(stream_, index);
}

bool cGZGimexReader::GetFramePixels(cGZGimexInfo* info, void* pixels, std::uint32_t bytes) {
    if (!reading_ || !g_library) return false;
    std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
    g_library->Set(module_);
    return g_library->Read(stream_, info, pixels, bytes) != 0;
}

void cGZGimexReader::FreeGInfo(cGZGimexInfo* info) {
    if (g_library) g_library->FreeInfo(info);
}

// ---- writer ---------------------------------------------------------------------------------------------

bool cGZGimexWriter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cIGZGimexWriter) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZGimexWriter::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    delete this;
    return 0;
}

void cGZGimexWriter::FinalRelease() {
    if (file_) {
        if (file_->IsOpen()) file_->Close();
        SetOutput(static_cast<cIGZFile*>(nullptr));
    }
}

bool cGZGimexWriter::SetOutput(cIGZFile* file) {
    if (writing_) EndWrite();
    bool ok = false;
    if (file && file->IsOpen()) {
        int access = 0, creation = 0, share = 0;
        file->GetOpenModes(access, creation, share);
        if (access & 2) {
            file->AddRef();
            ok = true;
        }
    }
    if (!ok) file = nullptr;
    if (file_) file_->Release();
    file_ = file;
    return ok;
}

bool cGZGimexWriter::SetOutput(cIGZPersistDBSerialRecord* record) {
    IRecordFile* adapter = nullptr;
    cIGZCOM* com = GZCOM();
    if (!com || !com->CreateInstance(kSerialRecordAsFileClass, kSerialRecordAsFileIid, reinterpret_cast<void**>(&adapter)) || !adapter) return false;
    adapter->SetRecord(record);
    const bool ok = SetOutput(adapter->AsFile());
    adapter->Release();
    return ok;
}

bool cGZGimexWriter::DoBeginWrite(std::uint32_t flags) {
    std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
    g_library->Set(module_);
    writing_ = g_library->WriteOpen(&stream_, file_, flags);
    return writing_;
}

bool cGZGimexWriter::BeginWrite(std::uint32_t format, std::uint32_t flags) {
    if (writing_ || !file_ || !g_library) return false;
    if (format - 0x30 < 3) {
        if (format - 0x31 > 1) return false;                   // 0x30 (auto) cannot be used to write
        const std::string extension = ExtensionOf(file_);
        if (extension.empty()) return false;
        std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
        if (g_library->Find(extension.c_str()) == 0) return false;
        module_ = g_library->Get();
    } else {
        module_ = static_cast<int>(format);
    }
    std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
    return DoBeginWrite(flags);
}

bool cGZGimexWriter::BeginWrite(const char* extension, std::uint32_t flags) {
    if (writing_ || !file_ || !g_library) return false;
    std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
    module_ = g_library->Find(extension);
    if (module_ == 0) return false;
    return DoBeginWrite(flags);
}

bool cGZGimexWriter::EndWrite() {
    if (!writing_) return false;
    bool ok = false;
    if (stream_ && g_library) {
        std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
        g_library->Set(module_);
        ok = g_library->WriteClose(stream_);
    }
    module_ = 0;
    stream_ = nullptr;
    writing_ = false;
    return ok;
}

bool cGZGimexWriter::PutFramePixels(const cGZGimexInfo* info, const void* pixels, std::uint32_t bytes) {
    if (!writing_ || !g_library) return false;
    std::lock_guard<std::recursive_mutex> guard(GimexModuleLock());
    g_library->Set(module_);
    return g_library->Write(stream_, info, pixels, bytes) != 0;
}
