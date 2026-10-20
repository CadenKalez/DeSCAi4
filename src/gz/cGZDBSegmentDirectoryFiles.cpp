#include "gz/cGZDBSegmentDirectoryFiles.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <vector>

#include "rz/RZStringUtils.h"
#include "rz/cRZFile.h"
#include "rz/nRZHash.h"

namespace {
DirectoryFilesServices g_services;

// isxdigit of a signed char the way the binary tests it: negative values never count.
bool IsHexDigit(char c) { return c >= 0 && std::isxdigit(static_cast<unsigned char>(c)) != 0; }

std::uint32_t HexField(const char* text) {          // strtoul(<8 chars>, 0, 16) on a private 8-character copy
    char buffer[9];
    std::memcpy(buffer, text, 8);
    buffer[8] = '\0';
    return static_cast<std::uint32_t>(std::strtoul(buffer, nullptr, 16));
}
}  // namespace

void SetDirectoryFilesServices(const DirectoryFilesServices& services) { g_services = services; }
const DirectoryFilesServices& GetDirectoryFilesServices() { return g_services; }

// ---------------------------------------------------------------------------------------------------------------
// cGZRESIDtoExtensionMap

bool cGZRESIDtoExtensionMap::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZPersistRESIDtoExtensionMap || iid == kGZIID_cIGZUnknown) {
        *out = static_cast<cIGZPersistRESIDtoExtensionMap*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZRESIDtoExtensionMap::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;                    // vslot 0x48: the deleting destructor
    return 0;
}

bool cGZRESIDtoExtensionMap::AddRESIDtoExtensionAssociation(std::uint32_t resid, const cIGZString& extension) {
    // The original descends the tree like an upper_bound insert: equal keys are appended after the existing ones.
    entries_.emplace(resid, cRZString(std::string(extension.ToChar(), extension.Strlen()).c_str()));
    return true;
}

bool cGZRESIDtoExtensionMap::RESIDToExtension(const std::uint32_t& resid, cIGZString& extension) {
    for (const auto& entry : entries_) {
        if (entry.first == resid) {
            extension.AssignFromCString(entry.second.ToChar());
            return true;
        }
    }
    return false;
}

bool cGZRESIDtoExtensionMap::ExtensionToRESID(const cIGZString& extension, std::uint32_t& resid) {
    const std::uint32_t length = extension.Strlen();
    for (const auto& entry : entries_) {
        const std::uint32_t stored = entry.second.Strlen();
        if (length == stored) {
            if (strncasecmp(extension.Data(), entry.second.ToChar(), length) == 0) {
                resid = entry.first;
                return true;
            }
        } else if (stored + 1 == length && length != 0 && extension.Data()[0] == '.') {
            // a leading dot on the lookup string is ignored
            if (strncasecmp(extension.Data() + 1, entry.second.ToChar(), length - 1) == 0) {
                resid = entry.first;
                return true;
            }
        }
    }
    return false;
}

void cGZRESIDtoExtensionMap::Copy(const cIGZPersistRESIDtoExtensionMap* other) {
    EraseAll();
    // The original walks the other object's tree directly (it only ever copies from this class).
    const auto* source = static_cast<const cGZRESIDtoExtensionMap*>(other);
    for (const auto& entry : source->entries_) AddRESIDtoExtensionAssociation(entry.first, entry.second);
}

// ---------------------------------------------------------------------------------------------------------------
// cGZFileNameToResKeyConverterDefault

bool cGZFileNameToResKeyConverterDefault::IsFileNameValid(const cIGZString& fileName) {
    cGZPersistResourceKey key{};
    return FileNameToKey(fileName, key);
}

// @ 0x1004d7842. The binary splits the name at the last '.' and reads hexadecimal fields from it exactly like the
// segment does, but never stores a key and always returns false (the function ends in XOR EAX,EAX); the parse has no
// side effects, so only the result is kept.
bool cGZFileNameToResKeyConverterDefault::FileNameToKey(const cIGZString&, cGZPersistResourceKey&) { return false; }

// ---------------------------------------------------------------------------------------------------------------
// cGZDBSegmentDirectoryFiles

