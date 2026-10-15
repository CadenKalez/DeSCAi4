#include "rz/cRZIniFile.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "rz/cRZFile.h"

namespace {

std::string Lower(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

std::string LeftTrimmed(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    return text.substr(i);
}

std::string Trimmed(std::string text) {
    text = LeftTrimmed(text);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
    return text;
}

// `key=value` -> key (right-trimmed) and value (trimmed); a line without '=' has an empty key.
void SplitEntry(const std::string& line, std::string& key, std::string& value) {
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) {
        key.clear();
        value = line;
        return;
    }
    key = line.substr(0, eq);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    value = Trimmed(line.substr(eq + 1));
}

}  // namespace

cRZIniFile::cRZIniFile() : file_(new cRZFile()) { file_->AddRef(); }

cRZIniFile::cRZIniFile(const cRZString& path) : path_(path), file_(new cRZFile(path)) { file_->AddRef(); }

cRZIniFile::cRZIniFile(cIGZFile* file) : file_(file) {
    if (!file_) file_ = new cRZFile();
    file_->AddRef();
}

cRZIniFile::~cRZIniFile() {
    if (file_ && file_->IsOpen()) file_->Close();
    if (file_) {
        file_->Release();
        file_ = nullptr;
    }
}

void cRZIniFile::SetPath(const cRZString& path) {
    if (indexed_) Close();
    path_ = path;
    // port: keep the backing file pointing at the same path (the original callers always followed SetPath with a
    // file bound to that path)
    file_->SetPath(path);
}

bool cRZIniFile::SetFile(cIGZFile* file) {
    if (file_ == file) return true;
    Close();
    if (file) file->AddRef();
    if (file_) file_->Release();
    file_ = file;
    if (!file_) {
        file_ = new cRZFile();
        file_->AddRef();
        file_->SetPath(path_);
    }
    return true;
}

bool cRZIniFile::Open(bool forWrite) {
    const int access = (forWrite ? 1 : 0) * 2 + 1;
    if (file_->IsOpen()) {
        int a = 0, c = 0, s = 0;
        file_->GetOpenModes(a, c, s);
        if (a == access) return true;
        file_->Close();     // reopen in the requested mode
    }
    const auto start = std::chrono::steady_clock::now();
    do {
        if (file_->Open(access, forWrite ? 3 : 2, !forWrite)) {
            openedForWrite_ = forWrite;
            return true;
        }
        if (!file_->Exists()) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(250000));
    } while (std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
    return false;
}

bool cRZIniFile::Close() {
    if (file_->IsOpen()) file_->Close();
    sections_.clear();
    sectionSpelling_.clear();
    lines_.clear();
    indexed_ = false;
    return true;
}

void cRZIniFile::FinishOperation() {
    if (!leaveOpen_) Close();
}

// Reads the whole file, splits it into lines (runs of \r / \n collapse into one break) and indexes the
// `[section]` headers: lookup is by lower-case name, the value is the line after the header.
bool cRZIniFile::LoadSectionNames(bool forWrite) {
    sections_.clear();
    sectionSpelling_.clear();
    lines_.clear();
    if (!Open(forWrite)) return false;
    std::string text(file_->Length(), '\0');
    file_->SeekToBegin();
    if (!text.empty()) file_->Read(text.data(), static_cast<std::uint32_t>(text.size()));
    std::string current;
    for (char c : text) {
        if (c == '\r' || c == '\n') {
            if (!current.empty()) lines_.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) lines_.push_back(current);
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const std::string line = LeftTrimmed(lines_[i]);
        if (line.size() > 1 && line[0] == '[') {
            const std::size_t close = line.find(']');
            if (close != std::string::npos && close > 1) {
                const std::string name = line.substr(1, close - 1);
                const std::string lower = Lower(name);
                if (!sections_.count(lower)) {
                    sections_[lower] = i + 1;
                    sectionSpelling_[lower] = name;
                }
            }
        }
    }
    indexed_ = true;
    return true;
}

bool cRZIniFile::EnsureIndexed() { return indexed_ || LoadSectionNames(false); }

bool cRZIniFile::SectionExists(const cIGZString& section) {
    if (!EnsureIndexed()) {
        FinishOperation();
        return false;
    }
    const bool found = sections_.count(Lower(std::string(section.ToChar(), section.Strlen()))) != 0;
    FinishOperation();
    return found;
}

bool cRZIniFile::ReadEntry(const cRZString& section, const cRZString& key, cRZString& value) {
    if (!EnsureIndexed()) {
        FinishOperation();
        return false;
    }
    const auto it = sections_.find(Lower(section.str()));
    bool found = false;
    if (it != sections_.end()) {
        const std::string wanted = Lower(key.str());
        for (std::size_t i = it->second; i < lines_.size(); ++i) {
            const std::string line = LeftTrimmed(lines_[i]);
            if (line.empty() || line[0] == ';') continue;
            if (line[0] == '[') break;
            std::string k, v;
            SplitEntry(line, k, v);
            if (Lower(k) != wanted) continue;
            value.str() = v;
            found = true;
            break;
        }
    }
    FinishOperation();
    return found;
}

