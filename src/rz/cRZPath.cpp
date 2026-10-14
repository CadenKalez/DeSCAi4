#include "rz/cRZPath.h"

// _splitpath-style split: an optional drive ("X:"), the directory up to and including the last
// separator ('/' or '\\'), the file name up to the last '.', and the extension after it.
void cRZPath::Split(const std::string& path, std::string* drive, std::string* directory, std::string* name, std::string* extension) {
    std::size_t start = 0;
    if (path.size() >= 2 && path[1] == ':') {
        if (drive) *drive = path.substr(0, 2);
        start = 2;
    } else if (drive) {
        drive->clear();
    }
    const std::size_t lastSeparator = path.find_last_of("/\\");
    std::size_t nameStart = start;
    if (lastSeparator != std::string::npos && lastSeparator >= start) {
        if (directory) *directory = path.substr(start, lastSeparator + 1 - start);
        nameStart = lastSeparator + 1;
    } else if (directory) {
        directory->clear();
    }
    const std::string tail = path.substr(nameStart);
    const std::size_t dot = tail.find_last_of('.');
    if (dot != std::string::npos) {
        if (name) *name = tail.substr(0, dot);
        if (extension) *extension = tail.substr(dot + 1);
    } else {
        if (name) *name = tail;
        if (extension) extension->clear();
    }
}

std::string cRZPath::FixDirectory(const std::string& directory) {
    std::string result = directory;
    if (!result.empty()) {
        if (result.front() != '/') result.insert(result.begin(), '/');
        if (result.back() != '/') result.push_back('/');
    }
    return result;
}

std::string cRZPath::FixExtension(const std::string& extension) {
    std::string result = extension;
    if (!result.empty() && result.front() != '.') result.insert(result.begin(), '.');
    return result;
}

bool cRZPath::SetPath(const std::string& path) {
    Split(path, &drive_, &directory_, &fileName_, &extension_);
    return true;
}

std::string cRZPath::GetDirectory() const { return FixDirectory(directory_); }
std::string cRZPath::GetExtension() const { return FixExtension(extension_); }

std::string cRZPath::GetPath() const { return drive_ + GetDirectory() + fileName_ + GetExtension(); }
std::string cRZPath::GetDriveAndDirectory() const { return drive_ + GetDirectory(); }
std::string cRZPath::GetFileNameAndExtension() const { return fileName_ + GetExtension(); }

bool cRZPath::SetFileName(const std::string& name, bool splitInput) {
    if (name == fileName_) return true;
    if (splitInput) Split(name, nullptr, nullptr, &fileName_, nullptr);
    else fileName_ = name;
    return true;
}

bool cRZPath::SetExtension(const std::string& extension, bool splitInput) {
    if (extension == extension_) return true;
    if (splitInput) Split(extension, nullptr, nullptr, nullptr, &extension_);
    else extension_ = FixExtension(extension);
    return true;
}