void cGZDBSegmentDirectoryFiles::RecordData::EraseRecord(cIGZPersistDBRecord* record) {
    for (auto it = records.begin(); it != records.end(); ++it) {
        cGZDBSDFRecord* held = it->get();
        cIGZPersistDBRecord* view = held ? static_cast<cIGZPersistDBRecord*>(held) : nullptr;
        if (view == record) {
            records.erase(it);
            return;
        }
    }
}

cGZDBSegmentDirectoryFiles::cGZDBSegmentDirectoryFiles() = default;

cGZDBSegmentDirectoryFiles::~cGZDBSegmentDirectoryFiles() {
    Shutdown();
    // timer_ (cRZCallbackTimer::DestructInPlace), the maps and the path are destroyed by the members.
}

void cGZDBSegmentDirectoryFiles::FinalRelease() {
    Shutdown();                                  // vslot 0x28
    cGZPersistDBSegment::FinalRelease();
}

bool cGZDBSegmentDirectoryFiles::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZPersistDBSegmentDirectoryFiles) {
        *out = static_cast<cIGZPersistDBSegmentDirectoryFiles*>(this);
        AddRef();
        return true;
    }
    return cGZPersistDBSegment::QueryInterface(iid, out);
}

bool cGZDBSegmentDirectoryFiles::SetSegmentID(const std::uint32_t& id) {
    segmentId_ = id;
    if (groupId_ == 0) groupId_ = id;            // the default group follows the segment id until one is set
    return true;
}

bool cGZDBSegmentDirectoryFiles::Init() {
    if (!initialized_) initialized_ = true;
    return true;
}

bool cGZDBSegmentDirectoryFiles::Init(std::uint32_t segmentId, const cIGZString* path, bool) {
    if (initialized_) return false;
    initialized_ = true;
    requireDirectoryForWrite_ = true;
    flag2a_ = false;
    SetSegmentID(segmentId);
    path_.AssignFromIGZString(*path);

    if (g_services.registry &&
        !g_services.registry->RegisterCategoryCallback(kGZCATNAME_cGZPersistDBSegmentDirectoryFiles,
                                                       &CreateDBSegmentDirectoryFilesCallback, this)) {
        return false;
    }

    // "<anything>%DataDirectory%<rest>" becomes "<data directory><rest>": the text in front of the token is dropped.
    {
        const std::string& text = path_.str();
        const std::size_t at = text.find("%DataDirectory%");
        if (at != std::string::npos && g_services.dataDirectory) {
            const char* dataDirectory = g_services.dataDirectory();
            std::string rest = text.substr(at + 15);
            path_.str() = std::string(dataDirectory) + rest;
        }
    }
    // "<front>%Language%<rest>" becomes "<front><language directory><rest>".
    {
        const std::string& text = path_.str();
        const std::size_t at = text.find("%Language%");
        if (at != std::string::npos && g_services.languageDirectory) {
            cRZString language;
            g_services.languageDirectory(language);
            std::string front = text.substr(0, at);
            std::string rest = text.substr(at + 10);
            path_.str() = front + language.str() + rest;
        }
    }

    if (SetPath(path_) && Open(true, true)) return true;
    Shutdown();
    return false;
}

bool cGZDBSegmentDirectoryFiles::Shutdown() {
    if (initialized_) {
        initialized_ = false;
        timer_.Stop();
        if ((openFlags_ & 3) != 0) Close();      // vslot 0xf0
        nextInstance_ = 1;
        path_.str().clear();
        groupId_ = 0;
        segmentId_ = 0;                          // the binary clears +0x68 and +0x6c with one 8-byte store
        allowPrefix_ = true;
        allowSuffix_ = false;
        if (directory_) {
            delete directory_;
            directory_ = nullptr;
        }
        extensionMap_.EraseAll();
        records_.clear();
    }
    return true;
}

