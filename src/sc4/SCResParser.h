// nSCRes — the SimCity script-resource text parser (the "SCRes" files under SCResFileParser.cpp /
// SCResCommandParser.cpp / SCResCommandParserSupport.cpp, 0x1005b97c2..0x100602872): the line oriented command language the
// effects, config-rule and similar text resources are written in.
//
// Language (as recovered):
//  * A line is split into arguments on white space; "text" and 'text' quotes are stripped, [..] (..) {..} bracket groups
//    become one argument, and '#' starts a comment. '#<' .. '#>' is a block comment across lines.
//  * "include file" / "sinclude file" read another file (relative paths resolve against the including file's folder; a
//    missing file is only an error for "include"). Every other line is a command.
//  * $name or ${name} in an argument is replaced by a variable (lookup walks out of the current namespace); a bare $name
//    surrounded by white space is quoted so its value stays one argument.
//  * Commands: end, version n, list (unimplemented), trace, set / seti / setf / setc / setv3, eval, define .. enddef,
//    create, arrayCreate, namespace (a block closed by "end"). "define name params" records the following lines as a macro,
//    "create name args" replays it with %param% substituted. Blocks (cBlockCommand) own sub-commands that are looked up
//    first, innermost block first; switches (-name) not consumed by a block command are an error.
//  * Numbers are expressions: + - * / % ^, parentheses, pi e and the functions sqrt exp log abs sin cos tan asin acos atan
//    sind cosd tand dasin dacos datan floor ceil sqr pow atan2 datan2 (angles in the d-forms are degrees, computed in float).
// Arguments are std::string here (the original uses cRZString). Errors are cParseError exceptions with a code (1 generic,
// 2 file not found, 3 wrapped open error, 4 unknown enum, 5 missing end quote, 6 missing end bracket).
#pragma once
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nSCRes {

class cParseError : public std::exception {
public:
    cParseError(std::uint32_t code, const char* format, ...) __attribute__((format(printf, 3, 4)));
    explicit cParseError(const char* format, ...) __attribute__((format(printf, 2, 3)));
    const char* what() const noexcept override { return message_.c_str(); }
    const char* What() const { return message_.c_str(); }
    std::uint32_t Code() const { return code_; }
    cParseError(std::uint32_t code, std::string message, int) : code_(code), message_(std::move(message)) {}
private:
    std::uint32_t code_ = 1;
    std::string message_;
};

using Args = std::vector<std::string>;

// ---- tokenising -----------------------------------------------------------------------------------------------
std::size_t SplitIntoArguments(Args& out, const std::string& line);       // @ 0x1005fd9eb
int CountArguments(const std::string& line);                              // @ 0x1005fe046
int FindSwitchIndex(const Args& args, const char* name);                  // @ 0x1005fe497
void FindNonSwitchIndices(std::vector<int>& out, const Args& args, int minimum);          // @ 0x1005fe61c
bool FindSwitchArgumentIndices(std::vector<int>& out, Args& args, const char* name, int minimum);   // @ 0x1005fe7b3
bool UnhandledSwitchesArePresent(const Args& args);                       // @ 0x1005fe9af
bool FindUnhandledSwitchIndices(std::vector<int>& out, const Args& args); // @ 0x1005feaba
std::string NextToken(std::string& rest, const char* delimiters);         // @ 0x1005fec15

// ---- values ---------------------------------------------------------------------------------------------------
struct Color3 { float r, g, b; };
struct Color4 { float r, g, b, a; };
struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct cEnumParseInfo { const char* name; int value; };

float ParseFloat(const std::string& text);                                // @ 0x1005fede6
int ParseInt(const std::string& text);                                    // @ 0x1005feef7
std::int64_t ParseUint(const std::string& text);                          // @ 0x1005fef93
bool ParseBool(const std::string& text);                                  // @ 0x1005ff01f
Color3 ParseColor(const std::string& text);                               // @ 0x1005ff223
Color4 ParseColorAlpha(const std::string& text);                          // @ 0x1005ff2f5
Vector2 ParseVector2(const std::string& text);                            // @ 0x1005ff40d
Vector3 ParseVector3(const std::string& text);                            // @ 0x1005ff4fd
int ParseEnum(const std::string& text, const cEnumParseInfo* table, const cEnumParseInfo* extra = nullptr);   // @ 0x1005ff62b
const char* EnumToString(int value, const cEnumParseInfo* table, const cEnumParseInfo* extra = nullptr);     // @ 0x1005ff77c
double StrToDEnglish(const char* text, const char** end);                 // @ 0x1005ff7c3
void DToStrEnglish(double value, std::string& out);                       // @ 0x1005ff84b

// ---- commands -------------------------------------------------------------------------------------------------
class cCommandParser;
class cBlockCommand;

// The parser as seen by commands (the original's virtual base).
class cICommandParser {
public:
    virtual ~cICommandParser() = default;
    virtual std::uint32_t AddRef() = 0;                        // [1]
    virtual std::uint32_t Release() = 0;                       // [2]
    virtual bool Init() = 0;                                   // [3]
    virtual bool Shutdown() = 0;                               // [4]
    virtual void ParseBegin() = 0;                             // [5]
    virtual void ParseEndSuccess() = 0;                        // [6]
    virtual void ParseEnd() = 0;                               // [7]
    virtual void ParseCommand(const char* line) = 0;           // [8]
    virtual void SetVariable(const char* name, const char* value) = 0;   // [9]
    virtual void PushNamespace(const char* name) = 0;          // [10]
    virtual void PopNamespace() = 0;                           // [11]
    virtual void PurgeNamespace(const char* prefix) = 0;       // [12]
    virtual cCommandParser* State() = 0;                       // [13]
    virtual bool ReadyToParse() const = 0;                     // [14]
    virtual const char* Variable(const char* name) const = 0;  // [15]
};

class cSimpleCommand {
public:
    virtual ~cSimpleCommand() = default;
    virtual void Parse(Args& args, cICommandParser* parser) = 0;
    virtual void RegisterCommands() {}                         // [3]: block commands register their sub-commands here
};

template <class T>
class cSimpleCommandT : public cSimpleCommand {
public:
    void Parse(Args& args, cICommandParser* parser) override { Parse(args, static_cast<T*>(parser->State())); }
    virtual void Parse(Args& args, T* parser) = 0;
};

class cBlockCommand : public cSimpleCommand {
public:
    cBlockCommand() = default;
    ~cBlockCommand() override;
    virtual void StartBlock(cICommandParser* parser);          // [4] @ 0x1005b98aa: pushes itself on the block stack
    virtual void EndBlock(cICommandParser*) {}                 // [5] @ 0x1005b999e
    void RegisterCommand(const char* name, cSimpleCommand* command);   // @ 0x1005b98ec
    const std::map<std::string, cSimpleCommand*>& Commands() const { return commands_; }
private:
    std::map<std::string, cSimpleCommand*> commands_;          // +8
};

template <class T>
class cBlockCommandT : public cBlockCommand {
public:
    void Parse(Args& args, cICommandParser* parser) override { Parse(args, static_cast<T*>(parser->State())); }
    void EndBlock(cICommandParser* parser) override { EndBlock(static_cast<T*>(parser->State())); }
    virtual void Parse(Args& args, T* parser) = 0;
    virtual void EndBlock(T*) {}
};

class cCommandParser : public cICommandParser {
public:
    class cMacroDefinition {
    public:
        void AddCommand(const Args& args);                                       // @ 0x1005b9f0e
        void SubstituteParams(std::string& argument, const Args& values) const;  // @ 0x1005bb7ec
        std::string name;                // +8
        Args params;                     // +0x28
        std::vector<Args> body;          // +0x40
    };

    cCommandParser();
    ~cCommandParser() override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool QueryInterface(std::uint32_t iid, void** out);

    bool Init() override;
    bool Shutdown() override;
    void ParseBegin() override;
    void ParseEndSuccess() override {}
    void ParseEnd() override;
    void ParseCommand(const char* line) override;
    void SetVariable(const char* name, const char* value) override;
    void PushNamespace(const char* name) override;
    void PopNamespace() override;
    void PurgeNamespace(const char* prefix) override;
    cCommandParser* State() override { return this; }
    bool ReadyToParse() const override { return true; }
    const char* Variable(const char* name) const override;

    virtual void RegisterCommands();                           // [18] @ 0x1005bb540
    void RegisterCommand(const char* name, cSimpleCommand* command);   // @ 0x1005bb77e
    void ParseCommandArguments(Args& args);                    // @ 0x1005b9c40
    void SubstituteVariables(std::string& argument);           // @ 0x1005b9fc6
    void ClearMacros();                                        // @ 0x1005ba672
    int Version() const { return version_; }

    // state shared with the commands
    std::vector<cBlockCommand*> blockStack_;                   // +0x40
    cMacroDefinition* currentMacro_ = nullptr;                 // +0xb0
    std::map<std::string, int> macroIndex_;                    // +0xb8
    std::vector<cMacroDefinition*> macros_;                    // +0xd0
    int version_ = 1;                                          // +0xc

private:
    std::uint32_t refCount_ = 0;                               // +8
    std::map<std::string, int> commandIndex_;                  // +0x10
    std::vector<cSimpleCommand*> commands_;                    // +0x28
    std::map<std::string, std::string> variables_;             // +0x58
    std::vector<int> namespaceLengths_;                        // +0x70
    std::string namespace_;                                    // +0x90
};

// The built-in commands.
class cEndBlockCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cVersionCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cListCommandsCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cTraceCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cSetCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cEvalCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cDefineCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cEndDefCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cCreateCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cArrayCreateCommand final : public cSimpleCommandT<cCommandParser> { public: void Parse(Args& a, cCommandParser* p) override; using cSimpleCommandT::Parse; };
class cNamespaceCommand final : public cBlockCommandT<cCommandParser> {
public:
    void Parse(Args& a, cCommandParser* p) override;           // @ 0x1005bd586
    void EndBlock(cCommandParser* p) override;                 // @ 0x1005bd61a
    using cBlockCommandT::Parse;
    using cBlockCommandT::EndBlock;
};

// ---- file parser ----------------------------------------------------------------------------------------------
struct FileParserServices {
    std::function<void(const std::string& path)> watchDirectory;   // creates a cIGZPersistDirectoryWatcher (0xe953da68) for the file
};
FileParserServices& GetFileParserServices();

class cFileParser {
public:
    static constexpr std::uint32_t kIID = 0x1cf94583, kMessageFilesChanged = 0x6ae76406;
    cFileParser() = default;
    ~cFileParser();
    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();
    bool Init() { return true; }
    bool Shutdown();
    bool DoMessage(std::uint32_t type);                        // @ 0x1006012a0
    void SetParser(cICommandParser* parser);                   // @ 0x1006012f4
    void AddInputFilePath(const char* path, std::uint16_t flags);   // @ 0x10060138e (bit 0: silent, bit 1: watch the folder)
    bool ParseFiles();                                         // @ 0x1006019c4
    bool ParseFiles(int first, int count);                     // @ 0x1006019e6
    int DoParseFile(const char* path, const char* directory);  // @ 0x100601d10: answers the number of lines read
    void SetParseFilesModified(bool modified) { modified_ = modified; }
    const char* ErrorString() const { return error_.c_str(); }
    cICommandParser* Parser() const { return parser_; }
    int NumInputFiles() const { return static_cast<int>(files_.size()); }
    const char* InputFilePath(int index) const { return files_[static_cast<std::size_t>(index)].path.c_str(); }
    bool ParseFilesModified() const { return modified_; }

    struct cInputFileSpec { std::string directory, path; bool silent = false; };

private:
    std::uint32_t refCount_ = 0;
    cICommandParser* parser_ = nullptr;                        // +0x20 (referenced)
    std::vector<cInputFileSpec> files_;                        // +0x28
    std::string error_;                                        // +0x40
    bool modified_ = false;                                    // +0x68
    std::map<std::string, bool> watchers_;                     // +0x70
};

std::string FindParentDir(const char* path);                   // @ 0x100601827 (anonymous namespace)

}  // namespace nSCRes
