// cRZDiskRadioStation — see DiskRadioStation.h for the overview, the layout and the quirks kept. Function order
// follows the binary (0x1004c3360..0x1004c86f0).
#include "rz/DiskRadioStation.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "gz/PlayList.h"
#include "gz/cIGZString.h"
#include "rz/cRZDirectory.h"
#include "rz/cRZFile.h"

// cRZDiskRadioStation is 0x218 bytes in the binary (CreatecRZDiskRadioStation allocates exactly that).
static_assert(sizeof(cRZDiskRadioStation) == 0x218, "cRZDiskRadioStation layout");

namespace {

// libc++'s std::random_shuffle (removed in C++17): Fisher-Yates driven by C rand(), which is what both the
// constructor and ShuffleCycle / ShufflePlayList call (symbols random_shuffle<std::__wrap_iter<int*>> @ 0x1004c37b7
// and random_shuffle<std::__wrap_iter<cRZString*>> @ 0x1004c5956).
template <class Iterator>
void RandomShuffle(Iterator first, Iterator last) {
    if (first == last) return;
    for (Iterator i = first + 1; i != last; ++i) {
        Iterator j = first + std::rand() % ((i - first) + 1);
        if (i != j) std::iter_swap(i, j);
    }
}

// The cursor of a playlist list: the original stores a raw node pointer (+0x118/+0x160/+0x1a8/+0x1f0), advances it
// with node->next and wraps to the first node when it reaches the sentinel. A default-constructed cursor (the
// original's null) is treated as "at the beginning"; the original dereferences it (see DiskRadioStation.h).
std::list<cRZString>::iterator NextNode(std::list<cRZString>& list, std::list<cRZString>::iterator cursor) {
    if (list.empty()) return list.end();
    if (cursor == list.end()) return list.begin();
    std::list<cRZString>::iterator next = std::next(cursor);
    return next == list.end() ? list.begin() : next;
}

// Advances the cursor and answers the newly selected entry (null when the list is empty; the original would follow a
// null or dangling node pointer there).
const cRZString* AdvanceCursor(std::list<cRZString>& list, std::list<cRZString>::iterator& cursor) {
    cursor = NextNode(list, cursor);
    if (cursor == list.end()) return nullptr;
    return &*cursor;
}

// The "is this entry on the modifier/exclusion list" test of PlayNextSelectionNotOnExclusionList (a length test plus a
// memcmp in the binary, i.e. a plain std::string comparison).
bool IsOnList(const std::list<cRZString>& list, const cRZString& value) {
    for (const cRZString& item : list) {
        if (item.str() == value.str()) return true;
    }
    return false;
}

unsigned RandPercent() { return static_cast<unsigned>(std::rand()) % 100u; }

}  // namespace

