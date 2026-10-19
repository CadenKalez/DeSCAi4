#include "sc4/SCResParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "rz/RZConvert.h"
#include "rz/cRZString.h"
#include "rz/cRZTextFile.h"

namespace nSCRes {

namespace {
const std::string kUnprocessedSwitchChars = "-";
const std::string kSwitchChars = "-+";
const std::string kComment = "#";
const std::string kWhiteSpace = " \t\n\r";
const std::string kQuotes = "'\"";
const std::string kOpenBrackets = "[({";
const std::string kCloseBrackets = "])}";
const std::string kVariableStart = "$";
const std::string kIncludeFileToken = "include";
const std::string kSilentIncludeFileToken = "sinclude";
const std::string kSeparators = "/\\";
const std::string kStopChars = kWhiteSpace + kOpenBrackets;
const std::string kStopCharsPlusQuotes = kWhiteSpace + kOpenBrackets + kQuotes;
constexpr std::size_t npos = std::string::npos;

FileParserServices g_fileServices;

bool IsAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool IsDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool IsAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }
bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// "-x" where x is a letter, with the given set of leading characters.
bool LooksLikeSwitch(const std::string& s, const std::string& leading) {
    return s.size() > 1 && leading.find(s[0]) != npos && IsAlpha(s[1]);
}

// One tokenizer drives SplitIntoArguments and CountArguments (the original keeps two copies of the loop).
// `emit` receives the pieces of the current argument: begin(), then piece()s.
template <class Begin, class Piece>
std::size_t Tokenize(const std::string& s, Begin&& begin, Piece&& piece) {
    std::size_t limit = kComment.empty() ? 0 : s.find(kComment);
    if (kComment.empty()) limit = 0;
    std::size_t count = 0;
    if (limit == 0) return 0;
    std::size_t pos = 0;
    for (;;) {
        if (pos >= s.size()) break;
        while (pos < s.size() && kWhiteSpace.find(s[pos]) != npos) ++pos;
        if (pos >= s.size()) break;
        const std::size_t start = pos;
        if (limit <= start) break;
        begin();
        ++count;
        const std::size_t open = kOpenBrackets.find(s[start]);
        if (limit <= open) {
            // a plain argument: runs to white space / an opening bracket / a quote; quotes are stripped
            std::size_t p = start;
            for (;;) {
                std::size_t stop = p < s.size() ? s.find_first_of(kStopCharsPlusQuotes, p) : npos;
                const std::size_t end = stop == npos ? s.size() : stop;
                piece(s.substr(p, end - p));
                if (limit <= stop) return count;
                if (kStopChars.find(s[stop]) < limit) { pos = stop; break; }
                std::size_t at = stop;
                if (kQuotes.find(s[stop]) < limit) {
                    const std::size_t close = s.find(s[stop], stop + 1);
                    if (limit <= close) throw cParseError(5, "Missing end quote");
                    piece(s.substr(stop + 1, close - stop - 1));
                    at = close;
                }
                p = at + 1;
            }
        } else {
            // a bracket group: one argument holding what is between the (nested) brackets
            const char opener = s[start], closer = kCloseBrackets[open];
            std::size_t closeAt = start, openAt = start;
            do {
                closeAt = s.find(closer, closeAt + 1);
                openAt = s.find(opener, openAt + 1);
                if (limit <= closeAt) throw cParseError(6, "Missing end bracket");
            } while (openAt < closeAt);
            piece(s.substr(start + 1, closeAt - start - 1));
            pos = closeAt + 1;
        }
        if (limit <= pos) break;
    }
    return count;
}

// ---- the numeric expression evaluator (anonymous namespace in the original) -------------------------------------

double EvalExpression(const char*& p);

void SkipSpace(const char*& p) { while (IsSpace(*p)) ++p; }

void EvalCharConst(const char*& p, char expected) {
    SkipSpace(p);
    if (*p != expected) throw cParseError("Expected '%c'", expected);
    ++p;
}

double EvalParentheses(const char*& p) {
    EvalCharConst(p, '(');
    const double value = EvalExpression(p);
    EvalCharConst(p, ')');
    return value;
}

void EvalParentheses2(const char*& p, double& first, double& second) {
    EvalCharConst(p, '(');
    first = EvalExpression(p);
    EvalCharConst(p, ',');
    second = EvalExpression(p);
    EvalCharConst(p, ')');
}

constexpr float kDegreesToRadians = 0.017453292f, kRadiansToDegrees = 57.29578f;

double EvalSignedNumber(const char*& p) {
    for (;;) {
        SkipSpace(p);
        if (*p != '+') break;
        ++p;
    }
    if (*p == '-') {
        ++p;
        return -EvalSignedNumber(p);
    }
    SkipSpace(p);
    const char c = *p;
    if (!IsDigit(c)) {
        if (c == '(') return EvalParentheses(p);
        if (c != '.') {
            if (!IsAlpha(c)) throw cParseError("Bad numerical expression");
            const char* start = p;
            do { ++p; } while (IsAlnum(*p) || *p == '_');
            const std::string name(start, static_cast<std::size_t>(p - start));
            double a, b;
            if (name == "pi") return static_cast<double>(3.14159274101257f);
            if (name == "e") return 2.718281828459045;
            if (name == "sqrt") return std::sqrt(EvalParentheses(p));
            if (name == "exp") return std::exp(EvalParentheses(p));
            if (name == "log") return std::log(EvalParentheses(p));
            if (name == "abs") return std::fabs(EvalParentheses(p));
            if (name == "sin") return std::sin(EvalParentheses(p));
            if (name == "cos") return std::cos(EvalParentheses(p));
            if (name == "tan") return std::tan(EvalParentheses(p));
            if (name == "asin") return std::asin(EvalParentheses(p));
            if (name == "acos") return std::acos(EvalParentheses(p));
            if (name == "atan") return std::atan(EvalParentheses(p));
            if (name == "sind") return std::sin(static_cast<double>(static_cast<float>(EvalParentheses(p)) * kDegreesToRadians));
            if (name == "cosd") return std::cos(static_cast<double>(static_cast<float>(EvalParentheses(p)) * kDegreesToRadians));
            if (name == "tand") return std::tan(static_cast<double>(static_cast<float>(EvalParentheses(p)) * kDegreesToRadians));
            if (name == "dasin") return static_cast<double>(static_cast<float>(std::asin(EvalParentheses(p))) * kRadiansToDegrees);
            if (name == "dacos") return static_cast<double>(static_cast<float>(std::acos(EvalParentheses(p))) * kRadiansToDegrees);
            if (name == "datan") return static_cast<double>(static_cast<float>(std::atan(EvalParentheses(p))) * kRadiansToDegrees);
            if (name == "floor") return std::floor(EvalParentheses(p));
            if (name == "ceil") return std::ceil(EvalParentheses(p));
            if (name == "sqr") { const double v = EvalParentheses(p); return v * v; }
            if (name == "pow") { EvalParentheses2(p, a, b); return std::pow(a, b); }
            if (name == "atan2") { EvalParentheses2(p, a, b); return std::atan2(a, b); }
            if (name == "datan2") { EvalParentheses2(p, a, b); return static_cast<double>(static_cast<float>(std::atan2(a, b)) * kRadiansToDegrees); }
            throw cParseError("Unknown function '%s'", name.c_str());
        }
    }
    const char* end = p;
    double value;
    if (c == '0' && p[1] == 'x') {
        value = static_cast<double>(static_cast<std::int64_t>(std::strtoul(p, const_cast<char**>(&end), 0)));
    } else {
        value = StrToDEnglish(p, &end);
    }
    p = end;
    return value;
}

double EvalFactor(const char*& p) {
    double value = EvalSignedNumber(p);
    SkipSpace(p);
    if (*p == '^') {
        ++p;
        return std::pow(value, EvalFactor(p));
    }
    return value;
}

double EvalTerm(const char*& p) {
    double value = EvalFactor(p);
    for (;;) {
        SkipSpace(p);
        const char c = *p;
        if (c == '%') { ++p; const double d = EvalFactor(p); value = value - std::floor(value / d) * d; }
        else if (c == '/') { ++p; value /= EvalFactor(p); }
        else if (c == '*') { ++p; value *= EvalFactor(p); }
        else return value;
    }
}

double EvalExpression(const char*& p) {
    double value = EvalTerm(p);
    for (;;) {
        SkipSpace(p);
        if (*p == '-') { ++p; value -= EvalTerm(p); }
        else if (*p == '+') { ++p; value += EvalTerm(p); }
        else return value;
    }
}

double ParseExpression(const char* text, const char** end) {
    const char* p = text;
    if (*p == '\0') throw cParseError("empty expression");
    const double value = EvalExpression(p);
    if (*p != '\0') throw cParseError("Garbage at end of expression");
    if (end) *end = p;
    return value;
}

// $name / ${name} (variables) and %name (macro parameters) share one substitution loop.
// `lookup` answers the replacement or null; `unknown` is the error text format.
template <class Lookup>
void Substitute(std::string& argument, char marker, const char* unknownFormat, Lookup&& lookup) {
    std::string out;
    std::size_t at = 0;
    for (;;) {
        if (at >= argument.size()) break;
        const std::size_t d = argument.find(marker, at);
        out.append(argument, at, d == npos ? npos : d - at);
        if (d == npos) break;
        std::size_t nameStart = d + 1, flag = d;
        if (d + 1 < argument.size()) {
            if (argument[d + 1] == '{') { nameStart = d + 2; flag = d + 1; }
        }
        std::size_t k = 0;
        while (nameStart + k < argument.size()) {
            const char c = argument[nameStart + k];
            if (!(IsAlpha(c) || IsDigit(c) || c == '_' || c == ':')) break;
            ++k;
        }
        const std::string name = argument.substr(flag + 1, k + (nameStart - 1) - flag);
        const std::size_t nameEnd = nameStart + k;
        const std::string* value = lookup(name);
        if (!value) throw cParseError(unknownFormat, name.c_str());
        bool quote = false;
        if (flag == 0) {
            if (nameEnd != argument.size()) {
                if (kWhiteSpace.find(argument[nameEnd]) != npos) quote = true;
            }
        } else if (kWhiteSpace.find(argument[flag - 1]) != npos) {
            if (nameEnd == argument.size() || kWhiteSpace.find(argument[nameEnd]) != npos) quote = true;
        }
        if (quote) out += '"';
        out += *value;
        if (quote) out += '"';
        at = nameEnd + (nameEnd < argument.size() && argument[nameEnd] == '}' ? 1 : 0);
    }
    argument = out;
}
}  // namespace

