// cRZCmdLine (23 functions, 0x100013a30..0x100015fc0, iid 0xa31e539c): the parsed command line. Arguments are kept as a
// vector of strings next to the text they were made from. Switches are written "-x", "/x" or "\x" (a single letter) or
// "-name:value".
//
// Quirks kept from the binary:
//  * The tokenizer splits on tab, line feed and space only (not carriage return) and strips one pair of enclosing
//    double quotes; the quote counter is not cleared by whitespace that sits inside quotes.
//  * IsSwitchPresent(char, ...) only accepts the "-" prefix while GetIndexOfSwitch(char, ...) accepts "/-\".
//  * IsSwitchPresent(char) answers true for a bare "-x" and leaves the value string untouched.
// EraseArgument on an empty list or with a negative index would corrupt memory in the original; here it only rebuilds the
// text.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

constexpr std::uint32_t kGZIID_cIGZCmdLine = 0xa31e539c;

class cIGZCmdLine : public cIGZUnknown {
public:
    virtual int argc() const = 0;                                                                // [3]
    virtual const char* argv(int index) const = 0;                                               // [4]
    virtual bool GetCommandLineText(cIGZString& out) const = 0;                                  // [5]
    virtual int Find(const cIGZString& argument, bool caseSensitive) const = 0;                  // [6]
    virtual int FindSubString(const cIGZString& part, bool caseSensitive) const = 0;             // [7]
    virtual bool IsSwitchPresent(char letter, cIGZString& value, bool stripQuotes) const = 0;    // [8]
    virtual int GetIndexOfSwitch(char letter, int start) const = 0;                              // [9]
    virtual bool IsSwitchPresent(const cIGZString& name) const = 0;                              // [10]
    virtual bool IsSwitchPresent(const cIGZString& name, cIGZString& value, bool stripQuotes) const = 0;   // [11]
    virtual int GetIndexOfSwitch(const cIGZString& name, int start) const = 0;                   // [12]
    virtual bool InsertArgument(const cIGZString& argument, int index) = 0;                      // [13]
    virtual bool EraseArgument(int index) = 0;                                                   // [14]
};

class cRZCmdLine final : public cIGZCmdLine {
public:
    explicit cRZCmdLine(const cIGZCmdLine& other);   // @ 0x100013a30
    explicit cRZCmdLine(const char* text);           // @ 0x100013e42
    virtual ~cRZCmdLine() = default;                 // [15] [16] @ 0x10002d6f0 / 0x100015fb6 / 0x100015fc0

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10001453a
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x10001455c
    std::uint32_t Release() override;                              // @ 0x10001456a
    int argc() const override { return static_cast<int>(arguments_.size()); }   // @ 0x100014616
    const char* argv(int index) const override;                    // @ 0x1000145d6
    bool GetCommandLineText(cIGZString& out) const override;       // @ 0x10001458a
    int Find(const cIGZString& argument, bool caseSensitive) const override;        // @ 0x10001462e
    int FindSubString(const cIGZString& part, bool caseSensitive) const override;   // @ 0x1000148d2
    bool IsSwitchPresent(char letter, cIGZString& value, bool stripQuotes) const override;   // @ 0x100014d46
    int GetIndexOfSwitch(char letter, int start) const override;   // @ 0x100014ff6
    bool IsSwitchPresent(const cIGZString& name) const override;   // @ 0x100014cb0
    bool IsSwitchPresent(const cIGZString& name, cIGZString& value, bool stripQuotes) const override;   // @ 0x100015162
    int GetIndexOfSwitch(const cIGZString& name, int start) const override;         // @ 0x10001556c
    bool InsertArgument(const cIGZString& argument, int index) override;            // @ 0x100015968
    bool EraseArgument(int index) override;                        // @ 0x100015d86
    // Argument `index` or an empty string when out of range. @ 0x1000145a6
    virtual const cRZString& operator[](int index) const;

    // The text -> arguments split and the arguments -> text join (the two ConvertStringArrayToString overloads).
    static void SplitArguments(const cRZString& text, std::vector<cRZString>& out);     // @ 0x100013f96
    static void JoinArguments(const std::vector<cRZString>& arguments, cRZString& out);   // @ 0x100013cd4

private:
    std::uint32_t refCount_ = 0;            // +8
    std::vector<cRZString> arguments_;      // +0x10
    cRZString text_;                        // +0x28
    cRZString empty_;                       // +0x50 returned for out-of-range indices
};