// @ 0x1004c3360 (the second symbol @ 0x1004c388c is the same constructor)
cRZDiskRadioStation::cRZDiskRadioStation() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(cRZDiskRadioStation, refCount_) == 0x08, "refCount_ at +0x08");
    static_assert(offsetof(cRZDiskRadioStation, onAir_) == 0x0c, "onAir_ at +0x0c");
    static_assert(offsetof(cRZDiskRadioStation, welcomePlayed_) == 0x0d, "welcomePlayed_ at +0x0d");
    static_assert(offsetof(cRZDiskRadioStation, cycles_) == 0x10, "cycles_ at +0x10");
    static_assert(offsetof(cRZDiskRadioStation, cycleIndex_) == 0x28, "cycleIndex_ at +0x28");
    static_assert(offsetof(cRZDiskRadioStation, musicRepeatCount_) == 0x2c, "musicRepeatCount_ at +0x2c");
    static_assert(offsetof(cRZDiskRadioStation, started_) == 0x30, "started_ at +0x30");
    static_assert(offsetof(cRZDiskRadioStation, timer_) == 0x38, "timer_ at +0x38");
    static_assert(offsetof(cRZDiskRadioStation, radio_) == 0x50, "radio_ at +0x50");
    static_assert(offsetof(cRZDiskRadioStation, sound_) == 0x58, "sound_ at +0x58");
    static_assert(offsetof(cRZDiskRadioStation, callSign_) == 0x60, "callSign_ at +0x60");
    static_assert(offsetof(cRZDiskRadioStation, description_) == 0x88, "description_ at +0x88");
    static_assert(offsetof(cRZDiskRadioStation, stationDirectory_) == 0xb0, "stationDirectory_ at +0xb0");
    static_assert(offsetof(cRZDiskRadioStation, musicDirectory_) == 0xd8, "musicDirectory_ at +0xd8");
    static_assert(offsetof(cRZDiskRadioStation, musicList_) == 0x100, "musicList_ at +0x100");
    static_assert(offsetof(cRZDiskRadioStation, musicCursor_) == 0x118, "musicCursor_ at +0x118");
    static_assert(offsetof(cRZDiskRadioStation, djDirectory_) == 0x120, "djDirectory_ at +0x120");
    static_assert(offsetof(cRZDiskRadioStation, djList_) == 0x148, "djList_ at +0x148");
    static_assert(offsetof(cRZDiskRadioStation, djCursor_) == 0x160, "djCursor_ at +0x160");
    static_assert(offsetof(cRZDiskRadioStation, commercialDirectory_) == 0x168, "commercialDirectory_ at +0x168");
    static_assert(offsetof(cRZDiskRadioStation, commercialList_) == 0x190, "commercialList_ at +0x190");
    static_assert(offsetof(cRZDiskRadioStation, commercialCursor_) == 0x1a8, "commercialCursor_ at +0x1a8");
    static_assert(offsetof(cRZDiskRadioStation, jingleDirectory_) == 0x1b0, "jingleDirectory_ at +0x1b0");
    static_assert(offsetof(cRZDiskRadioStation, jingleList_) == 0x1d8, "jingleList_ at +0x1d8");
    static_assert(offsetof(cRZDiskRadioStation, jingleCursor_) == 0x1f0, "jingleCursor_ at +0x1f0");
    static_assert(offsetof(cRZDiskRadioStation, modifierList_) == 0x1f8, "modifierList_ at +0x1f8");
    static_assert(offsetof(cRZDiskRadioStation, playlistMode_) == 0x210, "playlistMode_ at +0x210");
    static_assert(offsetof(cRZDiskRadioStation, shuffleStrategy_) == 0x214, "shuffleStrategy_ at +0x214");
#pragma clang diagnostic pop

    // The cycle order starts as {0,1,2,3} and is shuffled once here (the four push_backs of the original).
    cycles_.reserve(static_cast<std::size_t>(kRZRadioCycleTypeCount));
    for (std::int32_t cycle = 0; cycle < kRZRadioCycleTypeCount; ++cycle) cycles_.push_back(cycle);
    RandomShuffle(cycles_.begin(), cycles_.end());

    started_ = false;            // +0x30
    cycleIndex_ = 0;             // +0x28 and +0x2c (one 8-byte store in the binary)
    musicRepeatCount_ = 0;

    // ConstructWithUnits(4) at +0x38, then SetUnits(4) (which clears it again) and Start().
    timer_.SetUnits(cRZTimer::kMilliseconds);
    timer_.Start();

    musicCursor_ = musicList_.end();
    djCursor_ = djList_.end();
    commercialCursor_ = commercialList_.end();
    jingleCursor_ = jingleList_.end();
}

// @ 0x1004c3896 (aliases 0x1004c39b4 and 0x1004c39be)
cRZDiskRadioStation::~cRZDiskRadioStation() = default;

// @ 0x1004c39ec: only cIGZRadioStation (0x4056574b) and cIGZUnknown (1) are answered, both with the same pointer.
bool cRZDiskRadioStation::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZRadioStation || iid == kGZIID_cIGZUnknown) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// @ 0x1004c3a0e: the class's own counter at +0x08 (the symbol is void; the value in eax is whatever the increment
// left there).
std::uint32_t cRZDiskRadioStation::AddRef() { return ++refCount_; }

