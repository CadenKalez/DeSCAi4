// cIGZCommandServer — the framework's command registry (service 0xeb903a32, interface 0xcb90253a). Only the slots the
// command generators use are named; the others keep their position as placeholders.
#pragma once
#include <cstdint>

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

class cIGZCommandServer : public cIGZUnknown {
public:
    virtual void Slot3() {}
    virtual void Slot4() {}
    virtual void Slot5() {}
    virtual void Slot6() {}
    virtual void Slot7() {}
    virtual void Slot8() {}
    virtual void Slot9() {}
    virtual void Slot10() {}
    virtual bool GetCommandName(std::uint32_t id, cIGZString& name, std::uint32_t flags) = 0;      // [11] +0x58
    virtual std::uint32_t FindCommand(const cIGZString& name) = 0;                                  // [12] +0x60 (0 = unknown)
    virtual void Slot13() {}
    virtual void Slot14() {}
    virtual bool RegisterGenerator(cIGZCommandGenerator* generator) = 0;                            // [15] +0x78
    virtual bool UnregisterGenerator(cIGZCommandGenerator* generator) = 0;                          // [16] +0x80
    virtual void Slot17() {}
    virtual void Slot18() {}
    virtual std::uint32_t ExecuteCommand(std::uint32_t id, cIGZCommandParameterSet* input, cIGZCommandParameterSet* output) = 0;   // [19] +0x98
};
