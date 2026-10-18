#include "rz/cRZCmdLine.h"

#include <cctype>
#include <cstring>
#include <strings.h>

namespace {

bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\n'; }   // the mask 0x100000600

// "/-\" are the switch prefixes of GetIndexOfSwitch; IsSwitchPresent(char) only knows "-".
bool InSet(const char* set, char c) { return std::strchr(set, c) != nullptr && c != '\0'; }

void StripQuotePair(std::string& token) {
    if (!token.empty() && token.front() == '"' && token.back() == '"') {
        token.erase(token.size() - 1, 1);
        if (!token.empty()) token.erase(0, 1);
    }
}

std::string Lower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

}  // namespace

void cRZCmdLine::JoinArguments(const std::vector<cRZString>& arguments, cRZString& out) {   // 0x100013cd4
    std::string& text = out.str();
    text.erase();
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i].str();
        bool blank = false;
        for (char c : argument) blank = blank || IsBlank(c);
        if (blank) {
            text.push_back('"');
            text.append(argument);
            text.push_back('"');
        } else {
            text.append(argument);
        }
        if (i + 1 < arguments.size()) text.push_back(' ');
    }
}

void cRZCmdLine::SplitArguments(const cRZString& source, std::vector<cRZString>& out) {   // 0x100013f96
    std::string text = source.str();
    out.clear();
    while (!text.empty() && IsBlank(text.back())) text.pop_back();
    while (!text.empty() && IsBlank(text.front())) text.erase(0, 1);
    if (text.empty()) return;
    int start = 0, i = 0;
    const int size = static_cast<int>(text.size());
    std::uint8_t quotes = 0;
    auto push = [&](int from, int to) {
        std::string token = text.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
        StripQuotePair(token);
        out.emplace_back(token.c_str());
    };
    for (; i < size; ++i) {
        const char c = text[static_cast<std::size_t>(i)];
        if (IsBlank(c)) {
            if (!(quotes & 1)) {
                push(start, i);
                // skip the run of blanks that follows
                while (IsBlank(text[static_cast<std::size_t>(i) + 1])) ++i;   // the string's NUL ends the run
                start = i + 1;
                quotes = 0;
            }
        } else {
            quotes = static_cast<std::uint8_t>(quotes + (c == '"'));
        }
    }
    push(start, i);
}

cRZCmdLine::cRZCmdLine(const cIGZCmdLine& other) {   // 0x100013a30
    other.GetCommandLineText(text_);
    const int count = other.argc();
    for (int i = 0; i < count; ++i) arguments_.emplace_back(other.argv(i));
}

cRZCmdLine::cRZCmdLine(const char* text) : text_(text) {   // 0x100013e42
    SplitArguments(text_, arguments_);
}