// @ 0x1004c3a1c: at a count of 1 the deleting destructor runs without the count being decremented first.
std::uint32_t cRZDiskRadioStation::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

// @ 0x1004c3a3c / 0x1004c3a44
bool cRZDiskRadioStation::Init() { return true; }
bool cRZDiskRadioStation::Shutdown() { return true; }

// @ 0x1004c3a4c: the "too many branches" warning Ghidra reports for the tail jump at 0x1004c3a95 is a plain
// tail call to PlayWelcome (vtable +0xc8).
void cRZDiskRadioStation::SetStation() {
    ReadCallSignFromDisk();
    LoadAllPlaylists();
    ShufflePlaylist(shuffleStrategy_);
    if (welcomePlayed_) return;
    PlayWelcome();
}

// @ 0x1004c3a9c: empty in the binary.
void cRZDiskRadioStation::UnsetStation() {}

// @ 0x1004c3aa2: stationDirectory_ + "Welcome.wav", played only when the file exists.
bool cRZDiskRadioStation::PlayWelcome() {
    cRZString path(stationDirectory_);
    path.str().append(kRZRadioWelcomeFileName);
    if (cRZFile::FileExists(path)) {
        radio_->PlaySong(path);
        started_ = false;
        welcomePlayed_ = true;
    }
    return true;   // void in the binary
}

// @ 0x1004c3b50
bool cRZDiskRadioStation::OnIdle() {
    if (!IsOnTheAir()) return true;
    if (radio_->GetVolume() == 0) return true;
    if (radio_->IsPlaying()) return true;
    if (!started_) {
        // The radio is asked a second time before the selection is marked as started (as in the binary).
        if (radio_->IsPlaying()) return true;
        started_ = true;
        timer_.Reset();
        timer_.Start();
        return true;
    }
    if (timer_.GetElapsedTimeU32() < kRZRadioIdleDelayMs) return true;

    // Music is repeated 75% of the time without advancing the cycle order; the repeat counter is never read.
    if (cycles_[static_cast<std::size_t>(cycleIndex_)] == kRZRadioCycleMusic && IsCycleTypePresent(kRZRadioCycleMusic) &&
        RandPercent() < kRZRadioMusicRepeatPercent) {
        ++musicRepeatCount_;
        started_ = false;
        StartPlayingNextSelectionOfGivenType(kRZRadioCycleMusic);
        return true;
    }

    ++cycleIndex_;
    if (cycleIndex_ == static_cast<std::int32_t>(cycles_.size())) {
        ShuffleCycle();
        cycleIndex_ = 0;
    }

    std::int32_t cycleType = cycles_[static_cast<std::size_t>(cycleIndex_)];
    switch (cycleType) {
        case kRZRadioCycleMusic:
            if (!IsCycleTypePresent(kRZRadioCycleMusic)) return true;
            break;
        case kRZRadioCycleDj:
            if (RandPercent() >= kRZRadioDjPercent) return true;   // 0x31 < value in the binary
            if (!IsCycleTypePresent(kRZRadioCycleDj)) return true;
            break;
        case kRZRadioCycleCommercial:
            if (RandPercent() >= kRZRadioCommercialPercent) return true;
            if (!IsCycleTypePresent(kRZRadioCycleCommercial)) return true;
            break;
        case kRZRadioCycleJingle:
            if (RandPercent() >= kRZRadioJinglePercent) return true;
            if (!IsCycleTypePresent(kRZRadioCycleJingle)) return true;
            break;
        default:
            return true;
    }
    started_ = false;
    StartPlayingNextSelectionOfGivenType(cycleType);
    return true;
}

// @ 0x1004c3da4: the name as given, else the same name below musicDirectory_ (the station's own directory is not
// consulted - the binary only builds musicDirectory_ + name).
bool cRZDiskRadioStation::DoesFileExist(const cIGZString& fileName) {
    cRZString path;
    path.AssignFromIGZString(fileName);
    bool exists = cRZFile::FileExists(path);
    if (!exists) {
        path.str() = musicDirectory_.str() + path.str();
        exists = cRZFile::FileExists(path);
    }
    return exists;
}