bool cGZDBSegmentDirectoryFiles::Open(bool read, bool write) {
    if ((openFlags_ & 3) != 0) return true;
    if (!initialized_ || (!read && !write) || path_.Strlen() == 0) {
        openFlags_ = 0;
        return false;
    }
    openFlags_ = read ? 1u : 0u;
    if (write) {
        openFlags_ = (read ? 1u : 0u) | 2u;
        if (requireDirectoryForWrite_ && !cRZDirectory::DoesDirectoryExist(path_)) {
            openFlags_ = 0;
            return false;
        }
    }
    if (!directory_) directory_ = new cRZDirectory(path_, false, false, false);
    if (g_services.registry &&
        !g_services.registry->RegisterCategoryCallback(kGZCATNAME_cGZPersistDBSegmentDirectoryFiles,
                                                       &CreateDBSegmentDirectoryFilesCallback, this)) {
        return false;                            // (the flags stay set, as in the binary)
    }
    const bool built = BuildKeyList();           // vslot 0x180
    if (built && changeDetection_) StartBackgroundThread();
    return built;
}

void cGZDBSegmentDirectoryFiles::StartBackgroundThread() {
    if (!timer_.IsRunning()) {
        const std::int32_t seconds = random_.RandomSint32RangeUniform(static_cast<std::int32_t>(detectionInterval_),
                                                                      static_cast<std::int32_t>(detectionInterval_) * 2);
        timer_.SetCallbackFunction(&StaticBackgroundThreadFunction, this);
        timer_.StartOneShot(static_cast<std::uint32_t>(seconds) * 1000u,
                            static_cast<cIGZPersistDBSegmentDirectoryFiles*>(this));
    }
}

void cGZDBSegmentDirectoryFiles::StaticBackgroundThreadFunction(void* segment) {
    static_cast<cGZDBSegmentDirectoryFiles*>(segment)->DoFileChangeCheck();     // vslot 0xc8
}

bool cGZDBSegmentDirectoryFiles::Close() {
    if ((openFlags_ & 3) != 0) {
        CloseOpenRecords();
        openFlags_ = 0;
        extensionMap_.EraseAll();
        records_.clear();
        if (directory_) {
            delete directory_;
            directory_ = nullptr;
        }
    }
    return true;
}

void cGZDBSegmentDirectoryFiles::CloseOpenRecords() {
    for (auto& entry : records_) {
        RecordData& data = entry.second;
        if (!data.records.empty()) {
            for (auto& record : data.records) record->CloseInternal();          // vslot 0x78
            data.records.clear();
        }
    }
}

void cGZDBSegmentDirectoryFiles::GetPath(cIGZString& out) const { out.AssignFromChars(path_.ToChar(), path_.Strlen()); }

bool cGZDBSegmentDirectoryFiles::SetPath(const cIGZString& path) {
    if ((openFlags_ & 3) != 0) return false;
    path_.AssignFromIGZString(path);
    SC4AdjustPathSeparatorsForPlatform(path_, path_);
    // (an empty path reads the byte in front of the buffer in the binary, which is never '/')
    if (path_.str().empty() || path_.str().back() != '/') path_.str().push_back('/');
    return true;
}

std::uint32_t cGZDBSegmentDirectoryFiles::GetRecordCount(cIGZPersistResourceKeyFilter* filter) {
    return GetResourceKeyList(nullptr, filter);                                  // vslot 0x118
}

std::uint32_t cGZDBSegmentDirectoryFiles::GetResourceKeyList(cIGZPersistResourceKeyList* list,
                                                             cIGZPersistResourceKeyFilter* filter) {
    std::uint32_t count = 0;
    for (const auto& entry : records_) {
        if (!filter || filter->IsKeyIncluded(entry.first)) {
            if (list) list->Insert(entry.first);
            ++count;
        }
    }
    return count;
}

cRZString cGZDBSegmentDirectoryFiles::RecordPath(const RecordData& data) const {
    return cRZString((path_.str() + data.fileName.str()).c_str());
}

bool cGZDBSegmentDirectoryFiles::DoTestForRecord(const cGZPersistResourceKey& key) { return records_.find(key) != records_.end(); }

bool cGZDBSegmentDirectoryFiles::DoOpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record,
                                              std::uint32_t flags) {
    if ((flags & 2) != 0) return OpenRecordInternal(key, record, cIGZPersistDBSDFRecord::kModeRewrite);
    if ((flags & 1) == 0) return false;
    return OpenRecordInternal(key, record, cIGZPersistDBSDFRecord::kModeRead);
}

