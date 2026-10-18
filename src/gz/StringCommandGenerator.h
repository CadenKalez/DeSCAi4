// cGZStringCommandGenerator — turns command lines ("name arg1 "arg two"") into commands for the command server and
// back (vtable 0x100aa7cf8; 13 functions at 0x1003db5f6..0x1003dbcdc). Layout: +8 refcount, +0x10 the command server
// (found by Init), +0x18 scratch vector of tokens, +0x30 a parameter set (made in the constructor, dropped by Shutdown).
//
// Parsing splits the line at blanks (quotes group), starting with room for one token and doubling the token vector until
// RZParseDelimitedText reports it fit (or 512 slots were tried). Token 0 names the command; the others become the
// parameters 0.. as string variants. Arguments that contain a blank or a tab are written back wrapped in quotes.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/CommandServer.h"
#include "rz/cRZString.h"
#include "rz/cRZSysServPtr.h"

class cGZStringCommandGenerator : public cIGZStringCommandGenerator {
public:
    cGZStringCommandGenerator();                                           // @ 0x1003db5f6
    virtual ~cGZStringCommandGenerator();                                  // @ 0x1003db69a

    bool QueryInterface(std::uint32_t iid, void** out) override;           // @ 0x1003db82a (ids 1, 0x0b90251e, 0x0b90251f)
    std::uint32_t AddRef() override { return ++refCount_; }                // @ 0x1003db854
    std::uint32_t Release() override;                                      // @ 0x1003db862

    bool Init() override;                                                  // @ 0x1003db746
    bool Shutdown() override;                                              // @ 0x1003db7ea
    bool CommandRegistered(std::uint32_t, const cIGZString&, const cIGZString&) override { return true; }     // @ 0x1003db882
    bool CommandUnregistered(std::uint32_t, const cIGZString&, const cIGZString&) override { return true; }   // @ 0x1003db88a

    std::uint32_t ExecuteCommand(const char* text, std::uint32_t length, cIGZCommandParameterSet* output) override;   // @ 0x1003db892
    bool ConvertStringToCommand(const char* text, std::uint32_t length, std::uint32_t& id, cIGZCommandParameterSet*& params) override;   // @ 0x1003db92e
    bool ConvertCommandToString(std::uint32_t id, cIGZCommandParameterSet* params, cIGZString& out) override;   // @ 0x1003dbae0
    bool ConvertCommandOutputToString(cIGZCommandParameterSet* params, cIGZString& out) override;               // @ 0x1003dbcdc

    // Copies `in` to `out` and wraps it in quotes when it contains a blank or tab (works in place).   @ 0x1003dbc48
    static void ConvertStringToArgumentString(const cIGZString& in, cIGZString& out);

private:
    std::uint32_t refCount_ = 0;                                                   // +8
    cRZSysServPtr<cIGZCommandServer, kGZSID_cGZCommandServer, kGZIID_cIGZCommandServer> server_;   // +0x10
    std::vector<cRZString> tokens_;                                                // +0x18
    cIGZCommandParameterSet* parameters_ = nullptr;                                // +0x30
};