// @ 0x1004c3eca: walks the music playlist from the cursor, wraps through the first node once and reshuffles the
// playlists when nothing is left. Playlist mode 1 accepts entries that are NOT on the modifier list, every other mode
// accepts only entries that ARE on it (quirk kept).
void cRZDiskRadioStation::PlayNextSelectionNotOnExclusionList() {
    if (musicList_.empty()) return;

    std::list<cRZString>::iterator start = musicCursor_;
    if (start == musicList_.end()) start = musicList_.begin();   // the original's null cursor
    musicCursor_ = std::next(start);
    if (start == musicCursor_) {   // the binary's `start == start->next` guard
        ShufflePlaylist(shuffleStrategy_);
        return;
    }

    bool wrapped = false;
    std::list<cRZString>::iterator next = musicCursor_;
    for (;;) {
        if (next == musicList_.end()) {
            if (wrapped) return;   // the sentinel was reached a second time: give up
            next = musicList_.begin();
            musicCursor_ = next;
            wrapped = true;
        }
        const cRZString& candidate = *next;
        const bool onModifierList = IsOnList(modifierList_, candidate);
        const bool skip = (playlistMode_ == 1) ? onModifierList : !onModifierList;
        if (!skip && DoesFileExist(candidate)) {
            cRZString selection(candidate);
            if (!cRZFile::FileExists(selection)) selection.str() = musicDirectory_.str() + selection.str();
            radio_->PlaySong(selection);
            return;
        }
        next = std::next(musicCursor_);
        musicCursor_ = next;
        if (next == start) {
            ShufflePlaylist(shuffleStrategy_);
            return;
        }
    }
}

// @ 0x1004c428c
bool cRZDiskRadioStation::StartPlayingNextSelectionOfGivenType(std::int32_t cycleType) {
    cRZString selection;
    switch (cycleType) {
        case kRZRadioCycleMusic: {
            // With a playlist mode (or a non-empty modifier list) the exclusion-list walk picks the music.
            if (playlistMode_ != 0 && (playlistMode_ != 1 || !modifierList_.empty())) {
                PlayNextSelectionNotOnExclusionList();
                return true;   // the binary returns the walk's eax, which is always 1
            }
            const cRZString* item = AdvanceCursor(musicList_, musicCursor_);
            if (!item) return false;   // the original follows a null/dangling node here
            selection = *item;
            if (!cRZFile::FileExists(selection)) selection.str() = musicDirectory_.str() + selection.str();
            break;
        }
        case kRZRadioCycleDj: {
            const cRZString* item = AdvanceCursor(djList_, djCursor_);
            if (!item) return false;
            selection.str() = djDirectory_.str() + item->str();
            break;
        }
        case kRZRadioCycleCommercial: {
            const cRZString* item = AdvanceCursor(commercialList_, commercialCursor_);
            if (!item) return false;
            selection.str() = commercialDirectory_.str() + item->str();
            break;
        }
        case kRZRadioCycleJingle: {
            const cRZString* item = AdvanceCursor(jingleList_, jingleCursor_);
            if (!item) return false;
            selection.str() = jingleDirectory_.str() + item->str();
            break;
        }
        default:
            return false;   // cycleType > 3 (the binary compares unsigned)
    }
    radio_->PlaySong(selection);
    return true;
}