// ---- cParseError ----------------------------------------------------------------------------------------------

cParseError::cParseError(std::uint32_t code, const char* format, ...) : code_(code) {
    static char buffer[0x800];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    message_ = buffer;
}

cParseError::cParseError(const char* format, ...) {
    static char buffer[0x800];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    message_ = buffer;
}

// ---- tokenising -----------------------------------------------------------------------------------------------

std::size_t SplitIntoArguments(Args& out, const std::string& line) {
    out.clear();
    return Tokenize(
        line, [&] { out.emplace_back(); }, [&](const std::string& piece) { out.back() += piece; });
}

int CountArguments(const std::string& line) {
    return static_cast<int>(Tokenize(line, [] {}, [](const std::string&) {}));
}

int FindSwitchIndex(const Args& args, const char* name) {
    const std::size_t length = std::strlen(name);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a.size() == length + 1 && kUnprocessedSwitchChars.find(a[0]) != npos && a.compare(1, npos, name) == 0)
            return static_cast<int>(i);
    }
    return -1;
}

void FindNonSwitchIndices(std::vector<int>& out, const Args& args, int minimum) {
    out.clear();
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (LooksLikeSwitch(args[i], kSwitchChars)) break;
        out.push_back(static_cast<int>(i));
    }
    if (static_cast<std::size_t>(minimum) > out.size()) throw cParseError("Expecting at least %d arguments", minimum);
}