bool cRZIniFile::EnumEntries(const cRZString& section, EnumCallback callback, void* context) {
    if (!EnsureIndexed()) {
        FinishOperation();
        return false;
    }
    const auto it = sections_.find(Lower(section.str()));
    if (it == sections_.end()) {
        FinishOperation();
        return false;
    }
    // collected first: the callbacks may read the file themselves
    std::vector<std::pair<cRZString, cRZString>> entries;
    for (std::size_t i = it->second; i < lines_.size(); ++i) {
        const std::string line = LeftTrimmed(lines_[i]);
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') break;
        std::string k, v;
        SplitEntry(line, k, v);
        if (!k.empty() && !v.empty()) entries.emplace_back(cRZString(k.c_str()), cRZString(v.c_str()));
    }
    FinishOperation();
    for (auto& entry : entries) callback(entry.first, entry.second, context);
    return true;
}

// Calls back with every section's spelling from the file, in lower-case name order (the second argument is empty).
bool cRZIniFile::EnumSections(EnumCallback callback, void* context) {
    if (!EnsureIndexed()) {
        FinishOperation();
        return false;
    }
    std::vector<cRZString> names;
    for (const auto& section : sections_) {
        const auto spelling = sectionSpelling_.find(section.first);
        names.emplace_back(spelling == sectionSpelling_.end() ? section.first.c_str() : spelling->second.c_str());
    }
    FinishOperation();
    cRZString empty;
    for (const cRZString& name : names) callback(name, empty, context);
    return true;
}

bool cRZIniFile::Flush() {
    if (!Open(true)) return false;
    std::string text;
    for (const std::string& line : lines_) text += line + "\n";
    file_->SeekToBegin();
    file_->SetLength(0);
    if (!text.empty()) file_->Write(text.data(), static_cast<std::uint32_t>(text.size()));
    file_->Flush();
    return true;
}

// Replaces the value of an existing key (keeping its spelling), otherwise appends `key=value` to the end of the
// section; a missing section is appended to the file. Empty section or key names are refused.
bool cRZIniFile::WriteEntry(const cRZString& section, const cRZString& key, const cRZString& value) {
    if (section.Strlen() == 0 || key.Strlen() == 0) return false;
    if (!LoadSectionNames(true)) {
        FinishOperation();
        return false;
    }
    const std::string lowerSection = Lower(section.str());
    const std::string wanted = Lower(key.str());
    const std::string entry = key.str() + "=" + value.str();
    auto it = sections_.find(lowerSection);
    if (it == sections_.end()) {
        lines_.push_back("[" + section.str() + "]");
        lines_.push_back(entry);
    } else {
        std::size_t insertAt = lines_.size();
        bool replaced = false;
        std::size_t lastEntry = it->second;
        for (std::size_t i = it->second; i < lines_.size(); ++i) {
            const std::string line = LeftTrimmed(lines_[i]);
            if (line.empty()) continue;
            if (line[0] == '[') {
                insertAt = i;
                break;
            }
            if (line[0] == ';') {
                lastEntry = i + 1;
                continue;
            }
            std::string k, v;
            SplitEntry(line, k, v);
            lastEntry = i + 1;
            if (Lower(k) == wanted) {
                lines_[i] = k + "=" + value.str();
                replaced = true;
                break;
            }
        }
        if (!replaced) lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(std::min(lastEntry, insertAt)), entry);
    }
    const bool ok = Flush();
    Close();     // the index is stale after an edit; the next read rebuilds it
    return ok;
}

bool cRZIniFile::ReadNumber(const cRZString& section, const cRZString& key, int& value) {
    cRZString text;
    if (!ReadEntry(section, key, text)) return false;
    return std::sscanf(text.ToChar(), "%d", &value) == 1;
}

bool cRZIniFile::WriteNumber(const cRZString& section, const cRZString& key, int value) {
    if (section.Strlen() == 0 || key.Strlen() == 0) return false;
    cRZString text;
    text.Sprintf("%ld", static_cast<long>(value));
    return WriteEntry(section, key, text);
}

bool cRZIniFile::WriteBinaryEntry(const cRZString& section, const cRZString& key, const std::uint8_t* data,
                                  std::uint32_t size) {
    if (section.Strlen() == 0 || key.Strlen() == 0) return false;
    std::unique_ptr<char[]> hex(new char[size * 2 + 1]);
    ConvertBinaryDataToASCIIArray(data, size, hex.get());
    hex[size * 2] = '\0';
    return WriteEntry(section, key, cRZString(hex.get()));
}

std::uint32_t cRZIniFile::ReadBinaryEntry(const cRZString& section, const cRZString& key, std::uint8_t** data) {
    cRZString text;
    if (!ReadEntry(section, key, text)) return 0;
    const std::uint32_t length = text.Strlen();
    *data = new std::uint8_t[length >> 1];
    if (!ConvertASCIIArrayToBinaryData(text.ToChar(), length, *data)) {
        delete[] *data;
        *data = nullptr;
        return 0;
    }
    return length >> 1;
}

std::uint32_t cRZIniFile::ReadBinaryEntry(const cRZString& section, const cRZString& key, std::uint8_t* buffer,
                                          std::uint32_t capacity) {
    cRZString text;
    if (!ReadEntry(section, key, text)) return 0;
    std::uint32_t length = text.Strlen();
    if (capacity * 2 <= length) length = capacity * 2;
    return ConvertASCIIArrayToBinaryData(text.ToChar(), length, buffer) ? length >> 1 : 0;
}
