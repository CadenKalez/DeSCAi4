// cRZURL — see URL.h for the layout, the address of every member and the quirks that are kept.
//
// Every method below is a hand translation of the Ghidra decompilation exported with
// scripts/export_class.py rz '^cRZURL$' (work/rz/cRZURL.txt); where the decompiler lost a call
// argument the x86_64 disassembly in /tmp/sc4.asm was used to recover it (noted inline).
#include "rz/URL.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "rz/RZStringUtils.h"

namespace {

constexpr std::size_t kNpos = std::string::npos;

std::string& Str(cRZString& s) { return s.str(); }
const std::string& Str(const cRZString& s) { return s.str(); }

bool StartsWith(const std::string& text, const char* prefix, std::size_t length) {
    return text.size() >= length && std::memcmp(text.data(), prefix, length) == 0;
}

// isalnum/isxdigit with the C locale, on bytes (the original calls ___maskrune / __DefaultRuneLocale).
bool IsAlnum(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0; }
bool IsDigit(char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; }
bool IsXDigit(char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; }

}  // namespace

// --- construction / assignment -----------------------------------------------------------------

cRZURL::cRZURL() = default;   // @ 0x10023e7e0: memset-like zero-init of every member

// @ 0x10023e916: m_explicitSchemeOnly = true (the only place that flag is ever set in this build),
// m_type/m_port zero, m_fullURL copied, m_parsed stays false.
cRZURL::cRZURL(const cRZString& url) : m_explicitSchemeOnly(true), m_fullURL(url) {}

// @ 0x10023ea7a: copies the whole object except the cRZString reference counts.
cRZURL& cRZURL::operator=(const cRZURL& other) {
    if (this != &other) {
        m_explicitSchemeOnly = other.m_explicitSchemeOnly;
        m_parsed = other.m_parsed;
        m_type = other.m_type;
        m_fullURL = other.m_fullURL;
        m_scheme = other.m_scheme;
        m_username = other.m_username;
        m_password = other.m_password;
        m_server = other.m_server;
        m_absolutePath = other.m_absolutePath;
        m_parameters = other.m_parameters;
        m_query = other.m_query;
        m_fragment = other.m_fragment;
        m_port = other.m_port;
    }
    return *this;
}

cRZURL::~cRZURL() = default;   // @ 0x100569cbc: cRZString's release, nothing else

// --- getters -----------------------------------------------------------------------------------

// @ 0x10023eb58 / 0x10023ebee..0x10023efee: parse a copy of m_fullURL into *this when needed.  The
// copy (not m_fullURL itself) is consumed, so m_fullURL keeps the caller's text.
void cRZURL::EnsureParsed() const {
    if (!m_parsed) {
        cRZString copy(m_fullURL);
        const_cast<cRZURL*>(this)->ParseURL(copy);
    }
}

std::int32_t cRZURL::GetURLType() const {
    EnsureParsed();
    return m_type;
}

const cRZString& cRZURL::GetFullURL() const { return m_fullURL; }

const cRZString& cRZURL::GetScheme() const {
    EnsureParsed();
    return m_scheme;
}

const cRZString& cRZURL::GetUsername() const {
    EnsureParsed();
    return m_username;
}

const cRZString& cRZURL::GetPassword() const {
    EnsureParsed();
    return m_password;
}

const cRZString& cRZURL::GetServer() const {
    EnsureParsed();
    return m_server;
}

const cRZString& cRZURL::GetAbsolutePath() const {
    EnsureParsed();
    return m_absolutePath;
}

const cRZString& cRZURL::GetParameters() const {
    EnsureParsed();
    return m_parameters;
}

const cRZString& cRZURL::GetQuery() const {
    EnsureParsed();
    return m_query;
}

const cRZString& cRZURL::GetFragment() const {
    EnsureParsed();
    return m_fragment;
}