bool FindSwitchArgumentIndices(std::vector<int>& out, Args& args, const char* name, int minimum) {
    const int index = FindSwitchIndex(args, name);
    out.clear();
    if (index < 0) return false;
    args[static_cast<std::size_t>(index)][0] = '+';                 // marks the switch as handled
    for (std::size_t i = static_cast<std::size_t>(index) + 1; i < args.size(); ++i) {
        if (LooksLikeSwitch(args[i], kSwitchChars)) break;
        out.push_back(static_cast<int>(i));
    }
    if (out.size() < static_cast<std::size_t>(minimum)) throw cParseError("Expecting at least %d arguments for -%s", minimum, name);
    return true;
}

bool UnhandledSwitchesArePresent(const Args& args) {
    for (std::size_t i = 1; i < args.size(); ++i)
        if (LooksLikeSwitch(args[i], kUnprocessedSwitchChars)) return true;
    return false;
}

bool FindUnhandledSwitchIndices(std::vector<int>& out, const Args& args) {
    out.clear();
    for (std::size_t i = 1; i < args.size(); ++i)
        if (LooksLikeSwitch(args[i], kUnprocessedSwitchChars)) out.push_back(static_cast<int>(i));
    return !out.empty();
}

// Splits `rest` at the first delimiter: the part before it is answered, the part after it stays in `rest`.
std::string NextToken(std::string& rest, const char* delimiters) {
    std::string token = rest;
    const std::string whole = rest;
    rest.clear();
    if (delimiters && *delimiters) {
        const std::size_t at = whole.find_first_of(delimiters);
        if (at != npos) {
            rest = whole.substr(at + 1);
            token = whole.substr(0, at);
        }
    }
    return token;
}