bool cGZDBSegmentDirectoryFiles::DoOpenRecordForWrite(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record) {
    return OpenRecordInternal(key, record, cIGZPersistDBSDFRecord::kModeRewrite);
}

bool cGZDBSegmentDirectoryFiles::DoCreateNewRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record) {
    return OpenRecordInternal(key, record, cIGZPersistDBSDFRecord::kModeCreate);
}

bool cGZDBSegmentDirectoryFiles::DoCloseRecord(cIGZPersistDBRecord* record) {
    const cGZPersistResourceKey key = record->GetKey();
    auto it = records_.find(key);
    if (it == records_.end()) return false;
    for (auto& held : it->second.records) {
        cGZDBSDFRecord* sdf = held.get();
        if (sdf && static_cast<cIGZPersistDBRecord*>(sdf) == record) {
            sdf->CloseInternal();                                                 // vslot 0x78
            it->second.EraseRecord(record);
            return true;
        }
    }
    return false;
}

bool cGZDBSegmentDirectoryFiles::DoAbortRecord(cIGZPersistDBRecord* record) {
    const cGZPersistResourceKey key = record->GetKey();
    auto it = records_.find(key);
    if (it == records_.end()) return false;
    for (auto& held : it->second.records) {
        cGZDBSDFRecord* sdf = held.get();
        if (sdf && static_cast<cIGZPersistDBRecord*>(sdf) == record) {
            sdf->AbortInternal();                                                 // vslot 0x80
            it->second.EraseRecord(record);
            return true;
        }
    }
    return false;
}

bool cGZDBSegmentDirectoryFiles::DoDeleteRecord(const cGZPersistResourceKey& key) {
    if (!initialized_) return false;
    auto it = records_.find(key);
    if (it == records_.end() || !it->second.records.empty()) return false;
    cRZString path = RecordPath(it->second);
    records_.erase(it);
    cRZFile::Remove(path);
    return true;
}

std::uint32_t cGZDBSegmentDirectoryFiles::DoGetRecordSize(const cGZPersistResourceKey& key) {
    if (!initialized_) return 0xffffffffu;
    auto it = records_.find(key);
    if (it == records_.end()) return 0xffffffffu;
    if (!it->second.records.empty()) {
        std::uint32_t size = 0xffffffffu;
        for (auto& record : it->second.records) {
            if ((record->GetAccessFlags() & 0xfffffffeu) == 2) size = record->GetSize();   // writers know their size
        }
        if (size != 0xffffffffu) return size;
    }
    return cRZFile::GetLength(RecordPath(it->second));
}

std::uint32_t cGZDBSegmentDirectoryFiles::DoReadRecord(const cGZPersistResourceKey& key, void* buffer,
                                                       std::uint32_t& size) {
    std::uint32_t result = 0xffffffffu;
    std::uint32_t bytes = 0;
    if (initialized_) {
        auto it = records_.find(key);
        bytes = 0;
        if (it != records_.end()) {
            // A key with exactly one open handle that is a writer cannot be read.
            const bool writerOnly = it->second.records.size() == 1 &&
                                    (it->second.records.front()->GetAccessFlags() & 0xfffffffeu) == 2;
            if (!writerOnly) {
                cRZString path = RecordPath(it->second);
                if (buffer == nullptr) {
                    result = cRZFile::GetLength(path);
                    bytes = 0;
                } else {
                    cRZFile file(path);
                    if (!file.Open(1, 2, 1)) {
                        result = 0xffffffffu;
                        bytes = 0;
                    } else {
                        const std::uint32_t length = file.Length();
                        bytes = size;
                        if (length < size) bytes = length;
                        size = bytes;
                        file.ReadWithCount(buffer, size);
                        bytes = size;
                        file.Close();
                        result = bytes;
                    }
                }
            }
        }
    }
    size = bytes;
    return result;
}

bool cGZDBSegmentDirectoryFiles::GetRESIDtoExtensionMap(cIGZPersistRESIDtoExtensionMap** out) {
    auto* copy = new cGZRESIDtoExtensionMap(1);
    copy->Copy(&extensionMap_);
    *out = copy;
    return true;
}

