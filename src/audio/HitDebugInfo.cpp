#include "audio/HitDebugInfo.h"

#include <cstdio>

#include "audio/AudioServices.h"
#include "audio/HitMan.h"
#include "rz/cRZString.h"
#include "rz/cRZPath.h"

std::int32_t cSourceFileDebugInfo::LineNumFromOffset(std::int32_t offset) const {
    bool atOrAfter = false;
    bool atOrBefore = false;
    auto it = lines_.begin();
    for (; it != lines_.end(); ++it) {
        atOrAfter = atOrAfter || offset <= it->second;
        atOrBefore = atOrBefore || it->second <= offset;
        if (atOrAfter && atOrBefore) break;
    }
    return (atOrAfter && atOrBefore && it != lines_.end()) ? it->first : 0;
}

bool cHitDebugInfo::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIHitDebugInfo) {
        *out = static_cast<cIHitDebugInfo*>(this);
    } else if (iid == kGZIID_cIGZSystemService) {
        *out = static_cast<cIGZSystemService*>(this);
    } else {
        return cRZSystemService::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

const cSourceFileDebugInfo* cHitDebugInfo::sourceFile(std::size_t index) const {
    if (index >= files_.size()) return nullptr;
    auto it = files_.begin();
    std::advance(it, index);
    return it->get();
}

// @ 0x100484dc4: the new file becomes the target of AddSourceLine.
void cHitDebugInfo::AddSourceFile(cIGZString& fileName, const std::uint8_t* data, std::int32_t size) {
    files_.push_back(std::make_unique<cSourceFileDebugInfo>(std::string(fileName.Data(), fileName.Strlen()), data, size));
    current_ = files_.back().get();
}

void cHitDebugInfo::AddSourceLine(std::int32_t line, std::int32_t offset) {
    if (current_) current_->AddLine(line, offset);   // the original dereferences null before any file exists
}

bool cHitDebugInfo::GetDataAddressFromFilenameAndLineNum(cIGZString& fileName, std::int32_t line, const std::uint8_t** out) {
    const std::string wanted(fileName.Data(), fileName.Strlen());
    for (auto& file : files_) {
        if (file->name() != wanted) continue;
        for (const auto& entry : file->lines()) {
            if (entry.first == line) {
                if (entry.second != 0 && file->data() != nullptr) {
                    *out = file->data() + entry.second;
                    return true;
                }
                break;
            }
        }
    }
    return false;
}

bool cHitDebugInfo::GetFilenameAndLineNumFromDataAddress(const std::uint8_t* address, cIGZString& fileName, std::int32_t& line) {
    for (auto& file : files_) {
        const std::uint8_t* begin = file->data();
        if (begin <= address && address < begin + file->size()) {
            const std::int32_t found = file->LineNumFromOffset(static_cast<std::int32_t>(address - begin));
            if (found != 0) {
                fileName.AssignFromChars(file->name().data(), static_cast<std::uint32_t>(file->name().size()));
                line = found;
                return true;
            }
        }
    }
    return false;
}

bool cHitDebugInfo::GetDataAddressFromEntryPoint(std::uint32_t entryPoint, const std::uint8_t** out) {
    cIHitMan* hitMan = RZ::HitManInstance();
    if (!hitMan) return false;
    cIHitTrackDataBlock* block = nullptr;
    std::uint32_t offset = 0;
    if (!hitMan->GetTrackDataBlockAndOffset(entryPoint, &block, offset) || !block) return false;
    const std::uint8_t* data = block->DataStartAddress();
    if (!data) return false;
    *out = data + offset;
    return true;
}

bool cHitDebugInfo::GetHitFilenameFromEntryPoint(std::uint32_t entryPoint, cIGZString& fileName) {
    cIHitMan* hitMan = RZ::HitManInstance();
    if (!hitMan) return false;
    cIHitTrackDataBlock* block = nullptr;
    std::uint32_t offset = 0;
    if (!hitMan->GetTrackDataBlockAndOffset(entryPoint, &block, offset) || !block) return false;
    block->GetFilename(fileName);
    return true;
}

// @ 0x1004851fe: reads "<dir><name>.HDB".
bool cHitDebugInfo::LoadDebugInfo(cIGZString& hitFileName, const std::uint8_t* data, std::int32_t size) {
    cRZPath path{std::string(hitFileName.Data(), hitFileName.Strlen())};
    const std::string directory = path.GetDriveAndDirectory();
    const std::string debugPath = directory + path.GetFileName() + ".HDB";
    std::FILE* file = std::fopen(debugPath.c_str(), "rt");
    if (!file) return false;
    char line[0x400];
    while (std::fgets(line, sizeof(line), file)) {
        if (line[0] == '{') {
            int lineNumber = 0, offset = 0;
            if (std::sscanf(line, "{%d,%d}", &lineNumber, &offset) == 2) AddSourceLine(lineNumber, offset);
        } else if (line[0] == 'S') {
            char name[0x400];
            if (std::sscanf(line, "SOURCEFILE %s  {line,offset}", name) == 1) {
                const std::string full = directory + name;
                cRZString fullName(full.c_str());
                AddSourceFile(fullName, data, size);
            }
        }
    }
    std::fclose(file);
    return true;
}

// @ 0x1004858b6: parses "name count (" lines; the values are not kept.
bool cHitDebugInfo::LoadSymbolFile(cIGZString& fileName) {
    const std::string path(fileName.Data(), fileName.Strlen());
    std::FILE* file = std::fopen(path.c_str(), "rt");
    if (!file) return false;
    char line[0x400];
    while (std::fgets(line, sizeof(line), file)) {
        char symbol[0x400];
        int value = 0;
        std::sscanf(line, "%s %d (", symbol, &value);
    }
    std::fclose(file);
    return true;
}