// ---- values ---------------------------------------------------------------------------------------------------

float ParseFloat(const std::string& text) {
    const char* end = nullptr;
    const double value = ParseExpression(text.c_str(), &end);
    if (end != text.c_str() + text.size()) throw cParseError("Bad real number");
    return static_cast<float>(value);
}

// The original rounds through the float magic number 1.5 * 2^23.
int ParseInt(const std::string& text) {
    const char* end = nullptr;
    const double value = ParseExpression(text.c_str(), &end);
    if (end != text.c_str() + text.size()) throw cParseError("Bad integer");
    const float biased = static_cast<float>(value) + 12582912.0f;
    std::int32_t bits;
    std::memcpy(&bits, &biased, sizeof bits);
    return bits - 0x4b400000;
}

std::int64_t ParseUint(const std::string& text) {
    const char* end = nullptr;
    const double value = ParseExpression(text.c_str(), &end);
    if (end != text.c_str() + text.size()) throw cParseError("Bad unsigned integer");
    return static_cast<std::int64_t>(value);
}

bool ParseBool(const std::string& text) {
    std::string lower = text;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "true" || lower == "on") return true;
    if (lower == "false" || lower == "off" || lower.empty()) return false;
    return ParseUint(text) != 0;
}

Color3 ParseColor(const std::string& text) {
    Args parts;
    SplitIntoArguments(parts, text);
    if (parts.size() != 3) throw cParseError("Wrong number of color components");
    return {ParseFloat(parts[0]), ParseFloat(parts[1]), ParseFloat(parts[2])};
}

Color4 ParseColorAlpha(const std::string& text) {
    Args parts;
    SplitIntoArguments(parts, text);
    if (parts.size() != 3 && parts.size() != 4) throw cParseError("Wrong number of color components");
    Color4 color{ParseFloat(parts[0]), ParseFloat(parts[1]), ParseFloat(parts[2]), 1.0f};
    if (parts.size() == 4) color.a = ParseFloat(parts[3]);
    return color;
}

Vector2 ParseVector2(const std::string& text) {
    Args parts;
    SplitIntoArguments(parts, text);
    if (parts.size() > 2) throw cParseError("Too many components for a Vector2");
    Vector2 v{0, 0};
    if (!parts.empty()) v.x = ParseFloat(parts[0]);
    if (parts.size() > 1) v.y = ParseFloat(parts[1]);
    return v;
}

Vector3 ParseVector3(const std::string& text) {
    Args parts;
    SplitIntoArguments(parts, text);
    if (parts.size() > 3) throw cParseError("Too many components for a Vector3");
    Vector3 v{0, 0, 0};
    if (!parts.empty()) v.x = ParseFloat(parts[0]);
    if (parts.size() > 1) v.y = ParseFloat(parts[1]);
    if (parts.size() > 2) v.z = ParseFloat(parts[2]);
    return v;
}

int ParseEnum(const std::string& text, const cEnumParseInfo* table, const cEnumParseInfo* extra) {
    for (const cEnumParseInfo* t : {table, extra})
        for (; t && t->name; ++t)
            if (text == t->name) return t->value;
    throw cParseError(4, "Unknown enum '%s'", text.c_str());
}

const char* EnumToString(int value, const cEnumParseInfo* table, const cEnumParseInfo* extra) {
    for (const cEnumParseInfo* t : {table, extra})
        for (; t && t->name; ++t)
            if (t->value == value) return t->name;
    return nullptr;
}

// Parses a decimal with '.' as the separator whatever the locale; `end` points past the number text.
double StrToDEnglish(const char* text, const char** end) {
    const double value = RZ::ConvertEnglishToDouble(text);
    const char* p = text;
    if (*p == '-' || *p == '+') ++p;
    for (;;) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c >= 0x80 || (c != '.' && !std::isdigit(c))) {
            if (c < 0x80 && (c | 0x20) == 'e') {
                ++p;
                if (*p == '+' || *p == '-') ++p;
                while (static_cast<unsigned char>(*p) < 0x80 && std::isdigit(static_cast<unsigned char>(*p))) ++p;
            }
            break;
        }
        ++p;
    }
    *end = p;
    return value;
}

void DToStrEnglish(double value, std::string& out) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    out = buffer;
    const std::size_t comma = out.find(',');
    if (comma != npos) out[comma] = '.';
}

std::string FindParentDir(const char* path) {
    std::string result = path;
    for (std::size_t n = result.size(); n != 0; --n) {
        if (kSeparators.find(result[n - 1]) != npos) {
            result.erase(n);
            return result;
        }
    }
    return std::string();
}

// ---- commands -------------------------------------------------------------------------------------------------