// @ 0x10023f082: no lazy parse in the original (three instructions: load +0x170 and return).
std::uint32_t cRZURL::GetPort() const { return m_port; }

// --- setters / relative resolution -------------------------------------------------------------

// @ 0x10023f098: `invalidateParse` maps to the original's bool argument; it only clears the cache flag.
void cRZURL::SetFullURL(const cRZString& url, bool invalidateParse) {
    if (invalidateParse) m_parsed = false;
    m_fullURL = url;
}

// @ 0x10023ff88: the cRZURL overload only looks at the base's full-URL string.
bool cRZURL::ChangeURL(const cRZString& url, const cRZURL* baseURL) {
    if (baseURL != nullptr && !Str(baseURL->GetFullURL()).empty()) {
        const cRZString& base = baseURL->GetFullURL();
        return ChangeURL(url, &base);
    }
    return ChangeURL(url, static_cast<const cRZString*>(nullptr));
}

// @ 0x10023f0b6: reset, then either (a) no/empty base: parse the text itself, (b) empty text: copy
// the base string, or (c) both present: resolve `url` against the parsed base and rebuild.
bool cRZURL::ChangeURL(const cRZString& url, const cRZString* baseURL) {
    m_parsed = false;
    m_type = 0;
    m_port = 0;
    // Quirk: the original clears only these five members here (0x10023f0e5..0x10023f146); username,
    // parameters, query and fragment are left for ParseURL to clear.
    Str(m_fullURL).clear();
    Str(m_scheme).clear();
    Str(m_password).clear();
    Str(m_server).clear();
    Str(m_absolutePath).clear();

    if (baseURL != nullptr && !Str(*baseURL).empty()) {
        if (!Str(url).empty()) {
            cRZString referenceText(url);
            cRZURL reference(referenceText);
            reference.ParseURL(referenceText);
            Str(reference.m_fullURL).clear();   // 0x10023f285: the reference keeps only its components

            cRZString baseText(*baseURL);
            cRZURL base(baseText);
            base.ParseURL(baseText);

            if (!Str(reference.GetScheme()).empty()) {
                // 0x10023f32c: a reference with its own scheme wins outright; the original merely
                // stores the raw text and leaves every component empty and unparsed.
                m_fullURL = url;
            } else {
                reference.m_scheme = base.GetScheme();
                if (Str(reference.GetServer()).empty()) reference.m_server = base.GetServer();
                if (Str(reference.GetUsername()).empty()) reference.m_username = base.GetUsername();
                if (Str(reference.GetPassword()).empty()) reference.m_password = base.GetPassword();
                if (reference.m_port == 0) reference.m_port = base.m_port;
                if (Str(reference.GetAbsolutePath()).empty()) {
                    // 0x10023f46e: empty reference path -> inherit path and also ;?#
                    reference.m_absolutePath = base.GetAbsolutePath();
                    if (Str(reference.GetParameters()).empty()) reference.m_parameters = base.GetParameters();
                    if (Str(reference.GetQuery()).empty()) reference.m_query = base.GetQuery();
                    if (Str(reference.GetFragment()).empty()) reference.m_fragment = base.GetFragment();
                } else if (Str(reference.GetAbsolutePath())[0] != '/') {
                    // 0x10023f4bb: relative path -> replace the base's last segment, then canonicalize.
                    cRZString merged(base.GetAbsolutePath());
                    const std::size_t slash = Str(merged).rfind('/');
                    if (slash != kNpos) Str(merged).erase(slash + 1);
                    Str(merged) += Str(reference.GetAbsolutePath());
                    CanonicalizePath(merged);
                    reference.m_absolutePath = merged;
                }
                // 0x10023f645: rebuild the text from the merged components and adopt the result.
                reference.MakeURLStringFromComponents();
                *this = reference;
            }
        } else {
            // 0x10023f30f: empty reference -> the base text is stored verbatim.
            m_fullURL = *baseURL;
        }
        return true;
    }

    // 0x10023f175: no base (or an empty one); parse a copy of the text into *this.
    cRZString parseText(url);
    m_fullURL = url;
    return ParseURL(parseText);
}