bool cGZDBSegmentDirectoryFiles::GetFileNameFromKey(const cGZPersistResourceKey& key, cIGZString& out) {
    auto it = records_.find(key);
    if (it != records_.end()) out.Copy(it->second.fileName);
    return it != records_.end();
}

bool cGZDBSegmentDirectoryFiles::getFileNameFromKey(const cGZPersistResourceKey& key, cRZString& name,
                                                    cRZString& extension) {
    if (!extensionMap_.RESIDToExtension(key.type, extension)) return false;
    // "%8.8x" (instance) for the default group, "%8.8x%8.8x" (instance, group) otherwise
    if (key.group == groupId_) name.Sprintf("%8.8x", key.instance);
    else name.Sprintf("%8.8x%8.8x", key.instance, key.group);
    return true;
}

bool cGZDBSegmentDirectoryFiles::createFileNameFromKey(const cGZPersistResourceKey& key, cRZString& name,
                                                       cRZString& extension) {
    return getFileNameFromKey(key, name, extension);                             // vslot 0x190
}

bool cGZDBSegmentDirectoryFiles::GetFilePathFromKey(const cGZPersistResourceKey& key, cIGZString& out) {
    cRZString fileName;
    if (!GetFileNameFromKey(key, fileName)) return false;                        // vslot 0x88
    out.Copy(path_);
    out.AppendChars(fileName.Data(), fileName.Strlen());
    return true;
}

bool cGZDBSegmentDirectoryFiles::GetKeyFromFileName(const cIGZString& name, const cIGZString& extension,
                                                    cGZPersistResourceKey& key) {
    std::uint32_t group = groupId_;
    std::uint32_t instance = 0;
    const std::uint32_t length = name.Strlen();
    bool haveInstance = false;
    if (length > 7) {
        const char* text = name.ToChar();
        if (allowPrefix_) {
            // "<instance:8 hex>[<group:8 hex>]..." — count the hex digits at the front (at most 16)
            const int limit = length < 0x10 ? static_cast<int>(length) : 0x10;
            int run = 0;
            while (run < limit && IsHexDigit(text[run])) ++run;
            if (run >= 8) {
                instance = HexField(text);
                if (length >= 0x10 && run >= 0x10) group = HexField(text + 8);
                haveInstance = true;
            }
        }
        if (!haveInstance && allowSuffix_) {
            // "...<instance:8 hex>[<group:8 hex>]" — count the hex digits at the end (at most 16)
            const int limit = length > 0xf ? static_cast<int>(length) - 0x10 : 0;
            int index = static_cast<int>(length);
            do {
                --index;
                if (index < limit || !IsHexDigit(text[index])) break;
            } while (true);
            const std::uint32_t run = length - static_cast<std::uint32_t>(index) - 1;
            if (run > 7) {
                if (run < 0x10) {
                    instance = HexField(text + length - 8);
                } else {
                    group = HexField(text + length - 8);
                    instance = HexField(text + length - 16);
                }
                haveInstance = true;
            }
        }
    }
    if (!haveInstance) {
        if (!allowText_) return false;
        instance = nRZHash::CRC24(name.ToChar(), false) | 0xff000000u;
    }
    if (!extensionMap_.ExtensionToRESID(extension, key.type)) return false;
    key.group = group;
    key.instance = instance;
    return true;
}

bool cGZDBSegmentDirectoryFiles::IsFileNameValid(const cIGZString& fileName) {
    cRZString name, extension;
    cRZFile::GetPathName(fileName, name, false);
    cRZFile::GetPathExtension(fileName, extension);
    if (extension.Strlen() != 0 && extension.Data()[0] == '.') extension.str().erase(0, 1);
    cGZPersistResourceKey key{};
    return GetKeyFromFileName(name, extension, key);                             // vslot 0x98
}

bool cGZDBSegmentDirectoryFiles::GenerateNewKey(std::uint32_t type, cGZPersistResourceKey& key) {
    if (!initialized_) return false;
    key.group = groupId_;
    key.type = type;
    while (nextInstance_ != 0xffffffffu) {
        key.instance = nextInstance_++;
        if (records_.find(key) == records_.end()) return true;
    }
    return false;
}