cBlockCommand::~cBlockCommand() {
    for (auto& entry : commands_) delete entry.second;
}

void cBlockCommand::StartBlock(cICommandParser* parser) { parser->State()->blockStack_.push_back(this); }

void cBlockCommand::RegisterCommand(const char* name, cSimpleCommand* command) {
    commands_[name] = command;
    command->RegisterCommands();
}

void cEndBlockCommand::Parse(Args&, cCommandParser* parser) {
    if (parser->blockStack_.empty()) throw cParseError("Not inside a block!");
    cBlockCommand* block = parser->blockStack_.back();
    block->EndBlock(parser);
    parser->blockStack_.pop_back();
}

void cVersionCommand::Parse(Args& args, cCommandParser* parser) {
    if (args.size() != 2) throw cParseError("wrong number of arguments");
    parser->version_ = std::atoi(args[1].c_str());
}

void cListCommandsCommand::Parse(Args&, cCommandParser*) { throw cParseError("unimplemented"); }

void cTraceCommand::Parse(Args& args, cCommandParser*) {
    if (args.size() != 2) throw cParseError("wrong number of arguments");
}

// set / seti / setf / setc / setv3 NAME VALUE: stores a variable; the typed forms normalise VALUE first.
// Quirk kept: for setc the first lane is treated as alpha ("-alpha" overwrites it and "-scale" skips it) and the
// color is printed as lanes 1, 2, 3 and then lane 0 when it is below 1, although ParseColorAlpha stores r, g, b, a.
void cSetCommand::Parse(Args& args, cCommandParser* parser) {
    std::vector<int> indices;
    if (args.size() < 3) throw cParseError("not enough arguments");
    std::string value = args[2];
    const std::string& kind = args[0];
    if (kind == "seti") {
        value = std::to_string(ParseInt(value));
    } else if (kind == "setf") {
        DToStrEnglish(static_cast<double>(ParseFloat(value)), value);
    } else if (kind == "setc") {
        const Color4 parsed = ParseColorAlpha(value);
        float lane[4] = {parsed.r, parsed.g, parsed.b, parsed.a};
        if (FindSwitchArgumentIndices(indices, args, "scale", 1)) {
            const float scale = ParseFloat(args[static_cast<std::size_t>(indices[0])]);
            lane[1] *= scale; lane[2] *= scale; lane[3] *= scale;
        }
        if (FindSwitchArgumentIndices(indices, args, "mult", 1)) {
            const Color4 m = ParseColorAlpha(args[static_cast<std::size_t>(indices[0])]);
            lane[0] *= m.r; lane[1] *= m.g; lane[2] *= m.b; lane[3] *= m.a;
        }
        if (FindSwitchArgumentIndices(indices, args, "add", 1)) {
            const Color4 m = ParseColorAlpha(args[static_cast<std::size_t>(indices[0])]);
            lane[0] += m.r; lane[1] += m.g; lane[2] += m.b; lane[3] += m.a;
        }
        if (FindSwitchArgumentIndices(indices, args, "alpha", 1)) lane[0] = ParseFloat(args[static_cast<std::size_t>(indices[0])]);
        std::string text, piece;
        for (int i = 1; i < 4; ++i) {
            if (i != 1) text += ' ';
            DToStrEnglish(static_cast<double>(lane[i]), piece);
            text += piece;
        }
        if (lane[0] < 1.0f) {
            text += ' ';
            DToStrEnglish(static_cast<double>(lane[0]), piece);
            text += piece;
        }
        value = text;
    } else if (kind == "setv3") {
        Vector3 v = ParseVector3(value);
        if (FindSwitchArgumentIndices(indices, args, "scale", 1)) {
            const float scale = ParseFloat(args[static_cast<std::size_t>(indices[0])]);
            v.x *= scale; v.y *= scale; v.z *= scale;
        }
        if (FindSwitchArgumentIndices(indices, args, "mult", 1)) {
            const Vector3 m = ParseVector3(args[static_cast<std::size_t>(indices[0])]);
            v.x *= m.x; v.y *= m.y; v.z *= m.z;
        }
        if (FindSwitchArgumentIndices(indices, args, "add", 1)) {
            const Vector3 m = ParseVector3(args[static_cast<std::size_t>(indices[0])]);
            v.x += m.x; v.y += m.y; v.z += m.z;
        }
        std::string piece;
        value.clear();
        DToStrEnglish(static_cast<double>(v.x), piece); value = piece + " ";
        DToStrEnglish(static_cast<double>(v.y), piece); value += piece + " ";
        DToStrEnglish(static_cast<double>(v.z), piece); value += piece;
    }
    parser->SetVariable(args[1].c_str(), value.c_str());
}