// --- top-level parse ---------------------------------------------------------------------------

// @ 0x10023f754
bool cRZURL::ParseURL(cRZString& url) {
    bool parsed = false;
    m_type = 0;
    Str(m_scheme).clear();
    Str(m_username).clear();
    Str(m_password).clear();
    Str(m_server).clear();
    Str(m_absolutePath).clear();
    Str(m_parameters).clear();
    Str(m_query).clear();
    Str(m_fragment).clear();
    m_port = 0;

    ParseScheme(url);
    switch (m_type) {
        case kTypeUnknownScheme:
            // 0x10023f833: "scheme:rest" keeps the rest as the absolute path.
            m_absolutePath = url;
            Str(url).clear();
            break;
        case kTypeHTTP:
        case kTypeHTTPS:
        case kTypeIRC:
            ParseUsernameAndPassword(url);   // falls through into the common HTTP path
            [[fallthrough]];
        case kTypeRelative:
            ParseServerAndPort(url);
            ParseURLHTTP(url);
            break;
        case kTypeFILE:
            ParseServerAndPort(url);
            ParseURLFILE(url);
            break;
        case kTypeFTP:
            ParseUsernameAndPassword(url);
            ParseServerAndPort(url);
            ParseURLFTP(url);
            break;
        case kTypeMailTo:
            // 0x10023f81a: the helper's result *is* the parsed flag (false for an empty address).
            parsed = ParseURLMailTo(url);
            m_parsed = parsed;
            return parsed;
        case kTypeNews:
            parsed = ParseURLNNTP(url);
            m_parsed = parsed;
            return parsed;
        default:
            // 0x10023f83f: type 0 leaves m_parsed false, so the next getter parses again.
            m_parsed = parsed;
            return parsed;
    }
    parsed = true;
    m_parsed = parsed;
    return parsed;
}

// --- scheme ------------------------------------------------------------------------------------

