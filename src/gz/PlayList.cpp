#include "gz/PlayList.h"

#include <cstdlib>
#include <cstring>
#include <memory>

#include "rz/StringConversion.h"
#include "rz/cRZFile.h"

namespace {
constexpr const char* kBlanks = " \t\r\n";
constexpr const char* kLineEnds = "\r\n";

// atoi on the token text (the original calls atoi on ToChar()).
std::uint32_t ParseLength(const cRZString& s) { return static_cast<std::uint32_t>(std::atoi(s.ToChar())); }

// Erases through the first '=' (FindChars result + 1; not found erases nothing).
void EraseThroughEquals(cRZString& s) {
    const std::size_t at = s.FindChars("=", 0, true);
    s.Erase(0, static_cast<std::uint32_t>(at + 1));
}
}  // namespace

bool cGZPlayList::FindNotOneOf(const cRZString& text, const cRZString& set, std::uint32_t& index) {
    const std::uint32_t size = text.Strlen(), setSize = set.Strlen();
    index = 0;
    while (index < size) {
        if (setSize == 0) return true;
        const char c = text.ToChar()[index];
        bool inSet = false;
        for (std::uint32_t i = 0; i < setSize; ++i) inSet = inSet || c == set.ToChar()[i];
        if (!inSet) return true;
        ++index;
    }
    return false;
}

bool cGZPlayList::FindOneOf(const cRZString& text, const cRZString& set, std::uint32_t& index) {
    const std::uint32_t size = text.Strlen(), setSize = set.Strlen();
    index = 0;
    while (index < size) {
        const char c = text.ToChar()[index];
        for (std::uint32_t i = 0; i < setSize; ++i)
            if (c == set.ToChar()[i]) return true;
        ++index;
    }
    return false;
}

bool cGZPlayList::GetLine(cRZString& text, cRZString& line) {
    const cRZString blanks(kBlanks), lineEnds(kLineEnds);
    std::uint32_t index = 0;
    if (!FindNotOneOf(text, blanks, index)) return false;
    if (index != 0) text.Erase(0, index);
    if (FindOneOf(text, lineEnds, index)) {
        line = text;
        line.Resize(index);
        text.Erase(0, index);
    } else {
        line = text;
        text.Erase(0, text.Strlen());   // deviation: the original leaves the text untouched here (PLAYLIST-2)
    }
    line.Trim();
    ConvertStringEncoding(line, -1, line);
    return true;
}

bool cGZPlayList::GrabSegment(cRZString& source, cRZString& segment, int offset) {
    const char* data = source.ToChar();
    const std::uint32_t size = source.Strlen();
    if (!data) return false;
    if (size < static_cast<std::uint32_t>(offset)) return false;
    std::uint32_t end = static_cast<std::uint32_t>(offset);
    if (offset < static_cast<int>(size))
        while (end < size && data[end] != '\\') ++end;
    const std::uint32_t length = end + 1;
    segment.Strncpy(data, length);
    source.Erase(0, length);
    return true;
}

bool cGZPlayList::IsSpec(cRZString& path, cRZString& segment) {
    if (path.BeginsWith("\\", 1)) return GrabSegment(path, segment, 2);
    if (path.Strlen() >= 2 && path.ToChar()[1] == ':') return GrabSegment(path, segment, 0);
    return false;
}

bool cGZPlayList::StripEndSegment(cRZString& path) {
    if (path.EndsWith("\\", 1)) path.Resize(path.Strlen() - 1);
    if (path.ToChar() == nullptr) return false;
    const std::uint32_t at = static_cast<std::uint32_t>(path.ReverseFindChars("\\", static_cast<std::size_t>(-1), true));
    if (at != 0 || path.ToChar()[0] == '\\') path.Resize(at + 1);   // not found (-1) empties the path
    return true;
}

void cGZPlayList::AddNextPathSegment(cRZString& path, cRZString& out) {
    cRZString segment;
    if (!GrabSegment(path, segment, 0)) return;
    if (!segment.BeginsWith(".", 1)) {
        out.AppendString(segment);
        return;
    }
    // "." keeps the directory, ".." goes up one, "..." two, ...
    std::uint32_t dots = 0;
    while (dots < segment.Strlen() && segment.ToChar()[dots] == '.') ++dots;
    for (std::uint32_t i = 1; i < dots; ++i) StripEndSegment(out);
}