void cEvalCommand::Parse(Args& args, cCommandParser* parser) {
    if (args.size() != 2) throw cParseError("Wrong number of arguments");
    Args inner;
    SplitIntoArguments(inner, args[1]);
    parser->ParseCommandArguments(inner);
}

void cDefineCommand::Parse(Args& args, cCommandParser* parser) {
    if (parser->currentMacro_) throw cParseError("Can't nest definitions!");
    std::vector<int> indices;
    FindNonSwitchIndices(indices, args, 2);
    parser->currentMacro_ = new cCommandParser::cMacroDefinition;
    parser->currentMacro_->name = args[static_cast<std::size_t>(indices[0])];
    SplitIntoArguments(parser->currentMacro_->params, args[static_cast<std::size_t>(indices[1])]);
}

void cEndDefCommand::Parse(Args&, cCommandParser* parser) {
    if (!parser->currentMacro_) throw cParseError("Found enddef without matching define");
    parser->macroIndex_[parser->currentMacro_->name] = static_cast<int>(parser->macros_.size());
    parser->macros_.push_back(parser->currentMacro_);
    parser->currentMacro_ = nullptr;
}

// create NAME "values": replays the macro NAME with its %parameters% replaced.
void cCreateCommand::Parse(Args& args, cCommandParser* parser) {
    if (args.size() != 3) throw cParseError("wrong number of arguments");
    Args values;
    SplitIntoArguments(values, args[2]);
    const auto found = parser->macroIndex_.find(args[1]);
    if (found == parser->macroIndex_.end()) throw cParseError("Unknown definition: '%s'", args[1].c_str());
    cCommandParser::cMacroDefinition* macro = parser->macros_[static_cast<std::size_t>(found->second)];
    if (macro->params.size() != values.size()) throw cParseError("Wrong number of arguments");
    for (const Args& recorded : macro->body) {
        Args line(recorded.size());
        for (std::size_t j = 0; j < recorded.size(); ++j) {
            line[j] = recorded[j];
            macro->SubstituteParams(line[j], values);
        }
        parser->ParseCommandArguments(line);
    }
}

// arrayCreate NAME N: runs "create NAME i N" for i in [0, N).
void cArrayCreateCommand::Parse(Args& args, cCommandParser* parser) {
    if (args.size() != 3) throw cParseError("Wrong number of arguments");
    const std::uint32_t count = static_cast<std::uint32_t>(ParseInt(args[2]));
    Args line{"create", args[1], "0 0"};
    for (std::uint32_t i = 0; i < count; ++i) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%d %d", static_cast<int>(i), static_cast<int>(count));
        line[2] = buffer;
        parser->ParseCommandArguments(line);
    }
}

void cNamespaceCommand::Parse(Args& args, cCommandParser* parser) {
    if (args.size() != 2) throw cParseError("Expected (one) namespace name");
    StartBlock(parser);
    parser->PushNamespace(args[1].c_str());
}

void cNamespaceCommand::EndBlock(cCommandParser* parser) { parser->PopNamespace(); }

// ---- macros ---------------------------------------------------------------------------------------------------

void cCommandParser::cMacroDefinition::AddCommand(const Args& args) {
    if (!args.empty() && args[0] == "define") throw cParseError("Nested definitions not allowed");
    body.push_back(args);
}

void cCommandParser::cMacroDefinition::SubstituteParams(std::string& argument, const Args& values) const {
    Substitute(argument, '%', "Unknown parameter: '%s'", [&](const std::string& name) -> const std::string* {
        for (std::size_t i = 0; i < params.size() && i < values.size(); ++i)
            if (params[i] == name) return &values[i];
        return nullptr;
    });
}

// ---- cCommandParser -------------------------------------------------------------------------------------------

cCommandParser::cCommandParser() = default;

cCommandParser::~cCommandParser() {
    Shutdown();
    delete currentMacro_;
}

std::uint32_t cCommandParser::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cCommandParser::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid != 1 && iid != 0x095035f4) return false;
    *out = static_cast<cICommandParser*>(this);
    AddRef();
    return true;
}

bool cCommandParser::Init() {
    RegisterCommands();
    return true;
}

bool cCommandParser::Shutdown() {
    ClearMacros();
    commandIndex_.clear();
    for (cSimpleCommand* command : commands_) delete command;
    commands_.clear();
    return true;
}

void cCommandParser::ClearMacros() {
    macroIndex_.clear();
    for (cMacroDefinition* macro : macros_) delete macro;
    macros_.clear();
}

