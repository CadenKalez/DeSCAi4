#include "sc4/AppResources.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "gz/GZCOMDirector.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistDBSegmentMultiPackedFiles.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZPersistResourceManager.h"
#include "rz/cRZFile.h"
#include "sc4/App.h"

namespace {
constexpr std::uint32_t kPersistResourceManagerService = 0x056b906e, kPersistResourceManagerIid = 0x656b8efc;
constexpr std::uint32_t kSegmentIid = 0x656b8f0c;                      // cIGZPersistDBSegment
constexpr std::uint32_t kPackedFileClass = 0x086d4a89, kMultiPackedFilesClass = 0x090f8619;

cIGZPersistResourceManager* ResourceManager(cRZAutoRefCount<cIGZPersistResourceManager>& holder) {
    cIGZFrameWork* framework = RZGetFramework();
    if (framework) framework->GetSystemService(kPersistResourceManagerService, kPersistResourceManagerIid, holder.AsOutParam());
    return holder.get();
}

// The segment object: from COM, else built here (the resource director supplies the same two classes).
cRZAutoRefCount<cIGZPersistDBSegment> CreateSegment(bool multiFile) {
    cRZAutoRefCount<cIGZPersistDBSegment> segment;
    cIGZCOM* com = GZCOM();
    if (com && com->CreateInstance(multiFile ? kMultiPackedFilesClass : kPackedFileClass, kSegmentIid, segment.AsOutParam()) && segment) return segment;
    if (multiFile) return cRZAutoRefCount<cIGZPersistDBSegment>(static_cast<cIGZPersistDBSegment*>(new cGZPersistDBSegmentMultiPackedFiles()));
    return cRZAutoRefCount<cIGZPersistDBSegment>(static_cast<cIGZPersistDBSegment*>(new cGZDBSegmentPackedFile()));
}

// the files of a directory matching the *.dat glob (names only, sorted as the file system lists them)
std::vector<std::string> DatFilesIn(const std::string& directory) {
    std::vector<std::string> names;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return names;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.size() > 4 && name.compare(name.size() - 4, 4, ".dat") == 0) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> AllFilesBelow(const std::string& directory) {
    std::vector<std::string> paths;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
        if (entry.is_regular_file()) paths.push_back(entry.path().string());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string Extension(const std::string& path) {
    std::string::size_type dot = path.rfind('.');
    std::string::size_type slash = path.rfind('/');
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : path.substr(dot);
}
}  // namespace

void SetupPackedFileSegment(const cRZString& path, bool multiFile, bool onlyDatabases) {
    cRZAutoRefCount<cIGZPersistDBSegment> segment = CreateSegment(multiFile);
    if (!segment) return;
    if (onlyDatabases && cGZDBSegmentPackedFile::IsDatabaseFile(path, true) == -1) return;
    if (segment->Init()) {
        if (segment->SetPath(path) && segment->Open(true, false)) {
            cRZAutoRefCount<cIGZPersistResourceManager> manager;
            if (ResourceManager(manager) && manager->RegisterDBSegment(*segment)) return;
        }
        segment->Shutdown();
    }
}

// Registers, in order: the *.dat files of the data directory and of its language folder, the sku data, then every
// database below the plugin directory and the user plugin directory (the directories are created when missing) and
// the directories themselves as multi-file segments. At least three segments must exist afterwards.
bool cSC4App::SetupResources() {
    if (GetDebugFunctionalityEnabled()) {
        LoadRegistry();
        if (startupHooks_.registerSegmentCategoryCallback) startupHooks_.registerSegmentCategoryCallback();
    }

    cRZString directory;
    GetDataDirectory(directory, -1);
    for (const std::string& name : DatFilesIn(directory.ToChar())) SetupPackedFileSegment(cRZString((std::string(directory.ToChar()) + name).c_str()), false, false);
    GetDataDirectory(directory, 0);
    for (const std::string& name : DatFilesIn(directory.ToChar())) SetupPackedFileSegment(cRZString((std::string(directory.ToChar()) + name).c_str()), false, false);

    GetSkuSpecificDirectory(directory);
    SetupPackedFileSegment(directory, true, false);

    auto pluginDirectory = [&](auto getter) {
        (this->*getter)(directory);
        std::error_code error;
        if (!std::filesystem::is_directory(directory.ToChar(), error)) std::filesystem::create_directory(directory.ToChar(), error);
        for (const std::string& file : AllFilesBelow(directory.ToChar())) {
            if (Extension(file) != ".dat") SetupPackedFileSegment(cRZString(file.c_str()), false, true);
        }
        (this->*getter)(directory);
        SetupPackedFileSegment(directory, true, false);
    };
    pluginDirectory(&cSC4App::GetPluginDirectory);
    pluginDirectory(&cSC4App::GetUserPluginDirectory);

    cRZAutoRefCount<cIGZPersistResourceManager> manager;
    if (!ResourceManager(manager)) return false;
    // resource type associations resolved through COM (no factory object): class id, resource type
    static constexpr std::uint32_t kObjectFactories[][2] = {
        {0x0a63df8c, 0xca63e2a3}, {0x00436eb4, 0x2026960b}, {0xc8696797, 0x686aa4b0}, {0xa2ffb5d3, 0x856ddbac},
        {0x3ab50e2a, 0x7ab50e44}, {0x3ab50e2a, 0x7ab50e45}, {0x69b6f01c, 0x29a5d1ec}, {0x69b6f01c, 0x09adcd75},
        {0xfad0f0b6, 0x5ad0e817}, {0x453429b3, 0x6534284a}, {0x053429c8, 0x05342861}, {0xa83479ea, 0xa83479d3},
        {0x496678fe, 0x296678f7}, {0xea5118b5, 0xea5118b0}, {0x42e411c2, 0xa2e3d533},
    };
    for (const auto& pair : kObjectFactories) manager->RegisterObjectFactory(pair[0], pair[1], nullptr);
    return manager->GetSegmentCount() > 2;
}
