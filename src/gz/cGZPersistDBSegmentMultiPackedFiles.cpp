#include "gz/cGZPersistDBSegmentMultiPackedFiles.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "gz/cGZPersistResourceKeyList.h"

namespace {

// Case-insensitive glob supporting '*' and '?' (the original cRZDirectory filter is a plain wildcard
// match on the file name).
bool WildcardMatch(const char* pattern, const char* text) {
    while (*pattern) {
        if (*pattern == '*') {
            while (*pattern == '*') ++pattern;
            if (!*pattern) return true;
            for (; *text; ++text)
                if (WildcardMatch(pattern, text)) return true;
            return false;
        }
        if (!*text) return false;
        if (*pattern != '?' && std::tolower(static_cast<unsigned char>(*pattern)) !=
                                   std::tolower(static_cast<unsigned char>(*text)))
            return false;
        ++pattern;
        ++text;
    }
    return *text == '\0';
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

cGZPersistDBSegmentMultiPackedFiles::cGZPersistDBSegmentMultiPackedFiles() = default;

bool cGZPersistDBSegmentMultiPackedFiles::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZPersistDBSegmentMultiPackedFiles) {
        *out = static_cast<cIGZPersistDBSegmentMultiPackedFiles*>(this);
        AddRef();
        return true;
    }
    return cGZPersistDBSegment::QueryInterface(iid, out);
}

bool cGZPersistDBSegmentMultiPackedFiles::Init() {
    if (!initialized_) initialized_ = true;
    return true;
}

bool cGZPersistDBSegmentMultiPackedFiles::Init(std::uint32_t segmentId, const cIGZString* path, bool) {
    // The original forwards to SetSegmentID/SetPath/Init through its own vtable.
    segmentId_ = segmentId;
    if (path) SetPath(*path);
    return Init() || true;
}

bool cGZPersistDBSegmentMultiPackedFiles::Shutdown() {
    if (open_) Close();
    initialized_ = false;
    return true;
}

bool cGZPersistDBSegmentMultiPackedFiles::SetPath(const cIGZString& path) {
    if (open_) return false;
    rootPath_.AssignFromChars(path.Data(), path.Strlen());
    return true;
}

bool cGZPersistDBSegmentMultiPackedFiles::Open(bool read, bool write) {
    if (!read || write) return false;
    if (rootPath_.Strlen() == 0) return false;

    std::vector<std::string> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(rootPath_.ToChar(), ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (WildcardMatch(filter_.ToChar(), it->path().filename().string().c_str()))
            files.push_back(it->path().string());
    }
    std::sort(files.begin(), files.end(),
              [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });

    bool any = false;
    for (const std::string& file : files) {
        auto* segment = new cGZDBSegmentPackedFile();
        segment->AddRef();
        cRZString path(file.c_str());
        segment->SetPath(path);
        std::uint32_t id = segmentId_;
        segment->SetSegmentID(id);
        if (segment->Init() && segment->Open(true, false)) {
            segment->AddRef();
            segments_.push_back(segment);
            auto* keys = new cGZPersistResourceKeyList();
            keys->AddRef();
            segment->GetResourceKeyList(keys, nullptr);
            for (std::uint32_t i = 0; i < keys->Size(); ++i) AddedResource(*keys->GetKey(i), segment);
            keys->Release();
            any = true;
        }
        segment->Release();
    }
    open_ = any;
    return any;
}

bool cGZPersistDBSegmentMultiPackedFiles::Close() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    for (cIGZPersistDBSegment* segment : segments_) {
        segment->Close();
        segment->Shutdown();
        segment->Release();
    }
    segments_.clear();
    resources_.clear();
    open_ = false;
    return true;
}

void cGZPersistDBSegmentMultiPackedFiles::AddedResource(const cGZPersistResourceKey& key,
                                                        cIGZPersistDBSegment* segment) {
    if (segment) resources_[key] = segment;   // the latest package to supply a key wins
}