void cCommandParser::ParseBegin() {
    version_ = 0;
    namespace_.clear();
    namespaceLengths_.clear();
    ClearMacros();
}

void cCommandParser::ParseEnd() {
    delete currentMacro_;                 // the original just drops the pointer of an unfinished definition
    currentMacro_ = nullptr;
}

void cCommandParser::RegisterCommands() {
    RegisterCommand("end", new cEndBlockCommand);
    RegisterCommand("version", new cVersionCommand);
    RegisterCommand("list", new cListCommandsCommand);
    RegisterCommand("trace", new cTraceCommand);
    RegisterCommand("set", new cSetCommand);
    RegisterCommand("seti", new cSetCommand);
    RegisterCommand("setf", new cSetCommand);
    RegisterCommand("setc", new cSetCommand);
    RegisterCommand("setv3", new cSetCommand);
    RegisterCommand("eval", new cEvalCommand);
    RegisterCommand("define", new cDefineCommand);
    RegisterCommand("enddef", new cEndDefCommand);
    RegisterCommand("create", new cCreateCommand);
    RegisterCommand("arrayCreate", new cArrayCreateCommand);
    RegisterCommand("namespace", new cNamespaceCommand);
}

void cCommandParser::RegisterCommand(const char* name, cSimpleCommand* command) {
    commandIndex_[name] = static_cast<int>(commands_.size());
    commands_.push_back(command);
    command->RegisterCommands();
}

void cCommandParser::ParseCommand(const char* line) {
    Args args;
    SplitIntoArguments(args, line);
    if (!args.empty()) ParseCommandArguments(args);
}

void cCommandParser::ParseCommandArguments(Args& args) {
    if (currentMacro_ && !(args[0].size() == 6 && args[0] == "enddef")) {
        currentMacro_->AddCommand(args);        // while a definition is open every line (but enddef) is recorded
        return;
    }
    for (std::string& argument : args) SubstituteVariables(argument);
    for (std::size_t i = blockStack_.size(); i-- > 0;) {
        cBlockCommand* block = blockStack_[i];
        if (block->Commands().empty()) continue;
        const auto found = block->Commands().find(args[0]);
        if (found == block->Commands().end()) continue;
        found->second->Parse(args, this);
        if (!UnhandledSwitchesArePresent(args)) return;
        std::vector<int> unhandled;
        FindUnhandledSwitchIndices(unhandled, args);
        throw cParseError("Unexpected switch: %s", args[static_cast<std::size_t>(unhandled[0])].c_str());
    }
    const auto found = commandIndex_.find(args[0]);
    if (found == commandIndex_.end()) throw cParseError("Unknown command '%s'", args[0].c_str());
    commands_[static_cast<std::size_t>(found->second)]->Parse(args, this);
}

void cCommandParser::SubstituteVariables(std::string& argument) {
    Substitute(argument, kVariableStart[0], "Unknown variable: '%s'", [&](const std::string& name) -> const std::string* {
        static std::string holder;
        const char* value = Variable(name.c_str());
        if (!value) return nullptr;
        holder = value;
        return &holder;
    });
}

void cCommandParser::SetVariable(const char* name, const char* value) { variables_[namespace_ + name] = value; }

void cCommandParser::PushNamespace(const char* name) {
    namespaceLengths_.push_back(static_cast<int>(namespace_.size()));
    namespace_ += name;
    namespace_ += ':';
}

void cCommandParser::PopNamespace() {
    namespace_.erase(static_cast<std::size_t>(namespaceLengths_.back()));
    namespaceLengths_.pop_back();
}

void cCommandParser::PurgeNamespace(const char* prefix) {
    const std::string p = prefix;
    auto it = variables_.lower_bound(p);
    while (it != variables_.end() && it->first.compare(0, p.size(), p) == 0) it = variables_.erase(it);
}

// A name is looked up in the current namespace first, then in each enclosing one, finally globally.
const char* cCommandParser::Variable(const char* name) const {
    std::string prefix = namespace_;
    long level = static_cast<long>(namespaceLengths_.size()) - 1;
    std::map<std::string, std::string>::const_iterator found;
    for (;;) {
        found = variables_.find(prefix + name);
        if (level < 0 || found != variables_.end()) break;
        prefix.assign(namespace_, 0, static_cast<std::size_t>(namespaceLengths_[static_cast<std::size_t>(level)]));
        --level;
    }
    return found == variables_.end() ? nullptr : found->second.c_str();
}

// ---- cFileParser ----------------------------------------------------------------------------------------------

FileParserServices& GetFileParserServices() { return g_fileServices; }

cFileParser::~cFileParser() {
    if (parser_) parser_->Shutdown();
    if (parser_) parser_->Release();
}