// @ 0x10023ffbc
bool cRZURL::ParseScheme(cRZString& url) {
    m_type = 0;
    Str(m_scheme).clear();
    std::string& text = Str(url);

    // 0x100240000..0x1002400d7: "URL:" / "url:" prefix (only those two spellings).
    if (StartsWith(text, "URL:", 4) || StartsWith(text, "url:", 4)) text.erase(0, 4);

    // 0x1002400db..0x100240843: "mailto:" prefix becomes the scheme but not the type; the original
    // returns here, leaving m_type 0 (quirk).
    if (StartsWith(text, "mailto:", 7)) {
        Str(m_scheme).assign(text, 0, 6);
        text.erase(0, 7);
        return true;
    }

    // 0x100240173..0x100240285: scheme = text before "://" (lower-cased), colon consumed.
    std::size_t schemeEnd = kNpos;
    for (std::size_t p = text.find(':'); p != kNpos && p + 2 < text.size(); p = text.find(':', p + 1)) {
        if (text[p + 1] == '/' && text[p + 2] == '/') {
            schemeEnd = p;
            break;
        }
    }
    if (schemeEnd != kNpos) {
        for (std::size_t i = 0; i < schemeEnd; ++i) {
            Str(m_scheme).push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
        }
        text.erase(0, schemeEnd + 1);
    }

    // 0x100240289..0x100240672: known schemes, prefix match only.  "http" is tested first and returns,
    // so "https" is reported as http (quirk); the kTypeHTTPS branch below is kept for faithfulness.
    const std::string& scheme = Str(m_scheme);
    if (!scheme.empty()) {
        if (StartsWith(scheme, "http", 4)) { m_type = kTypeHTTP; return true; }
        if (StartsWith(scheme, "https", 5)) { m_type = kTypeHTTPS; return true; }
        if (StartsWith(scheme, "file", 4)) { m_type = kTypeFILE; return true; }
        if (StartsWith(scheme, "ftp", 3)) { m_type = kTypeFTP; return true; }
        if (StartsWith(scheme, "mailto", 6)) { m_type = kTypeMailTo; return true; }
        if (StartsWith(scheme, "news", 4)) { m_type = kTypeNews; return true; }
        if (StartsWith(scheme, "nntp", 4)) { m_type = kTypeNews; return true; }
        if (StartsWith(scheme, "irc", 3)) { m_type = kTypeIRC; return true; }
    }

    if (m_type != 0) return true;

    if (!m_explicitSchemeOnly) {
        // 0x1002406ba..0x100240a35: guess a scheme from the text, then prepend "//" so that the
        // server/port parser sees the network-path form.
        if (StartsWith(text, "www.", 4)) m_type = kTypeHTTP;
        else if (StartsWith(text, "ftp.", 4)) m_type = kTypeFTP;
        else if (StartsWith(text, "news.", 5)) m_type = kTypeNews;
        else if (StartsWith(text, "nntp.", 5)) m_type = kTypeNews;
        else if (StartsWith(text, "irc.", 4)) m_type = kTypeIRC;
        text.insert(0, "//");
        return true;
    }

    // 0x10024068f..0x1002407ce: explicit "scheme:rest" (no "//") form.  m_type becomes 9 first, and
    // only a well-formed scheme (alnum/+/./-, before a non-zero ':' inside the text) upgrades it to 1.
    m_type = kTypeRelative;
    const std::size_t colon = text.find(':');
    if (colon != kNpos && colon != 0 && colon < text.size()) {
        bool valid = true;
        for (std::size_t i = 0; i < colon; ++i) {
            const char ch = text[i];
            if (!(IsAlnum(ch) || ch == '+' || ch == '.' || ch == '-')) {
                valid = false;
                break;
            }
        }
        if (!valid) return true;
        m_type = kTypeUnknownScheme;
        Str(m_scheme).assign(text, 0, colon);
        text.erase(0, colon + 1);
    }
    return true;
}

// --- authority / path --------------------------------------------------------------------------

// @ 0x100240a58
bool cRZURL::ParseUsernameAndPassword(cRZString& url) {
    Str(m_username).clear();
    Str(m_password).clear();
    std::string& text = Str(url);
    if (text.size() < 2) return true;
    const std::size_t authority = text.find("//");
    if (authority == kNpos || authority != 0) return true;
    const std::size_t at = text.find('@');
    if (at == kNpos) return true;
    // Quirk: the leading "//" is part of the username, and consuming through '@' removes it, so the
    // following ParseServerAndPort finds nothing and m_server stays empty for userinfo URLs.
    Str(m_username).assign(text, 0, at);
    text.erase(0, at + 1);
    const std::size_t colon = Str(m_username).find(':');
    if (colon == kNpos) return true;
    Str(m_password).assign(Str(m_username), colon + 1, kNpos);
    Str(m_username).erase(colon);
    return true;
}