std::uint32_t cGZDBSegmentDirectoryFiles::EnumerateRecordFiles(cIGZString** names, std::uint32_t count) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);                          // the critical section at +0x18
    if (names == nullptr) return static_cast<std::uint32_t>(records_.size());
    std::uint32_t n = 0;
    if (!records_.empty() && count != 0) {
        auto it = records_.begin();
        while (names[n] != nullptr) {
            names[n]->AssignFromChars(it->second.fileName.ToChar(), it->second.fileName.Strlen());
            ++it;
            ++n;
            if (it == records_.end() || count <= n) break;
        }
    }
    return n;
}

bool cGZDBSegmentDirectoryFiles::SetFileChangeDetection(bool enable, std::uint32_t intervalSeconds) {
    if (changeDetection_ != enable) {
        if (enable) {
            if (initialized_) StartBackgroundThread();
        } else {
            timer_.Stop();
        }
        changeDetection_ = enable;
    }
    detectionInterval_ = intervalSeconds > 0xf ? intervalSeconds : 0xf;           // at least 15 seconds
    return true;
}

bool cGZDBSegmentDirectoryFiles::OpenRecordInternal(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record,
                                                    cIGZPersistDBSDFRecord::dbsdfFlags mode) {
    if (mode == 0 || !initialized_) return false;
    auto existing = records_.find(key);
    if (existing == records_.end()) {
        if (mode == cIGZPersistDBSDFRecord::kModeRead) return false;              // a read needs an existing file
    } else if (!existing->second.records.empty()) {
        // Open handles exist: only another reader may join readers.
        const bool joinReaders = mode == cIGZPersistDBSDFRecord::kModeRead &&
                                 existing->second.records.front()->GetAccessFlags() == 1;
        if (!joinReaders) return false;
    }

    cRZString name, extension;
    if (mode == cIGZPersistDBSDFRecord::kModeCreate) {
        createFileNameFromKey(key, name, extension);                              // vslot 0x198
        name.str().push_back('.');
        name.str().append(extension.str());
    } else {
        GetFileNameFromKey(key, name);                                            // vslot 0x88
    }
    cRZString fullPath((path_.str() + name.str()).c_str());

    auto* sdf = new cGZDBSDFRecord(static_cast<cIGZPersistDBSegment*>(this));
    sdf->AddRef();
    if (!sdf->Init(key, fullPath, mode)) {
        sdf->Release();
        return false;
    }
    if (existing == records_.end()) {
        RecordData& data = records_[key];
        data.records.push_back(cRZAutoRefCount<cGZDBSDFRecord>(sdf));
        data.fileName = name;
    } else {
        existing->second.records.push_back(cRZAutoRefCount<cGZDBSDFRecord>(sdf));
    }
    if (!sdf->QueryInterface(kGZIID_cIGZPersistDBRecord, reinterpret_cast<void**>(record))) {
        records_[key].EraseRecord(static_cast<cIGZPersistDBRecord*>(sdf));
        sdf->Release();
        return false;
    }
    sdf->Release();
    return true;
}

bool cGZDBSegmentDirectoryFiles::BuildKeyList() {
    if (!cRZDirectory::DoesDirectoryExist(path_)) return false;
    std::list<cRZString> files;
    directory_->ReadDirectoryFileEntriesIntoStringList(files);
    // Quirk: the binary keeps one extension string for the whole loop and only overwrites it when a file has an
    // extension, so a file without one reuses the extension of the file before it.
    cRZString extensionString;
    for (const cRZString& file : files) {
        char drive[256], dir[256], name[256], ext[256];
        Splitpath(file.ToChar(), drive, dir, name, ext);
        cRZString nameString(name);
        if (ext[0] != '\0') extensionString.AssignFromCString(ext + 1);          // without the dot
        cGZPersistResourceKey key{};
        if (GetKeyFromFileName(nameString, extensionString, key) && records_.find(key) == records_.end()) {
            RecordData data;
            data.fileName = file;
            records_.emplace(key, std::move(data));
        }
    }
    return true;
}