// @ 0x1004c4590: one pass per sound-file filter token ("*.mp3", "*.wav", "*.xa", "*.m3u", "*.pls"), each appending to
// the four lists, then the .m3u/.pls entries are expanded into their contents.
bool cRZDiskRadioStation::LoadAllPlaylists() {
    cRZDirectory directory(cRZString(""), false, false, false);
    cRZString filter(kRZRadioSoundFileFilter);

    musicList_.clear();
    djList_.clear();
    commercialList_.clear();
    jingleList_.clear();

    cRZString token;
    for (;;) {
        const bool more = filter.SplitTokenAtDelimiter(',', token);
        directory.ChangeDirectory(musicDirectory_, false);
        directory.SetNewDirectoryEntryFilter(token, false);
        directory.ReadDirectoryFileEntriesIntoStringList(musicList_);
        directory.ChangeDirectory(djDirectory_, false);
        directory.SetNewDirectoryEntryFilter(token, false);   // the original copies the token per call
        directory.ReadDirectoryFileEntriesIntoStringList(djList_);
        directory.ChangeDirectory(commercialDirectory_, false);
        directory.SetNewDirectoryEntryFilter(token, false);
        directory.ReadDirectoryFileEntriesIntoStringList(commercialList_);
        directory.ChangeDirectory(jingleDirectory_, false);
        directory.SetNewDirectoryEntryFilter(token, false);
        directory.ReadDirectoryFileEntriesIntoStringList(jingleList_);
        if (!more) {
            ExpandPlayLists(musicDirectory_, musicList_);
            ExpandPlayLists(djDirectory_, djList_);
            ExpandPlayLists(commercialDirectory_, commercialList_);
            ExpandPlayLists(jingleDirectory_, jingleList_);
            return true;   // void in the binary
        }
    }
}

// @ 0x1004c4c36: for every listed name whose extension matches one of the playlist filter tokens, read the playlist
// and append its entries, then drop the playlist file itself from the list.
bool cRZDiskRadioStation::ExpandPlayLists(cRZString& directory, std::list<cRZString>& list) {
    const std::string& directoryName = directory.str();
    for (std::list<cRZString>::iterator it = list.begin(); it != list.end();) {
        // The original re-reads the node pointer after erasing it (quirk); the copy keeps those comparisons defined.
        const cRZString entry = *it;
        cRZString filter(kRZRadioPlaylistFilter);
        bool erased = false;
        bool more = false;
        cRZString token;
        do {
            more = filter.SplitTokenAtDelimiter(',', token);
            if (erased) continue;   // already expanded and removed: a name can only match one filter token
            token.Erase(0, 1);   // drop the leading '*' ("*.m3u" -> ".m3u")
            cRZString extension;
            extension.str() = entry.Right(token.Strlen()).str();
            if (!extension.IsEqualToStringPointer(&token, false)) continue;

            cGZPlayList playlist;
            cRZString path;
            if (cRZFile::FileExists(entry)) path.str() = entry.str();
            else path.str() = directoryName + entry.str();
            if (playlist.Open(path)) {
                for (const cGZPlayListEntry& playlistEntry : playlist.Entries()) {
                    cRZString name;
                    name.AssignFromIGZString(playlistEntry.path);
                    list.push_back(name);
                }
            }
            it = list.erase(it);
            erased = true;
        } while (more);
        if (!erased) ++it;
    }
    return true;   // void in the binary
}

// @ 0x1004c9230 (RZFileTimeSort::operator(), the comparator of the sort below): both paths are built from the
// comparator's directory, then the modified times are compared signed, so the newest entry sorts first.
bool RZFileTimeSort::operator()(const cRZString& a, const cRZString& b) const {
    cRZString pathA;
    cRZString pathB;
    pathA.str() = directory_.str() + a.str();
    pathB.str() = directory_.str() + b.str();
    const std::int32_t timeA = static_cast<std::int32_t>(cRZFile::GetTime(pathA, cRZFile::kTimeModified));
    const std::int32_t timeB = static_cast<std::int32_t>(cRZFile::GetTime(pathB, cRZFile::kTimeModified));
    return timeB < timeA;
}

// @ 0x1004c578a: copy the list, shuffle or sort it (newest first), clear and refill.
bool cRZDiskRadioStation::ShufflePlayList(const cRZString& directory, std::list<cRZString>& list,
                                          std::int32_t strategy) {
    if (list.empty()) return true;   // the binary only runs with a non-empty list
    std::vector<cRZString> items(list.begin(), list.end());
    if (strategy == 1) {
        std::sort(items.begin(), items.end(), RZFileTimeSort(directory));
    } else {
        RandomShuffle(items.begin(), items.end());
    }
    list.clear();
    for (const cRZString& item : items) list.push_back(item);
    return true;   // void in the binary
}