// @ 0x100240c1a
bool cRZURL::ParseServerAndPort(cRZString& url) {
    Str(m_server).clear();
    m_port = 0;
    std::string& text = Str(url);
    if (text.size() < 2) return true;
    const std::size_t authority = text.find("//");
    if (authority == kNpos || authority != 0) return true;
    text.erase(0, 2);
    const std::size_t length = text.size();
    // 0x100240d16: file URLs stop here (which also makes ParseURLFILE's "localhost" check dead).
    if (m_type == kTypeFILE) return true;

    std::size_t colon = text.find(':');
    const std::size_t slash = text.find('/');
    if (slash < colon) colon = length;
    if (colon < length) {
        // 0x100240d6d..0x100240e69: decimal port digits; the original keeps them in a 16-byte stack
        // buffer, so a run longer than 15 digits overflows it.  The port read only the buffered
        // digits; here the buffer is clamped to 15 (documented deviation) while the consumed length
        // still counts every digit, exactly like the original's erase.
        char buffer[16] = {};
        std::size_t digits = 0;
        while (colon + 1 + digits < length) {
            const char ch = text[colon + 1 + digits];
            if (!IsDigit(ch)) break;
            if (digits < 15) buffer[digits] = ch;
            ++digits;
        }
        if (digits < 15) buffer[digits] = '\0';
        else buffer[15] = '\0';
        if (buffer[0] != '\0') m_port = static_cast<std::uint32_t>(std::atoi(buffer));
        Str(m_server).assign(text, 0, colon);
        text.erase(0, colon + 1 + digits);
    } else if (slash < length) {
        Str(m_server).assign(text, 0, slash);
        text.erase(0, slash);
    } else {
        // 0x100240de8: no ':' or '/' -> the whole remainder is the server, but for http/https one
        // trailing ';', '?' or '#' still terminates it.
        if ((m_type & ~1u) == kTypeHTTP) {
            const std::size_t semicolon = text.find(';');
            if (semicolon < length) {
                Str(m_server).assign(text, 0, semicolon);
                text.erase(0, semicolon);
            }
            if (Str(m_server).empty()) {
                const std::size_t question = text.find('?');
                if (question < length) {
                    Str(m_server).assign(text, 0, question);
                    text.erase(0, question);
                }
            }
            if (Str(m_server).empty()) {
                const std::size_t hash = text.find('#');
                if (hash < length) {
                    Str(m_server).assign(text, 0, hash);
                    text.erase(0, hash);
                }
            }
        }
        if (Str(m_server).empty()) {
            m_server = url;
            text.clear();
        }
    }
    return true;
}

// @ 0x100240ff6
bool cRZURL::ParseURLHTTP(cRZString& url) {
    Str(m_absolutePath).clear();
    Str(m_parameters).clear();
    Str(m_query).clear();
    Str(m_fragment).clear();
    std::string& text = Str(url);

    if (text.empty()) {
        // 0x100240df: an empty relative URL keeps an empty path; the others default to "/".
        if (m_type != kTypeRelative) m_absolutePath = "/";
        return true;
    }

    // 0x100241099..0x100241221: ';' parameters up to the next '?' or '#'.
    const std::size_t semicolon = text.find(';');
    const std::size_t firstQuestion = text.find('?');
    const std::size_t firstHash = text.find('#');
    if (semicolon < firstHash && semicolon < firstQuestion && semicolon < text.size()) {
        const std::size_t end = (firstQuestion < text.size())
                                    ? firstQuestion
                                    : ((firstHash < text.size()) ? firstHash : text.size());
        if (Str(m_absolutePath).empty()) Str(m_absolutePath).assign(text, 0, semicolon);
        Str(m_parameters).assign(text, semicolon, end - semicolon);
        text.erase(0, end);
    }

    // 0x100241221..0x100241315: '?' query up to the next '#'.
    const std::size_t question = text.find('?');
    const std::size_t hashAfterQuery = text.find('#');
    if (question < hashAfterQuery && question < text.size()) {
        const std::size_t end = (hashAfterQuery < text.size()) ? hashAfterQuery : text.size();
        if (Str(m_absolutePath).empty()) Str(m_absolutePath).assign(text, 0, question);
        Str(m_query).assign(text, question, end - question);
        text.erase(0, end);
    }

    // 0x100241315..0x1002413bf: '#fragment' to the end.
    const std::size_t hash = text.find('#');
    if (hash < text.size()) {
        if (Str(m_absolutePath).empty()) Str(m_absolutePath).assign(text, 0, hash);
        Str(m_fragment).assign(text, hash, text.size() - hash);
        text.clear();
    }

    // 0x1002413bf..0x1002413e7: whatever is left after all of the above is the path.
    if (Str(m_absolutePath).empty()) m_absolutePath = url;
    text.clear();
    return true;
}