bool cRZCmdLine::QueryInterface(std::uint32_t iid, void** out) {   // 0x10001453a
    if (iid != 1 && iid != kGZIID_cIGZCmdLine) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cRZCmdLine::Release() {   // 0x10001456a
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

const cRZString& cRZCmdLine::operator[](int index) const {   // 0x1000145a6
    return index >= argc() ? empty_ : arguments_[static_cast<std::size_t>(index)];
}

const char* cRZCmdLine::argv(int index) const {   // 0x1000145d6
    return index >= argc() ? empty_.ToChar() : arguments_[static_cast<std::size_t>(index)].ToChar();
}

bool cRZCmdLine::GetCommandLineText(cIGZString& out) const {   // 0x10001458a
    out.Copy(text_);
    return true;
}

int cRZCmdLine::Find(const cIGZString& argument, bool caseSensitive) const {   // 0x10001462e
    std::string wanted(argument.Data(), argument.Strlen());
    if (!caseSensitive) wanted = Lower(wanted);
    for (int i = 0; i < argc(); ++i) {
        const std::string& candidate = arguments_[static_cast<std::size_t>(i)].str();
        if ((caseSensitive ? candidate : Lower(candidate)) == wanted) return i;
    }
    return -1;
}

int cRZCmdLine::FindSubString(const cIGZString& part, bool caseSensitive) const {   // 0x1000148d2
    std::string wanted(part.Data(), part.Strlen());
    if (!caseSensitive) wanted = Lower(wanted);
    for (int i = 0; i < argc(); ++i) {
        const std::string& argument = arguments_[static_cast<std::size_t>(i)].str();
        const std::string candidate = caseSensitive ? argument : Lower(argument);
        // An empty needle sits at 0, which only counts for a non-empty argument.
        const std::size_t at = wanted.empty() ? 0 : candidate.find(wanted);
        if (at < argument.size()) return i;
    }
    return -1;
}

bool cRZCmdLine::IsSwitchPresent(char letter, cIGZString& value, bool stripQuotes) const {   // 0x100014d46
    const char lower = std::isupper(static_cast<unsigned char>(letter)) ? static_cast<char>(letter + 32) : letter;
    const char upper = std::islower(static_cast<unsigned char>(letter)) ? static_cast<char>(letter - 32) : letter;
    for (const cRZString& argument : arguments_) {
        const std::string& text = argument.str();
        if (text.size() < 2 || !InSet("-", text[0])) continue;
        if (text[1] != lower && text[1] != upper) continue;
        if (text.size() == 2) return true;   // a bare switch: the value is left alone
        std::string result;
        if (stripQuotes && text.size() >= 4 && text[2] == '"' && text.back() == '"') {
            if (text.size() != 4) result = text.substr(3, text.size() - 4);
        } else {
            result = text.substr(2);
        }
        cRZString holder(result.c_str());
        value.Copy(holder);
        return true;
    }
    return false;
}

int cRZCmdLine::GetIndexOfSwitch(char letter, int start) const {   // 0x100014ff6
    if (start < 0) start = 0;
    const char lower = std::isupper(static_cast<unsigned char>(letter)) ? static_cast<char>(letter + 32) : letter;
    const char upper = std::islower(static_cast<unsigned char>(letter)) ? static_cast<char>(letter - 32) : letter;
    for (int i = start; i < argc(); ++i) {
        const std::string& text = arguments_[static_cast<std::size_t>(i)].str();
        if (text.size() == 2 && InSet("/-\\", text[0]) && (text[1] == lower || text[1] == upper)) return i;
    }
    return -1;
}

bool cRZCmdLine::IsSwitchPresent(const cIGZString& name) const {   // 0x100014cb0
    cRZString value;
    if (GetIndexOfSwitch(name, 0) < 0) return IsSwitchPresent(name, value, true);
    return true;
}

bool cRZCmdLine::IsSwitchPresent(const cIGZString& name, cIGZString& value, bool stripQuotes) const {   // 0x100015162
    if (name.Strlen() == 0) return false;
    const std::string wanted = Lower(std::string(name.Data(), name.Strlen()));
    for (const cRZString& argument : arguments_) {
        const std::string& text = argument.str();
        if (text.empty() || !InSet("/-\\", text[0])) continue;
        if (text.size() < wanted.size() + 3 || text[1 + wanted.size()] != ':') continue;
        const std::string lowered = Lower(text);
        if (lowered.compare(1, wanted.size(), wanted) != 0) continue;
        std::string result = text.substr(wanted.size() + 2);
        if (stripQuotes && !result.empty() && (result.front() == '\'' || result.front() == '"')
            && (result.back() == '\'' || result.back() == '"')) {
            result.erase(0, 1);
            result.erase(result.size() - 1, 1);
        }
        cRZString holder(result.c_str());
        value.Copy(holder);
        return true;
    }
    return false;
}

int cRZCmdLine::GetIndexOfSwitch(const cIGZString& name, int start) const {   // 0x10001556c
    if (start < 0) start = 0;
    const std::string wanted = Lower(std::string(name.Data(), name.Strlen()));
    for (int i = start; i < argc(); ++i) {
        const std::string& text = arguments_[static_cast<std::size_t>(i)].str();
        if (text.size() < 2 || !InSet("/-\\", text[0])) continue;
        if (text.size() - 1 != wanted.size()) continue;
        if (Lower(text.substr(1)).find(wanted) == 0) return i;
    }
    return -1;
}

bool cRZCmdLine::InsertArgument(const cIGZString& argument, int index) {   // 0x100015968
    int at = index < argc() ? index : argc();
    if (argc() == 0 || index < 0) at = 0;
    arguments_.insert(arguments_.begin() + at, cRZString(argument.Data()));
    JoinArguments(arguments_, text_);
    return true;
}

bool cRZCmdLine::EraseArgument(int index) {   // 0x100015d86
    const int at = index < argc() ? index : argc() - 1;
    if (at >= 0 && at < argc()) arguments_.erase(arguments_.begin() + at);
    JoinArguments(arguments_, text_);
    return true;
}
