// cHitDebugInfo / cSourceFileDebugInfo — debug maps for Hit track files. Recovered from Sim City 4
// Deluxe Editionsub 1.2.1 (cHitDebugInfo vtable 0x100ab5c50, ctor @ 0x100484b40;
// cSourceFileDebugInfo ctor @ 0x100485a74).
//
// A ".HDB" file next to the hit file lists "SOURCEFILE <name>  {line,offset}" headers followed by
// "{line,offset}" lines. Each source file keeps (line, offset) pairs and the track data it maps
// into (data pointer + size).
//
// Preserved quirks: LineNumFromOffset keeps "seen an entry at or after / at or before" flags across
// the whole scan, so it may return the line of a later entry than the closest one; looking up a
// hit file name from an entry point checks only that the track block exists.
#pragma once
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <utility>

#include "audio/interfaces/cIHitDebugInfo.h"
#include "rz/cRZSystemService.h"

class cSourceFileDebugInfo {
public:
    cSourceFileDebugInfo(const std::string& name, const std::uint8_t* data, std::int32_t size)
        : name_(name), data_(data), size_(size) {}    // @ 0x100485a74

    const std::string& name() const { return name_; }
    const std::uint8_t* data() const { return data_; }
    std::int32_t size() const { return size_; }
    void AddLine(std::int32_t line, std::int32_t offset) { lines_.push_back({line, offset}); }
    const std::list<std::pair<std::int32_t, std::int32_t>>& lines() const { return lines_; }
    // @ 0x100485bcc: the line number for a data offset, 0 when not found.
    std::int32_t LineNumFromOffset(std::int32_t offset) const;

private:
    std::string name_;                                // +0
    const std::uint8_t* data_;                        // +0x28
    std::int32_t size_;                               // +0x30
    std::list<std::pair<std::int32_t, std::int32_t>> lines_;   // +0x38 (line, offset)
};

class cHitDebugInfo final : public cIHitDebugInfo, public cRZSystemService {
public:
    cHitDebugInfo() : cRZSystemService(kGZSID_cHitDebugInfo, 0) {}   // @ 0x100484b40
    ~cHitDebugInfo() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;     // 1, 0x7bba0cbd, 0x287fb697 @ 0x100484d46
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }   // @ 0x100485c20
    std::uint32_t Release() override { return cRZSystemService::Release(); } // @ 0x100485c2e

    bool Init() override { return true; }                            // @ 0x100484da4
    bool Shutdown() override { return true; }                        // @ 0x100484db4
    std::int32_t GetServicePriority() const override { return 0; }   // @ 0x100485c4a
    void AddSourceFile(cIGZString& fileName, const std::uint8_t* data, std::int32_t size) override;   // @ 0x100484dc4
    void AddSourceLine(std::int32_t line, std::int32_t offset) override;   // @ 0x100484ea4
    bool GetDataAddressFromFilenameAndLineNum(cIGZString& fileName, std::int32_t line, const std::uint8_t** out) override;   // @ 0x100484ef6
    bool GetFilenameAndLineNumFromDataAddress(const std::uint8_t* address, cIGZString& fileName, std::int32_t& line) override;   // @ 0x100484fec
    bool GetDataAddressFromEntryPoint(std::uint32_t entryPoint, const std::uint8_t** out) override;   // @ 0x100485082
    bool GetHitFilenameFromEntryPoint(std::uint32_t entryPoint, cIGZString& fileName) override;       // @ 0x100485146
    bool LoadDebugInfo(cIGZString& hitFileName, const std::uint8_t* data, std::int32_t size) override;   // @ 0x1004851fe
    bool LoadSymbolFile(cIGZString& fileName) override;              // @ 0x1004858b6

    std::size_t sourceFileCount() const { return files_.size(); }
    const cSourceFileDebugInfo* sourceFile(std::size_t index) const;

private:
    std::list<std::unique_ptr<cSourceFileDebugInfo>> files_;         // +0x30
    cSourceFileDebugInfo* current_ = nullptr;                        // +0x48
};
