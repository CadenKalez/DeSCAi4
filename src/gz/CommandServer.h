// cIGZCommandServer — the framework's command registry (service 0xeb903a32, interface 0xcb90253a). Only the slots the
// command generators use are named; the others keep their position as placeholders.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/CommandParameterSet.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"

constexpr std::uint32_t kGZSID_cGZCommandServer = 0xeb903a32;
constexpr std::uint32_t kGZIID_cIGZCommandServer = 0xcb90253a;
constexpr std::uint32_t kGZIID_cIGZCommandGenerator = 0x0b90251e;
constexpr std::uint32_t kGZIID_cIGZStringCommandGenerator = 0x0b90251f;

// A source of commands (a console, a script engine). The server tells it when commands come and go.
class cIGZCommandGenerator : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                        // [3] +0x18
    virtual bool Shutdown() = 0;                                                                    // [4] +0x20
    virtual bool CommandRegistered(std::uint32_t id, const cIGZString& name, const cIGZString& help) = 0;     // [5] +0x28
    virtual bool CommandUnregistered(std::uint32_t id, const cIGZString& name, const cIGZString& help) = 0;   // [6] +0x30
};

// A generator that turns text into commands and back (interface 0x0b90251f).
class cIGZStringCommandGenerator : public cIGZCommandGenerator {
public:
    // Parses and executes `text` (length 0xffffffff = NUL terminated); returns the command's status, 2 when the text is
    // not a known command.
    virtual std::uint32_t ExecuteCommand(const char* text, std::uint32_t length, cIGZCommandParameterSet* output) = 0;   // [9]
    virtual bool ConvertStringToCommand(const char* text, std::uint32_t length, std::uint32_t& id, cIGZCommandParameterSet*& params) = 0;   // [10]
    virtual bool ConvertCommandToString(std::uint32_t id, cIGZCommandParameterSet* params, cIGZString& out) = 0;                           // [11]
    virtual bool ConvertCommandOutputToString(cIGZCommandParameterSet* params, cIGZString& out) = 0;                                       // [12]
};

// What the server dispatches commands to (cGZMessage2CommandDispatcher is the standard one).
constexpr std::uint32_t kGZIID_cIGZCommandDispatcher = 0x0b903cc3;
class cIGZCommandDispatcher : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                                  // [3] +0x18
    virtual bool Shutdown() = 0;                                                                              // [4] +0x20
    virtual std::uint32_t ExecuteCommand(std::uint32_t id, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out) = 0;   // [5] +0x28
};

// Registration record the callers pass to RegisterCommands (stride 0x18).
struct cGZCommandInfoEntry {
    std::uint32_t id;
    const char* name;
    const char* help;
};

class cIGZCommandServer : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                        // [3] +0x18
    virtual bool Shutdown() = 0;                                                                    // [4] +0x20
    virtual void CreateCommandParameterSet(cIGZCommandParameterSet** out) = 0;                      // [5]
    virtual bool RegisterCommand(std::uint32_t id, const char* name, const char* help) = 0;         // [6]
    virtual bool RegisterCommands(const cGZCommandInfoEntry* entries, std::uint32_t count) = 0;     // [7]
    virtual bool RegisterCommandParameterInfo(std::uint32_t id, const char* text) = 0;              // [8]
    virtual bool UnregisterCommand(std::uint32_t id) = 0;                                           // [9]
    virtual bool UnregisterCommands(const cGZCommandInfoEntry* entries, std::uint32_t count) = 0;   // [10]
    virtual bool GetCommandInfo(std::uint32_t id, cIGZString& name, cIGZString* help) const = 0;    // [11] +0x58
    virtual std::uint32_t GetCommandID(const cIGZString& name) const = 0;                           // [12] +0x60 (0 = unknown)
    virtual std::uint32_t GetCommandIDFromCommandString(const char* text, std::uint32_t length) const = 0;   // [13]
    virtual void EnumerateCommands(std::vector<std::uint32_t>& ids) const = 0;                      // [14]
    virtual bool RegisterCommandGenerator(cIGZCommandGenerator* generator) = 0;                     // [15] +0x78
    virtual bool UnregisterCommandGenerator(cIGZCommandGenerator* generator) = 0;                   // [16] +0x80
    virtual cIGZCommandDispatcher* GetCommandDispatcher() const = 0;                                // [17]
    virtual bool SetCommandDispatcher(cIGZCommandDispatcher* dispatcher) = 0;                       // [18]
    virtual std::uint32_t ExecuteCommand(std::uint32_t id, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out) = 0;   // [19] +0x98
    virtual std::uint32_t ExecuteCommand(const char* text, std::uint32_t length, cIGZCommandParameterSet* out) = 0;           // [20]
    virtual std::uint32_t ExecuteCommandFormatted(const char* format, cIGZCommandParameterSet* out, ...) = 0;                 // [21]
    virtual bool ConvertStringToCommand(const char* text, std::uint32_t length, std::uint32_t& id, cIGZCommandParameterSet*& params) = 0;   // [22]
    virtual bool ConvertCommandToString(std::uint32_t id, cIGZCommandParameterSet* params, cIGZString& out) = 0;                           // [23]
    virtual bool ConvertCommandOutputToString(cIGZCommandParameterSet* params, cIGZString& out) = 0;                                       // [24]
};