// @ 0x100241424: file URLs keep everything after the authority as the path; the "localhost" check can
// never fire because ParseServerAndPort returns before setting m_server for kTypeFILE (quirk).
bool cRZURL::ParseURLFILE(cRZString& url) {
    if (Str(m_server).size() == 9 && Str(m_server) == "localhost") Str(m_server).clear();
    Str(m_parameters).clear();
    Str(m_query).clear();
    Str(m_fragment).clear();
    m_absolutePath = url;
    Str(url).clear();
    return true;
}

// @ 0x100241508
bool cRZURL::ParseURLFTP(cRZString& url) {
    Str(m_absolutePath).clear();
    Str(m_parameters).clear();
    Str(m_query).clear();
    Str(m_fragment).clear();
    std::string& text = Str(url);
    if (text.empty()) {
        // Quirk: the "/" is assigned to the working string and then cleared below, so an empty FTP
        // path stays empty instead of becoming "/".  The dead store is kept verbatim.
        text = "/";
    } else {
        const std::size_t semicolon = text.find(';');
        if (semicolon < text.size()) {
            Str(m_absolutePath).assign(text, 0, semicolon);
            Str(m_parameters).assign(text, semicolon, text.size() - semicolon);
        } else {
            m_absolutePath = url;
        }
    }
    text.clear();
    return true;
}

// @ 0x100241698
bool cRZURL::ParseURLMailTo(cRZString& url) {
    Str(m_absolutePath).clear();
    Str(m_parameters).clear();
    Str(m_query).clear();
    Str(m_fragment).clear();
    m_absolutePath = url;
    Str(url).clear();
    return !Str(m_absolutePath).empty();
}

// @ 0x100241748
bool cRZURL::ParseURLNNTP(cRZString& url) {
    Str(m_absolutePath).clear();
    Str(m_parameters).clear();
    Str(m_query).clear();
    Str(m_fragment).clear();
    m_absolutePath = url;
    Str(url).clear();
    return !Str(m_absolutePath).empty();
}

// --- building ----------------------------------------------------------------------------------

// @ 0x10023fcbe
bool cRZURL::MakeURLStringFromComponents() {
    if (!m_parsed) {
        cRZString copy(m_fullURL);
        ParseURL(copy);
    }
    std::string& out = Str(m_fullURL);
    out = Str(m_scheme);
    out.push_back(':');
    if (m_type != kTypeMailTo) out += "//";   // 0x10023fd59
    if (!Str(m_username).empty() || !Str(m_password).empty()) {
        out += Str(m_username);               // 0x10023fdc7
        if (!Str(m_password).empty()) {
            out.push_back(':');
            // Quirk (0x10023fdf6..0x10023fe16): the original appends the username's data/length a
            // second time; the password is never written.
            out += Str(m_username);
        }
        out.push_back('@');
    }
    out += Str(m_server);
    if (m_port != 0) {
        char buffer[16];
        std::snprintf(buffer, sizeof buffer, ":%u", m_port);
        out += buffer;
    }
    out += Str(m_absolutePath);
    out += Str(m_parameters);
    out += Str(m_query);
    out += Str(m_fragment);
    return true;
}

// --- path canonicalization ---------------------------------------------------------------------