// @ 0x1004c5286: shuffle the four lists, then make sure the track that just played is not the new first one.
bool cRZDiskRadioStation::ShufflePlaylist(std::int32_t strategy) {
    const auto shuffleOne = [this, strategy](const cRZString& directory, std::list<cRZString>& list,
                                             std::list<cRZString>::iterator& cursor) {
        if (list.empty()) return;
        const std::string previousLast = list.back().str();   // the value of the last node before the shuffle
        ShufflePlayList(directory, list, strategy);
        std::list<cRZString>::iterator first = list.begin();
        if (previousLast == first->str()) {
            // The reshuffled first entry equals the one that played last: swap it with the last entry.
            const std::string lastValue = list.back().str();
            list.back().str() = first->str();
            first->str() = lastValue;
        }
        cursor = list.begin();
    };
    shuffleOne(musicDirectory_, musicList_, musicCursor_);
    shuffleOne(djDirectory_, djList_, djCursor_);
    shuffleOne(commercialDirectory_, commercialList_, commercialCursor_);
    shuffleOne(jingleDirectory_, jingleList_, jingleCursor_);
    return true;   // void in the binary
}

// @ 0x1004c5ab2: shuffle the cycle order, but keep the previous last cycle from becoming the first one (unless it
// was the music cycle, 0).
void cRZDiskRadioStation::ShuffleCycle() {
    const std::int32_t previousLast = cycles_.back();
    RandomShuffle(cycles_.begin(), cycles_.end());
    if (previousLast != 0 && cycles_.front() == previousLast) {
        cycles_.front() = cycles_.back();
        cycles_.back() = previousLast;
    }
}

// @ 0x1004c5af2: exactly four bytes, no terminator. The original copies the raw cRZString bytes (reading past the
// terminator of a shorter sign); the port pads with '?' like the buffer cRZRadio pre-fills.
void cRZDiskRadioStation::GetStationCallSign(char* out) {
    const std::string& sign = callSign_.str();
    for (std::size_t i = 0; i < kRZRadioCallSignLength; ++i) out[i] = i < sign.size() ? sign[i] : '?';
}

// @ 0x1004c5b4e: the description when it was read, otherwise the call sign (vslot +0x28 = AssignFromCString).
void cRZDiskRadioStation::GetStationDescription(cIGZString* out) {
    const cRZString& text = description_.str().empty() ? callSign_ : description_;
    out->AssignFromCString(text.ToChar());
}

// @ 0x1004c5b9c
bool cRZDiskRadioStation::IsOnTheAir() { return onAir_; }

// @ 0x1004c5ba6 / 0x1004c5bb2
void cRZDiskRadioStation::SetPlaylistMode(std::int32_t mode) { playlistMode_ = mode; }
void cRZDiskRadioStation::SetShuffleStrategy(std::int32_t strategy) { shuffleStrategy_ = strategy; }

// @ 0x1004c5bbe: the DJ/commercial/jingle cycles also need the matching radio option; cRZRadio answers options 0, 1
// and 3 only, so the jingle cycle (option 2) is never present.
bool cRZDiskRadioStation::IsCycleTypePresent(std::int32_t cycleType) {
    switch (cycleType) {
        case kRZRadioCycleMusic:
            return !musicList_.empty();
        case kRZRadioCycleDj:
            if (!radio_->GetOption(cIGZRadio::kRadioOption0)) return false;
            return !djList_.empty();
        case kRZRadioCycleCommercial:
            if (!radio_->GetOption(cIGZRadio::kRadioOption1)) return false;
            return !commercialList_.empty();
        case kRZRadioCycleJingle:
            if (!radio_->GetOption(cIGZRadio::kRadioOption2)) return false;
            return !jingleList_.empty();
        default:
            return false;
    }
}