// ---- the server and its standard message dispatcher ----
#include <cstdarg>
#include <map>
#include <set>

#include "gz/cIGZMessage2.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "rz/cRZSysServPtr.h"
#include "rz/cRZSystemService.h"

constexpr std::int32_t kGZCommandServerPriority = -1000000;

// The message the dispatcher sends (iids 0x0b903cc5 / 0x4b99446a): the command id is the type, and the input and output
// parameter sets ride in data 1 / 2 and are also held here (+0x58 / +0x60, 0x68 bytes).
class cGZMessage2CommandDispatcherMessage final : public cRZMessage2Standard {
public:
    ~cGZMessage2CommandDispatcherMessage() override;            // @ 0x1003dbdf2
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1003dbf0e
    void SetInput(cIGZCommandParameterSet* set);
    void SetOutput(cIGZCommandParameterSet* set);
    cIGZCommandParameterSet* Input() const { return input_; }
    cIGZCommandParameterSet* Output() const { return output_; }

private:
    cIGZCommandParameterSet* input_ = nullptr;    // +0x58
    cIGZCommandParameterSet* output_ = nullptr;   // +0x60
};

// Runs a command by posting a message synchronously to message server 2 (vtable 0x100aa7ef0, 0x38 bytes). Layout: +8
// refcount, +0x10 message server, +0x18 message, +0x20 / +0x28 scratch input / output sets, +0x30 string generator.
class cGZMessage2CommandDispatcher final : public cIGZCommandDispatcher {
public:
    virtual ~cGZMessage2CommandDispatcher();                     // @ 0x1003dbf54
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1003dc322 (ids 1, 0x0b903cc3, 0x0b903cc4)
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1003dc34c
    std::uint32_t Release() override;                              // @ 0x1003dc35a
    bool Init() override;                                          // @ 0x1003dc052
    bool Shutdown() override;                                      // @ 0x1003dc28e
    std::uint32_t ExecuteCommand(std::uint32_t id, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out) override;   // @ 0x1003dc376

private:
    std::uint32_t refCount_ = 0;
    cRZSysServPtr<cIGZMessageServer2, 0x04fa845b, 0x652294c7> server_;
    cGZMessage2CommandDispatcherMessage* message_ = nullptr;
    cIGZCommandParameterSet* input_ = nullptr;
    cIGZCommandParameterSet* output_ = nullptr;
    cIGZStringCommandGenerator* generator_ = nullptr;
};

class cGZCommandServer final : public cIGZCommandServer, public cRZSystemService {
public:
    cGZCommandServer() : cRZSystemService(kGZSID_cGZCommandServer, kGZCommandServerPriority) {}   // @ 0x1003dc4c8
    ~cGZCommandServer() override;                                  // @ 0x1003dc55a

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1003dc8c4 (ids 1, 0xcb90253a, 0xcb90253b)
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1003dc910 (own counter at +0x2c)
    std::uint32_t Release() override;                              // @ 0x1003dc93a
    bool Init() override;                                          // @ 0x1003dc6b6
    bool Shutdown() override;                                      // @ 0x1003dc7c4

