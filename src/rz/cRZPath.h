// cRZPath — splits a path into drive, directory, file name and extension and rebuilds it.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (ctor @ 0x1006a24a0, SetPath @ 0x1006a25d0,
// getters @ 0x1006a275a..0x1006a2cb0).
//
// Conventions of the original: GetDirectory() always has a leading and a trailing '/', and
// GetExtension() a leading '.' (both added when missing and only when non-empty). The original
// resolves relative paths against the file system service's current directory before splitting;
// the port keeps the path as given (callers pass absolute paths).
#pragma once
#include <string>

class cRZPath {
public:
    cRZPath() = default;
    explicit cRZPath(const std::string& path) { SetPath(path); }

    bool SetPath(const std::string& path);                      // @ 0x1006a25d0

    const std::string& GetDrive() const { return drive_; }      // @ 0x1006a275a
    std::string GetDirectory() const;                           // @ 0x1006a2790
    const std::string& GetFileName() const { return fileName_; }   // @ 0x1006a2830
    std::string GetExtension() const;                           // @ 0x1006a283a

    std::string GetPath() const;                                // @ 0x1006a28a2 (drive + directory + name + extension)
    std::string GetDriveAndDirectory() const;                   // @ 0x1006a2a10
    std::string GetFileNameAndExtension() const;                // @ 0x1006a2a74

    // @ 0x1006a2ad8 / 0x1006a2bb2: when `splitInput` is set the argument is itself a path and only
    // its file name (extension) part is taken.
    bool SetFileName(const std::string& name, bool splitInput);
    bool SetExtension(const std::string& extension, bool splitInput);

private:
    static void Split(const std::string& path, std::string* drive, std::string* directory, std::string* name, std::string* extension);
    static std::string FixDirectory(const std::string& directory);
    static std::string FixExtension(const std::string& extension);

    std::string drive_;
    std::string directory_;
    std::string fileName_;
    std::string extension_;
};