std::uint32_t cFileParser::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cFileParser::Shutdown() {
    watchers_.clear();
    return true;
}

bool cFileParser::DoMessage(std::uint32_t type) {
    if (type == kMessageFilesChanged) modified_ = true;
    return type == kMessageFilesChanged;
}

void cFileParser::SetParser(cICommandParser* parser) {
    if (parser_) parser_->Shutdown();
    if (parser) parser->AddRef();
    if (parser_) parser_->Release();
    parser_ = parser;
    if (parser_) parser_->Init();
}

void cFileParser::AddInputFilePath(const char* path, std::uint16_t flags) {
    cInputFileSpec spec;
    spec.path = path;
    spec.directory = FindParentDir(path);
    spec.silent = (flags & 1) != 0;
    files_.push_back(spec);
    if ((flags & 2) && watchers_.find(spec.directory) == watchers_.end()) {
        if (g_fileServices.watchDirectory) g_fileServices.watchDirectory(spec.directory);
        watchers_[spec.directory] = true;
    }
    modified_ = true;
}

bool cFileParser::ParseFiles() { return ParseFiles(0, NumInputFiles()); }

bool cFileParser::ParseFiles(int first, int count) {
    if (!parser_ || !parser_->ReadyToParse()) {
        error_ = "Not ready to parse";
        return true;
    }
    modified_ = false;
    parser_->ParseBegin();
    int lines = 0;
    for (int i = 0; i < count; ++i) {
        const cInputFileSpec& spec = files_[static_cast<std::size_t>(first + i)];
        lines += DoParseFile(spec.path.c_str(), spec.directory.c_str());
    }
    parser_->ParseEndSuccess();
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "Successfully parsed %d lines", lines);
    error_ = buffer;
    parser_->ParseEnd();
    return true;
}

// Reads one file line by line. Errors raised while handling a line are re-thrown as "<message> at <file>:<line>".
int cFileParser::DoParseFile(const char* path, const char* directory) {
    cRZTextFile file(path);
    if (!file.Open(1, 2, 7)) throw cParseError(2, "Couldn't open file '%s'\n", path);
    std::string name = path;
    for (std::size_t n = name.size(); n != 0; --n)
        if (kSeparators.find(name[n - 1]) != npos) { name.erase(0, n); break; }

    int lines = 0, included = 0;
    bool inBlockComment = false;
    try {
        for (;;) {
            cRZString raw;
            if (!file.ReadLine(raw)) {
                file.Close();
                return lines + included;
            }
            ++lines;
            std::string line = raw.ToChar();
            // "#<" starts a block comment that runs to a "#>" (possibly on a later line)
            std::size_t hit = npos;
            for (std::size_t from = 0; from + 1 < line.size();) {
                const std::size_t q = line.find('#', from);
                if (q == npos || q + 1 >= line.size()) break;
                if (line[q + 1] == '<') { hit = q; break; }
                from = q + 1;
            }
            if (hit != npos) {
                line.erase(hit);
                inBlockComment = true;
            }
            if (inBlockComment) {
                std::size_t end = npos;
                for (std::size_t from = 0; from + 1 < line.size();) {
                    const std::size_t q = line.find('#', from);
                    if (q == npos || q + 1 >= line.size()) break;
                    if (line[q + 1] == '>') { end = q; break; }
                    from = q + 1;
                }
                if (end == npos) continue;
                inBlockComment = false;
                line.erase(0, end + 2);
            }

            Args args;
            SplitIntoArguments(args, line);
            const bool isInclude = !args.empty() && (args[0] == kIncludeFileToken || args[0] == kSilentIncludeFileToken);
            if (!isInclude) {
                parser_->ParseCommand(line.c_str());
                continue;
            }
            if (args.size() != 2) throw cParseError("\"%s\" takes a single argument", args[0].c_str());
            std::string target = args[1];
            const bool driveLetter = target.size() >= 2 && target[1] == ':' && IsAlpha(target[0]);
            const bool rooted = !target.empty() && kSeparators.find(target[0]) != npos;
            if (!driveLetter && !rooted) target.insert(0, directory);
            const std::string nested = FindParentDir(target.c_str());
            try {
                included += DoParseFile(target.c_str(), nested.c_str());
            } catch (const cParseError& error) {
                if (error.Code() != 2) throw;
                if (args[0] != kSilentIncludeFileToken) throw cParseError(3, "%s", error.What());
            }
        }
    } catch (const cParseError& error) {
        file.Close();
        throw cParseError(error.Code(), "%s at %s:%d", error.What(), name.c_str(), lines);
    }
}

}  // namespace nSCRes