bool cGZDBSegmentDirectoryFiles::AddFile(const cRZString& fileName, bool add) {
    if (!add) {
        // Remove the record whose (lower-cased) file name equals `fileName`, unless it has open handles.
        for (auto it = records_.begin(); it != records_.end(); ++it) {
            if (!it->second.records.empty()) continue;
            cRZString lower(it->second.fileName);
            lower.MakeLower();
            if (lower.str() == fileName.str()) {
                records_.erase(it);
                return true;
            }
        }
        return false;
    }
    cRZString name, extension;
    cRZFile::GetPathName(fileName, name, false);
    cRZFile::GetPathExtension(fileName, extension);
    if (extension.Strlen() != 0) extension.str().erase(0, 1);                    // (unconditional here)
    cGZPersistResourceKey key{};
    if (GetKeyFromFileName(name, extension, key) && records_.find(key) == records_.end()) {
        RecordData data;
        data.fileName = fileName;
        records_.emplace(key, std::move(data));
    }
    return false;                                                                 // sic: adding always answers false
}

bool cGZDBSegmentDirectoryFiles::DoFileChangeCheck() {
    if (!initialized_) return false;
    cRZDirectory directory(path_, false, false, false);
    std::list<cRZString> files;
    bool result = false;
    if (directory.ReadDirectoryFileEntriesIntoStringList(files) && !files.empty()) {
        const std::uint32_t known = EnumerateRecordFiles(nullptr, 0);
        int valid = 0;
        for (auto it = files.begin(); it != files.end();) {
            if (!IsFileNameValid(*it)) {
                it = files.erase(it);
            } else {
                it->MakeLower();
                ++valid;
                ++it;
            }
        }
        if (known != 0 && valid != 0) {
            std::vector<cRZString*> recorded(known);
            std::vector<cIGZString*> slots(known);
            for (std::uint32_t i = 0; i < known; ++i) slots[i] = recorded[i] = new cRZString();
            EnumerateRecordFiles(slots.data(), known);
            for (cRZString* s : recorded) s->MakeLower();

            std::uint32_t matched = 0;
            for (auto it = files.begin(); it != files.end();) {
                bool found = false;
                for (std::uint32_t i = 0; i < known && !found; ++i) {
                    if (recorded[i] && recorded[i]->str() == it->str()) {
                        delete recorded[i];
                        recorded[i] = nullptr;
                        it = files.erase(it);
                        ++matched;
                        found = true;
                    }
                }
                if (!found) ++it;
            }
            if (matched < known) {
                // records whose file disappeared
                for (cRZString* s : recorded)
                    if (s) AddFile(*s, false);
                result = true;
            }
            if (!files.empty()) {
                // files that are new
                result = true;
                for (const cRZString& file : files) AddFile(file, true);
            }
            for (cRZString* s : recorded) delete s;
        }
    }
    if (changeDetection_) {
        const std::int32_t seconds = random_.RandomSint32RangeUniform(static_cast<std::int32_t>(detectionInterval_),
                                                                      static_cast<std::int32_t>(detectionInterval_) * 2);
        timer_.StartOneShot(static_cast<std::uint32_t>(seconds) * 1000u,
                            static_cast<cIGZPersistDBSegmentDirectoryFiles*>(this));
    }
    return result;
}

// Registry callback for category "CN_DBSDF": entries are "<segment id hex>" = "<type hex>:<ext>[:filenameasinstanceid],...".
void cGZDBSegmentDirectoryFiles::CreateDBSegmentDirectoryFilesCallback(const cIGZString& key, const cIGZString& value,
                                                                       void* user) {
    auto* segment = static_cast<cGZDBSegmentDirectoryFiles*>(user);
    if (RZAtoUint32HexA(key.ToChar()) != segment->groupId_) return;
    cRZString rest(value);
    cRZString token;
    do {
        rest.SplitTokenAtDelimiter(',', token);
        token.Trim();
        if (token.Strlen() != 0) {
            token.MakeLower();
            cRZString typeText = token.SplitToken(":");
            const std::uint32_t resid = RZAtoUint32HexA(typeText.ToChar());
            const std::size_t at = token.str().find(":filenameasinstanceid");
            if (at != std::string::npos) {
                segment->SetAllowTextAsKey(true);
                token.str().erase(at);
            }
            segment->extensionMap_.AddRESIDtoExtensionAssociation(resid, token);
        }
        if (token.Strlen() == 0) break;
    } while (rest.Strlen() != 0);
}