// @ 0x1004c5c64: advance one cursor, wrapping to the first entry.
bool cRZDiskRadioStation::MoveToNextPlaylistItem(std::int32_t cycleType) {
    switch (cycleType) {
        case kRZRadioCycleMusic: musicCursor_ = NextNode(musicList_, musicCursor_); break;
        case kRZRadioCycleDj: djCursor_ = NextNode(djList_, djCursor_); break;
        case kRZRadioCycleCommercial: commercialCursor_ = NextNode(commercialList_, commercialCursor_); break;
        case kRZRadioCycleJingle: jingleCursor_ = NextNode(jingleList_, jingleCursor_); break;
        default: break;
    }
    return true;   // void in the binary
}

// @ 0x1004c5d54: the first "*.id" file names the station (and its extension is dropped), the first "*.txt" file is the
// description. Nothing is cleared when no file is found.
bool cRZDiskRadioStation::ReadCallSignFromDisk() {
    cRZDirectory directory(cRZString(""), false, false, false);
    cRZString filter(kRZRadioCallIdFilter);

    directory.ChangeDirectory(stationDirectory_, true);
    directory.SetNewDirectoryEntryFilter(filter, true);
    if (directory.GetDirectoryEntryCount() != 0) {
        const cRZDirectoryEntry* entry = directory.GetNthEntry(0);
        cRZFile::GetPathName(entry->name, callSign_, false);
    }

    const cRZString descriptionFilter(kRZRadioStationDescriptionFilter);
    filter.str() = descriptionFilter.str();   // the same filter string is reused
    directory.SetNewDirectoryEntryFilter(filter, true);
    if (directory.GetDirectoryEntryCount() != 0) {
        const cRZDirectoryEntry* entry = directory.GetNthEntry(0);
        cRZFile::GetPathName(entry->name, description_, false);
    }
    return !callSign_.str().empty();
}

// @ 0x1004c6070: replace the modifier list with a copy of the given one.
void cRZDiskRadioStation::SetModifierList(std::list<cRZString>* modifiers) {
    modifierList_.clear();
    if (!modifiers) return;
    for (const cRZString& modifier : *modifiers) {
        cRZString copy;
        copy.str() = modifier.str();
        modifierList_.push_back(copy);
    }
}

// @ 0x1004c86c6: cache the radio and the sound object it plays through.
void cRZDiskRadioStation::SetRadio(cIGZRadio* radio) {
    radio_ = radio;
    sound_ = radio->GetRadioSound();
}

// @ 0x1003def08: new (0x218) + constructor, no AddRef.
cRZDiskRadioStation* CreatecRZDiskRadioStation() { return new cRZDiskRadioStation(); }

// --- IRadioStationHost over the real class -------------------------------------------------------------------------

cIGZRadioStation* cRZDiskRadioStationHost::CreateDiskStation() { return CreatecRZDiskRadioStation(); }

// The host is only ever handed stations it created itself, so the downcast is safe (the binary wrote these members
// directly from cRZRadio).
cRZString& cRZDiskRadioStationHost::StationDirectory(cIGZRadioStation* station) {
    return static_cast<cRZDiskRadioStation*>(station)->stationDirectory_;
}
cRZString& cRZDiskRadioStationHost::MusicDirectory(cIGZRadioStation* station) {
    return static_cast<cRZDiskRadioStation*>(station)->musicDirectory_;
}
cRZString& cRZDiskRadioStationHost::DjDirectory(cIGZRadioStation* station) {
    return static_cast<cRZDiskRadioStation*>(station)->djDirectory_;
}
cRZString& cRZDiskRadioStationHost::CommercialDirectory(cIGZRadioStation* station) {
    return static_cast<cRZDiskRadioStation*>(station)->commercialDirectory_;
}
cRZString& cRZDiskRadioStationHost::JingleDirectory(cIGZRadioStation* station) {
    return static_cast<cRZDiskRadioStation*>(station)->jingleDirectory_;
}
void cRZDiskRadioStationHost::ClearStartedFlag(cIGZRadioStation* station) {
    static_cast<cRZDiskRadioStation*>(station)->started_ = false;
}

IRadioStationHost* DiskRadioStationHost() {
    static cRZDiskRadioStationHost host;
    return &host;
}