cIGZPersistDBSegment* cGZPersistDBSegmentMultiPackedFiles::SegmentFor(const cGZPersistResourceKey& key) const {
    if (!open_) return nullptr;
    auto it = resources_.find(key);
    return it == resources_.end() ? nullptr : it->second;
}

bool cGZPersistDBSegmentMultiPackedFiles::DoTestForRecord(const cGZPersistResourceKey& key) {
    cIGZPersistDBSegment* s = SegmentFor(key);
    return s && s->TestForRecord(key);
}

std::uint32_t cGZPersistDBSegmentMultiPackedFiles::DoGetRecordSize(const cGZPersistResourceKey& key) {
    cIGZPersistDBSegment* s = SegmentFor(key);
    return s ? s->GetRecordSize(key) : 0;   // (sic) 0, not 0xffffffff, for unknown keys
}

bool cGZPersistDBSegmentMultiPackedFiles::DoOpenRecord(const cGZPersistResourceKey& key,
                                                       cIGZPersistDBRecord** record, std::uint32_t flags) {
    cIGZPersistDBSegment* s = SegmentFor(key);
    return s && s->OpenRecord(key, record, flags);
}

bool cGZPersistDBSegmentMultiPackedFiles::DoCloseRecord(cIGZPersistDBRecord* record) {
    if (!open_) return false;
    cIGZPersistDBSegment* s = SegmentFor(record->GetKey());
    return s && s->CloseRecord(record);
}

bool cGZPersistDBSegmentMultiPackedFiles::DoAbortRecord(cIGZPersistDBRecord* record) {
    if (!open_) return false;
    cIGZPersistDBSegment* s = SegmentFor(record->GetKey());
    return s && s->AbortRecord(record);
}

std::uint32_t cGZPersistDBSegmentMultiPackedFiles::DoReadRecord(const cGZPersistResourceKey& key, void* buffer,
                                                                std::uint32_t& size) {
    cIGZPersistDBSegment* s = SegmentFor(key);
    return s ? s->ReadRecord(key, buffer, size) : 0;
}

bool cGZPersistDBSegmentMultiPackedFiles::FindDBSegment(const cGZPersistResourceKey& key,
                                                        cIGZPersistDBSegment** segment) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    cIGZPersistDBSegment* s = SegmentFor(key);
    if (!s) return false;
    *segment = s;
    s->AddRef();
    return true;
}

std::uint32_t cGZPersistDBSegmentMultiPackedFiles::GetResourceKeyList(cIGZPersistResourceKeyList* list,
                                                                      cIGZPersistResourceKeyFilter* filter) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    std::uint32_t total = 0;
    for (cIGZPersistDBSegment* s : segments_) total += s->GetResourceKeyList(list, filter);
    return total;
}

int cGZPersistDBSegmentMultiPackedFiles::ConsolidateDatabaseRecords(cIGZPersistDBSegment* destination,
                                                                    cIGZPersistResourceKeyFilter* filter) {
    int copied = 0;
    for (std::uint32_t i = 0, n = GetSegmentCount(); i != n; ++i) {
        void* raw = nullptr;
        if (GetSegmentByIndex(i)->QueryInterface(kGZIID_cGZDBSegmentPackedFile, &raw)) {
            auto* packed = static_cast<cGZDBSegmentPackedFile*>(raw);
            copied += packed->CopyDatabaseRecords(destination, filter, false, true);
            packed->Release();
        }
    }
    return copied;
}

std::uint32_t cGZPersistDBSegmentMultiPackedFiles::ConsolidateDatabaseRecords(const cIGZString& path,
                                                                              cIGZPersistResourceKeyFilter* filter) {
    auto* target = new cGZDBSegmentPackedFile();
    target->AddRef();
    std::uint32_t result = 0xffffffffu;
    if (target->Init() && target->SetPath(path) && target->Open(true, true)) {
        result = static_cast<std::uint32_t>(
            ConsolidateDatabaseRecords(static_cast<cIGZPersistDBSegment*>(target), filter));
        target->Close();
    }
    target->Shutdown();
    target->Release();
    return result;
}