// @ 0x10023f914.  Four passes, in the original order:
//   1. remove every "./" whose '.' is not itself preceded by '.' (restarting after the match);
//   2. remove one trailing '.' when it starts the string or follows '/';
//   3. remove "/seg/../" runs from the front, repeatedly;
//   4. remove one trailing "/..".
bool cRZURL::CanonicalizePath(cRZString& path) {
    std::string& text = Str(path);

    // Pass 1 (0x10023f925..0x10023fa42).
    std::size_t offset = 0;
    for (;;) {
        const std::size_t length = text.size();
        if (offset > length) break;
        std::size_t position = kNpos;
        for (std::size_t search = offset; search + 1 < length;) {
            const std::size_t dot = text.find('.', search);
            if (dot == kNpos || dot + 1 >= length) break;
            if (text[dot + 1] == '/') {
                position = dot;
                break;
            }
            search = dot + 1;
        }
        if (position == kNpos || position >= length) break;
        if (position != 0 && text[position - 1] == '.') {
            offset = position + 1;   // a later "./" may still be removed; "../" is left to pass 3
            continue;
        }
        text.erase(position, 2);
        offset = position + 1;
    }

    // Pass 2 (0x10023fa59..0x10023fae2).
    if (!text.empty() && text.back() == '.' && (text.size() == 1 || text[text.size() - 2] == '/')) {
        text.erase(text.size() - 1);
    }

    // Pass 3 (0x10023fae5..0x10023fbd4).
    while (text.size() > 3) {
        const std::size_t length = text.size();
        std::size_t position = kNpos;
        for (std::size_t search = 0; search + 3 < length;) {
            const std::size_t slash = text.find('/', search);
            if (slash == kNpos || slash + 3 >= length) break;
            if (text[slash + 1] == '.' && text[slash + 2] == '.' && text[slash + 3] == '/') {
                position = slash;
                break;
            }
            search = slash + 1;
        }
        if (position == kNpos) break;
        if (position == 0) {
            text.erase(0, 3);
        } else {
            std::size_t start = position;
            std::size_t index = position - 1;
            for (;;) {
                if (text[index] == '/') break;
                --index;
                --start;
                if (start == 0) {
                    start = 0;
                    break;
                }
            }
            text.erase(start, position + 4 - start);
        }
    }

    // Pass 4 (0x10023fbda..0x10023fcad).
    if (text.size() > 3) {
        const std::size_t marker = text.size() - 3;   // index of the '/' in a trailing "/.."
        if (text.rfind('/') == marker && text[text.size() - 2] == '.' && text[text.size() - 1] == '.') {
            std::size_t start = 0;
            if (marker != 0) {
                std::size_t index = marker - 1;
                std::size_t count = marker;
                for (;;) {
                    start = count;
                    if (text[index] == '/') break;
                    --index;
                    --count;
                    if (count == 0) {
                        start = 0;
                        break;
                    }
                }
            }
            text.erase(start, marker + 3 - start);
        }
    }
    return true;
}

// --- percent decoding --------------------------------------------------------------------------

// @ 0x1002417fa
bool cRZURL::ConvertPathFromURLToLocalForm(const cRZString& path, cRZString& out) {
    cRZString converted(path);
    ConvertPathFromEndodedForm(converted, converted);   // the original passes the same object twice
    out = converted;
    return true;
}

// @ 0x100241890
bool cRZURL::ConvertPathFromEndodedForm(const cRZString& encoded, cRZString& out) {
    cRZString converted;
    const std::string& text = Str(encoded);
    const std::size_t length = text.size();
    for (std::size_t i = 0; i < length;) {
        if (text[i] == '%' && i + 2 < length) {
            const char high = text[i + 1];
            const char low = text[i + 2];
            char value = ' ';   // quirk: an invalid escape becomes one space (0x100241969)
            if (IsXDigit(high) && IsXDigit(low)) {
                char digits[3] = {high, low, '\0'};
                value = static_cast<char>(RZAtoUint32HexA(digits));
            }
            Str(converted).push_back(value);
            i += 2;
        } else {
            Str(converted).push_back(text[i]);
        }
        ++i;
    }
    out = converted;
    return true;
}

// --- layout of the recovered object ------------------------------------------------------------

static_assert(sizeof(cRZString) == 0x28, "cRZString must stay 0x28 bytes to preserve the cRZURL layout");
static_assert(sizeof(cRZURL) >= 0x174, "cRZURL must hold the original 0x174 bytes of data");