    void CreateCommandParameterSet(cIGZCommandParameterSet** out) override;               // @ 0x1003dc9a2
    bool RegisterCommand(std::uint32_t id, const char* name, const char* help) override;  // @ 0x1003dcb1a
    bool RegisterCommands(const cGZCommandInfoEntry* entries, std::uint32_t count) override;   // @ 0x1003dce3c
    bool RegisterCommandParameterInfo(std::uint32_t id, const char* text) override;       // @ 0x1003dce8c
    bool UnregisterCommand(std::uint32_t id) override;                                    // @ 0x1003dd006
    bool UnregisterCommands(const cGZCommandInfoEntry* entries, std::uint32_t count) override;   // @ 0x1003dd06c
    bool GetCommandInfo(std::uint32_t id, cIGZString& name, cIGZString* help) const override;    // @ 0x1003dd0aa
    std::uint32_t GetCommandID(const cIGZString& name) const override;                    // @ 0x1003dd120
    std::uint32_t GetCommandIDFromCommandString(const char* text, std::uint32_t length) const override;   // @ 0x1003dd19c
    void EnumerateCommands(std::vector<std::uint32_t>& ids) const override;               // @ 0x1003dd28c
    bool RegisterCommandGenerator(cIGZCommandGenerator* generator) override;              // @ 0x1003dd322
    bool UnregisterCommandGenerator(cIGZCommandGenerator* generator) override;            // @ 0x1003dd48e
    cIGZCommandDispatcher* GetCommandDispatcher() const override { return dispatcher_; }   // @ 0x1003dd51e
    bool SetCommandDispatcher(cIGZCommandDispatcher* dispatcher) override;                // @ 0x1003dd528
    std::uint32_t ExecuteCommand(std::uint32_t id, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out) override;   // @ 0x1003dd56e
    std::uint32_t ExecuteCommand(const char* text, std::uint32_t length, cIGZCommandParameterSet* out) override;           // @ 0x1003dd58c
    std::uint32_t ExecuteCommandFormatted(const char* format, cIGZCommandParameterSet* out, ...) override;                 // @ 0x1003dd5aa
    bool ConvertStringToCommand(const char* text, std::uint32_t length, std::uint32_t& id, cIGZCommandParameterSet*& params) override;   // @ 0x1003dd69c
    bool ConvertCommandToString(std::uint32_t id, cIGZCommandParameterSet* params, cIGZString& out) override;               // @ 0x1003dd6b8
    bool ConvertCommandOutputToString(cIGZCommandParameterSet* params, cIGZString& out) override;                           // @ 0x1003dd6d4

    // Tells one generator (or every generator when null) about one command (or every command when null). @ 0x1003dc9e4
    struct CommandInfo {
        std::uint32_t id = 0;
        cRZString name;    // +8
        cRZString help;    // +0x30
    };
    bool NotifyCommandRegistration(const CommandInfo* info, bool registered, cIGZCommandGenerator* generator);
    // When set, commands registered without help text get "Format: <name>\nOutput: <none>" (flag at +0x78, never set here).
    void SetDefaultHelpFormat(bool on) { defaultHelp_ = on; }

private:
    std::uint32_t refCount_ = 0;                                   // +0x2c
    std::map<std::uint32_t, CommandInfo> commands_;                // +0x30
    cIGZCommandDispatcher* dispatcher_ = nullptr;                  // +0x48 (referenced)
    cIGZCommandDispatcher* ownDispatcher_ = nullptr;               // +0x50 (made by Init)
    cIGZStringCommandGenerator* generator_ = nullptr;              // +0x58
    std::set<cIGZCommandGenerator*> generators_;                   // +0x60 (each referenced)
    bool defaultHelp_ = false;                                     // +0x78
};