bool cGZPlayList::MakeAbsolutePath(const cRZString& base, const cRZString& path, cRZString& out) {
    cRZString remaining(path);   // `out` may be `path`
    out = base;
    while (remaining.Strlen() != 0) {
        cRZString segment;
        if (IsSpec(remaining, segment)) out = segment;
        else AddNextPathSegment(remaining, out);
    }
    return true;
}

bool cGZPlayList::ParseExtM3UFormat() {
    for (;;) {
        cRZString line;
        if (!GetLine(text_, line)) return true;
        if (!line.BeginsWith("#EXTINF:", 8)) return false;
        line.SplitToken(":");
        const cRZString seconds = line.SplitToken(",");
        const std::uint32_t length = ParseLength(seconds);
        cRZString path;
        if (!GetLine(text_, path)) return false;
        MakeAbsolutePath(directory_, path, path);
        entries_.emplace_back(length, line, path);   // `line` now holds the title
    }
}

bool cGZPlayList::ParsePLSFormat() {
    cRZString line;
    if (!GetLine(text_, line)) return true;
    int count = 0;
    for (;;) {
        while (!line.BeginsWith("File", 4)) {
            if (line.BeginsWith("NumberOfEntries", 15)) {
                line.Erase(0, 16);
                return count == std::atoi(line.ToChar());
            }
            if (!GetLine(text_, line)) return false;
        }
        EraseThroughEquals(line);   // line is the file path now
        cRZString lookahead, title;
        if (!GetLine(text_, title)) return false;
        if (title.BeginsWith("Title", 5)) EraseThroughEquals(title);
        else lookahead = title;
        std::uint32_t length = 0;
        if (lookahead.Strlen() == 0) {
            cRZString lengthLine;
            if (!GetLine(text_, lengthLine)) return false;
            if (lengthLine.BeginsWith("Length", 6)) EraseThroughEquals(lengthLine);
            else lookahead = lengthLine;
            length = ParseLength(lengthLine);
        }
        while (lookahead.Strlen() == 0) {
            if (!GetLine(text_, lookahead)) return false;
            if (line.BeginsWith("File", 4)) break;   // quirk: tests the entry's path, not the new line
        }
        MakeAbsolutePath(directory_, line, line);
        entries_.emplace_back(length, title, line);
        line = lookahead;
        ++count;
    }
}

bool cGZPlayList::ParseM3UFormat() {
    for (;;) {
        cRZString line;
        if (!GetLine(text_, line)) return true;
        if (line.FindChars(":", 0, true) == 0 && line.FindChars("\\\\", 0, true) == 0) {
            line = cRZString((std::string(path_.ToChar()) + line.ToChar()).c_str());   // quirk: the "found" index is tested as a bool
        }
        cRZString title;
        cRZFile::GetPathName(line, title, false);
        MakeAbsolutePath(directory_, line, line);
        entries_.emplace_back(0, title, line);
    }
}

bool cGZPlayList::Open(const cIGZString& path) {
    path_ = cRZString(path);
    cRZFile::GetPathDirectory(path_, directory_);
    cRZFile file(path);
    if (!file.Open(1, 2, 1)) return false;
    const std::uint32_t length = file.Length();
    text_.Resize(length);
    // one spare byte: Strncpy measures the text with strlen, and the original reads the unterminated buffer (PLAYLIST-2)
    std::unique_ptr<char[]> buffer(new char[std::size_t(length) + 1]());
    const bool read = file.Read(buffer.get(), length);
    file.Close();
    if (!read) return false;
    for (std::uint32_t i = 0; i + 1 < length; ++i)
        if (buffer[i] == '\0') buffer[i] = ' ';
    text_.Strncpy(buffer.get(), length);
    const cRZString original(text_);
    cRZString firstLine;
    GetLine(text_, firstLine);
    if (firstLine.Strlen() == 7 && std::memcmp(firstLine.ToChar(), "#EXTM3U", 7) == 0) return ParseExtM3UFormat();
    if (firstLine.Strlen() == 10 && std::memcmp(firstLine.ToChar(), "[playlist]", 10) == 0) return ParsePLSFormat();
    text_ = original;   // plain m3u: the first line is an entry too
    ParseM3UFormat();
    return true;
}
