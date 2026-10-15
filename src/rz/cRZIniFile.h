// cRZIniFile — case-insensitive INI reader/writer over a cIGZFile (28 functions, 0x10002e1c0..0x10003144f).
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1.
//
// File model: lines separated by \r and/or \n (runs of line breaks collapse), `[section]` headers, `key=value`
// entries (key right-trimmed, value trimmed, ';' starts a comment line), section and key lookups ignore case.
// The original keeps the file open between operations (+0x30, default true) after indexing the section names
// (+0x38 lower-case name -> file position of the first line after the header, +0x50 lower-case -> original
// spelling); every operation indexes on first use and closes again when the flag is off. This port holds the
// file as a list of lines instead of file positions, which is observably the same for readers.
//
// Original layout: +0x08 path string, +0x30 leave-open flag, +0x31 indexed flag, +0x68 cIGZFile*, +0x70 the
// mode the file was opened in (write).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gz/cIGZFile.h"
#include "rz/RZStringUtils.h"
#include "rz/cRZString.h"

class cRZIniFile {
public:
    using EnumCallback = void (*)(const cRZString& key, const cRZString& value, void* context);

    cRZIniFile();
    explicit cRZIniFile(const cRZString& path);
    explicit cRZIniFile(cIGZFile* file);
    virtual ~cRZIniFile();
    cRZIniFile(const cRZIniFile&) = delete;
    cRZIniFile& operator=(const cRZIniFile&) = delete;

    const cRZString& GetPath() const { return path_; }
    void SetPath(const cRZString& path);
    cIGZFile* GetFile() const { return file_; }
    bool SetFile(cIGZFile* file);

    // Opens the underlying file for reading, or reading+writing; retries for up to three seconds while the file is
    // locked by someone else. Already open in the right mode counts as success.
    bool Open(bool forWrite);
    bool Close();
    void SetLeaveFileOpenBetweenOperations(bool leaveOpen) { leaveOpen_ = leaveOpen; }

    virtual bool ReadEntry(const cRZString& section, const cRZString& key, cRZString& value);
    virtual bool WriteEntry(const cRZString& section, const cRZString& key, const cRZString& value);
    virtual bool EnumEntries(const cRZString& section, EnumCallback callback, void* context);
    virtual bool EnumSections(EnumCallback callback, void* context);
    bool SectionExists(const cIGZString& section);

    bool ReadNumber(const cRZString& section, const cRZString& key, int& value);
    bool WriteNumber(const cRZString& section, const cRZString& key, int value);
    bool WriteBinaryEntry(const cRZString& section, const cRZString& key, const std::uint8_t* data, std::uint32_t size);
    // Allocates with new[]; returns the byte count (0 on failure).
    std::uint32_t ReadBinaryEntry(const cRZString& section, const cRZString& key, std::uint8_t** data);
    std::uint32_t ReadBinaryEntry(const cRZString& section, const cRZString& key, std::uint8_t* buffer, std::uint32_t capacity);

protected:
    bool EnsureIndexed();
    void FinishOperation();                    // closes again unless the file is left open
    bool LoadSectionNames(bool forWrite);
    bool Flush();                              // rewrites the file from lines_

    cRZString path_;
    cIGZFile* file_ = nullptr;
    bool leaveOpen_ = true;
    bool indexed_ = false;
    bool openedForWrite_ = false;
    std::vector<std::string> lines_;
    std::map<std::string, std::size_t> sections_;          // lower-case name -> index of first line after the header
    std::map<std::string, std::string> sectionSpelling_;   // lower-case name -> spelling in the file
};
