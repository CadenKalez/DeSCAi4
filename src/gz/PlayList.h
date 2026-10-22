// cGZPlayList — loads an audio playlist (.m3u, extended .m3u or .pls) into a list of entries
// (cGZPlayList 0x1004d1c80..0x1004d34e2, cGZPlayListEntry 0x1004d357a). Original layout: +8 entry vector (0x58-byte
// entries), +0x18 playlist path, +0x40 its directory, +0x68 the file text (consumed line by line).
//
// File text is split into lines by GetLine (leading blanks and line ends skipped, line ends " \t\r\n" / "\r\n"); relative
// entry paths are resolved against the playlist directory by MakeAbsolutePath, which understands "\\" separators, "." / ".."
// segments, drive ("C:") and "\\server" roots. Format detection is on the first line: "#EXTM3U", "[playlist]" or plain m3u.
//
// Quirks kept (see PLAYLIST-1): the PLS parser only adds an entry once it has read one more line, so the last entry is
// dropped unless a line (normally NumberOfEntries) follows it; the plain-m3u "prefix with the playlist path" test runs only
// when ':' and "\\" are both found at index 0 (it treats the found index as a bool); the extended parser stops at the first
// line that is not "#EXTINF:...". Deviation: when the text ends without a line end the original returns the same last line
// forever; here the remainder is consumed (see PLAYLIST-2).
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cIGZString.h"
#include "rz/cRZString.h"

struct cGZPlayListEntry {
    cRZString title;           // +0x08
    cRZString path;            // +0x30
    std::uint32_t length = 0;  // +0x50 seconds (0 = unknown)
    cGZPlayListEntry() = default;
    cGZPlayListEntry(std::uint32_t entryLength, const cIGZString& entryTitle, const cIGZString& entryPath)
        : title(entryTitle), path(entryPath), length(entryLength) {}
};

class cGZPlayList {
public:
    // Reads `path` and fills Entries(); false only when the file cannot be opened or read.
    bool Open(const cIGZString& path);
    const std::vector<cGZPlayListEntry>& Entries() const { return entries_; }

    // ---- text helpers (members of the original class, none touches the object) ----
    // Next non-blank line of `text` into `line` and removes it (and its line end) from `text`; false when only blanks remain.
    static bool GetLine(cRZString& text, cRZString& line);
    static bool FindNotOneOf(const cRZString& text, const cRZString& set, std::uint32_t& index);
    static bool FindOneOf(const cRZString& text, const cRZString& set, std::uint32_t& index);
    // `path` relative to `base` -> `out` (may alias `path`).
    static bool MakeAbsolutePath(const cRZString& base, const cRZString& path, cRZString& out);
    // A rooted path ("\x" or "C:"): moves its first segment (root included) from `path` to `segment`.
    static bool IsSpec(cRZString& path, cRZString& segment);
    static void AddNextPathSegment(cRZString& path, cRZString& out);
    // Moves the text up to and including the next '\\' found at or after `offset` from `source` to `segment`.
    static bool GrabSegment(cRZString& source, cRZString& segment, int offset);
    static bool StripEndSegment(cRZString& path);

private:
    bool ParseExtM3UFormat();
    bool ParsePLSFormat();
    bool ParseM3UFormat();

    std::vector<cGZPlayListEntry> entries_;   // +0x08
    cRZString path_;                          // +0x18
    cRZString directory_;                     // +0x40
    cRZString text_;                          // +0x68
};
