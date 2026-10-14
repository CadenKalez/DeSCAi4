#include "audio/HitMan.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include "audio/HitClassIds.h"
#include "audio/HitManAccess.h"
#include "audio/HitTables.h"
#include "audio/interfaces/cIGZSndViewUpdateTarget.h"

namespace {

constexpr char kHexDigits[] = "0123456789abcdefABCDEF";

bool IsHexDigit(char c) { return c != '\0' && std::strchr(kHexDigits, c) != nullptr; }

// RZAtoUint32HexA @ 0x100019fcf: parses up to eight hex digits.
std::uint32_t ParseHex8(const char* s) {
    std::uint32_t v = 0;
    for (int i = 0; i < 8 && IsHexDigit(s[i]); ++i) {
        const char c = s[i];
        const std::uint32_t d = c <= '9' ? static_cast<std::uint32_t>(c - '0')
                                         : static_cast<std::uint32_t>((c | 0x20) - 'a' + 10);
        v = (v << 4) | d;
    }
    return v;
}

// EnumFunc_UpdateView @ 0x1004bf512: tells every update target that implements the view
// interface that the listener changed; always asks to continue.
bool EnumFuncUpdateView(cIGZSndUpdateTarget* target, std::uint32_t, void*) {
    void* view = nullptr;
    if (target->QueryInterface(kGZIID_cIGZSndViewUpdateTarget, &view) && view != nullptr) {
        auto* v = static_cast<cIGZSndViewUpdateTarget*>(view);
        v->UpdateView();
        v->Release();
    }
    return true;
}

// Reference-counted list entry of the sound-system lookups below.
template <class T>
T* Find(std::map<std::uint32_t, cRZAutoRefCount<T>>& m, std::uint32_t key) {
    const auto it = m.find(key);
    return it == m.end() ? nullptr : it->second.get();
}

}  // namespace

// ---- RZ::HM() ---------------------------------------------------------------------------------

namespace {
cHitMan* g_hitMan = nullptr;

// The narrow register/random view the sem models use (see HitManAccess.h).
class HitManRegisterViewAdapter final : public RZ::cHitManRegisterView {
public:
    std::int32_t GetRegisterValue(std::int32_t id) const override { return g_hitMan ? g_hitMan->GetRegisterVal(id) : 0; }
    std::int32_t Random(std::int32_t range, std::int32_t lower) override {
        return g_hitMan ? g_hitMan->Random(range, lower) : 0;
    }
};
}  // namespace

void RZ::SetHitMan(cHitMan* manager) { g_hitMan = manager; }
cHitMan* RZ::HitManInstance() { return g_hitMan; }

RZ::cHitManRegisterView* RZ::HM() {
    static HitManRegisterViewAdapter adapter;
    return g_hitMan != nullptr ? &adapter : nullptr;
}

// ---- construction -----------------------------------------------------------------------------

// cHitMan::cHitMan() @ 0x1004be428: service id 0x1b2ce671, priority/tick priority 0x7a116.
cHitMan::cHitMan() : cRZSystemService(kServiceId_HitMan, 0x7a116) {}

// cHitMan::~cHitMan() @ 0x1004be7d6
cHitMan::~cHitMan() {
    if (g_hitMan == this) g_hitMan = nullptr;
    while (!blocks_.empty()) {
        cRZAutoRefCount<cIHitTrackDataBlock> block = blocks_.front();
        ReleaseBlockBuffer(block.get());
        blocks_.pop_front();
    }
    // The original leaks the control groups; the port releases the reference the map holds.
    for (auto& [id, group] : controlGroups_) group->Release();
    controlGroups_.clear();
}

bool cHitMan::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIHitMan && iid != kGZIID_cIGZUnknown) return cRZSystemService::QueryInterface(iid, out);
    *out = static_cast<cIHitMan*>(this);
    AddRef();
    return true;
}

// ---- service life cycle -----------------------------------------------------------------------

// cHitMan::Init() @ 0x1004bede6
bool cHitMan::Init() {
    if (initialized_) return true;
    if (g_hitMan == nullptr) g_hitMan = this;
    RegisterSem(&sem2dDefault_);
    RegisterSem(&sem3dDefault_);

    Hit::cIHitManServices* services = Hit::GetServices();
    hasSoundSystem_ = services != nullptr && services->HasSoundSystem();
    activeTracks_.clear();
    activeTracksLive_ = true;

    if (services != nullptr) {
        constexpr std::uint32_t kPropertyGroup = 0xfc45caf7;
        const cGZPersistResourceKey plainKey{0xebfee345, kPropertyGroup, 0x0bfd8404};
        const cGZPersistResourceKey trackKey{0x0b8d821a, kPropertyGroup, 0xab903a1b};
        const cGZPersistResourceKey channelKey{0xebfee345, kPropertyGroup, 0xcb954e2c};
        propertySetResource_ = services->CreatePropertySetResource(plainKey, nullptr, 0, false);
        trackDefaultsResource_ = services->CreatePropertySetResource(
            trackKey, Hit::kTrackPropertyDefaults.data(), Hit::kTrackPropertyDefaults.size(), true);
        // The channel resource first gets 0xff4c97be = 0, then the table.
        std::vector<Hit::DefaultProperty> channelDefaults;
        channelDefaults.reserve(Hit::kChannelPropertyDefaults.size() + 1);
        channelDefaults.push_back({0xff4c97be, 0});
        channelDefaults.insert(channelDefaults.end(), Hit::kChannelPropertyDefaults.begin(),
                               Hit::kChannelPropertyDefaults.end());
        channelDefaultsResource_ = services->CreatePropertySetResource(
            channelKey, channelDefaults.data(), channelDefaults.size(), true);
    }
    initialized_ = true;
    return true;
}

// cHitMan::Shutdown() @ 0x1004bf27c
bool cHitMan::Shutdown() {
    if (!initialized_) return true;
    sequenceSnds_.clear();
    if (activeTracksLive_) {
        for (cIGZSnd* snd : activeTracks_) {
            if (snd != nullptr) snd->Kill();
        }
        activeTracks_.clear();
        activeTracksLive_ = false;
    }
    typeGroups_.clear();
    sourceDataHandler_ = nullptr;
    trackDefaultsResource_ = nullptr;   // (the original releases +0x88 and +0x98 here,
    channelDefaultsResource_ = nullptr; //  but keeps +0x78 until the destructor)
    sems_.clear();
    // The original clears the lists without freeing the loaded hit data; the port frees the
    // buffers it allocated itself (ownership rule: see ReleaseBlockBuffer).
    for (auto& block : blocks_) ReleaseBlockBuffer(block.get());
    blocks_.clear();
    sequenceSnds_.clear();
    dbSegments_.clear();
    initialized_ = false;
    return true;
}

// ---- pause / ducking --------------------------------------------------------------------------

bool cHitMan::Pause() {  // @ 0x1004bf790
    DuckMapRemoveAll();
    return true;
}

void cHitMan::DuckMapSetSndobPri(const cIGZSnd* snd, std::int32_t priority) {  // @ 0x1004bfbae
    if (!initialized_) return;
    const auto it = duckMap_.find(snd);
    if (priority == 0) {
        if (it == duckMap_.end()) return;  // nothing to remove: no register update either
        duckMap_.erase(it);
    } else if (it == duckMap_.end()) {
        duckMap_[snd] = priority;
    } else {
        const std::int32_t previous = it->second;
        it->second = priority;
        if (previous == priority) return;  // unchanged: no register update
    }
    SetRegisterVal(0x1b, DuckMapMaxPri());
    UpdateView();
}

std::int32_t cHitMan::DuckMapSndobPri(const cIGZSnd* snd) {  // @ 0x1004bfdb4
    const auto it = duckMap_.find(snd);
    return it == duckMap_.end() ? 0 : it->second;
}

std::int32_t cHitMan::DuckMapMaxPri() {  // @ 0x1004bfd48
    std::int32_t best = 0;
    for (const auto& entry : duckMap_) best = std::max(best, entry.second);
    return best;
}

void cHitMan::DuckMapRemoveAll() {  // @ 0x1004bfe24
    duckMap_.clear();
    SetRegisterVal(0x1b, 0);
    UpdateView();
}

void cHitMan::UpdateView() {  // @ 0x1004bf576
    if (!hasSoundSystem_) return;
    if (Hit::cIHitManServices* services = Hit::GetServices()) {
        services->EnumerateUpdateTargets(&EnumFuncUpdateView, 0, nullptr);
    }
}

// ---- registers --------------------------------------------------------------------------------

// SetRegisterVal/GetRegisterVal @ 0x1004bf854 / 0x1004bf7c8: the binary writes into the property
// set at this+0x80, which is never assigned (null dereference if ever called). The port keeps
// the same observable contract (missing registers read as 0) in a local table.
bool cHitMan::SetRegisterVal(std::int32_t id, std::int32_t value) {
    registers_[id] = value;
    return true;
}

std::int32_t cHitMan::GetRegisterVal(std::int32_t id) const {
    const auto it = registers_.find(id);
    return it == registers_.end() ? 0 : it->second;
}

std::int32_t cHitMan::Random(std::int32_t upper, std::int32_t lower) {  // @ 0x1004c2548
    return static_cast<std::int32_t>(random_.RandomUint32Uniform(static_cast<std::uint32_t>(upper - lower))) + lower;
}

// ---- track players and channels --------------------------------------------------------------

bool cHitMan::CreateTrackPlayer(std::uint32_t iid, void** out) {  // @ 0x1004bf4b4
    Hit::cIHitManServices* services = Hit::GetServices();
    return services != nullptr && services->CreateTrackPlayer(iid, out);
}

// The original ignores `iid` and always asks the new channel for cIGZSnd (0x206077d2).
bool cHitMan::AllocChannel(std::uint32_t, void** out) {  // @ 0x1004bfa7a
    void* snd = nullptr;
    Hit::cIHitManServices* services = Hit::GetServices();
    const bool ok = services != nullptr && services->CreateChannel(&snd);
    if (ok && snd != nullptr) channels_.push_back(static_cast<cIGZSnd*>(snd));
    *out = snd;
    return ok;
}

void cHitMan::FreeChannel(cIGZSnd* channel) {  // @ 0x1004bfb3c — Release only if it was allocated here
    const auto it = std::find(channels_.begin(), channels_.end(), channel);
    if (it == channels_.end()) return;
    channels_.erase(it);
    channel->Release();
}

// The binary's "remove" ignores its argument and pops the *front* entry; adding therefore first
// drops the oldest entry and the list never holds more than the most recent track.
bool cHitMan::AddToActiveTrackList(cIGZSnd* snd) {  // @ 0x1004bf8de
    RemoveFromActiveTrackList(snd);
    activeTracks_.push_back(snd);
    return true;
}

bool cHitMan::RemoveFromActiveTrackList(const cIGZSnd*) {  // @ 0x1004bf922
    if (!activeTracks_.empty()) activeTracks_.pop_front();
    return true;
}

// ---- sems -------------------------------------------------------------------------------------

bool cHitMan::RegisterSem(cIHitSem* sem) {  // @ 0x1004c1e76
    const std::uint32_t id = sem->GetId();
    sems_[id] = sem;  // AddRef the new one before releasing the old
    return id != 0;
}

bool cHitMan::RemoveSem(std::uint32_t semId) { return sems_.erase(semId) != 0; }  // @ 0x1004c1fc2

bool cHitMan::RemoveAllSems() {  // @ 0x1004c2022
    sems_.clear();
    return true;
}

bool cHitMan::GetSem(std::uint32_t semId, std::uint32_t iid, void** out) const {  // @ 0x1004c1d24
    const auto it = sems_.find(semId);
    return it != sems_.end() && it->second.get()->QueryInterface(iid, out);
}

cIHitSem* cHitMan::GetSem(std::uint32_t semId) { return Find(sems_, semId); }  // @ 0x1004c1d84

// Walks `index` steps from the first entry in key order (the original has no bounds check).
cIHitSem* cHitMan::GetSemAt(std::uint32_t index) {  // @ 0x1004c1e10
    if (index >= sems_.size()) return nullptr;
    auto it = sems_.begin();
    std::advance(it, index);
    return it->second.get();
}

// ---- control groups ---------------------------------------------------------------------------

cIHitControlGroup* cHitMan::ControlGroup(std::int32_t groupId) {  // @ 0x1004bfebe
    const auto it = controlGroups_.find(groupId);
    if (it != controlGroups_.end()) return it->second;
    cIHitControlGroup* group = new cHitControlGroup(groupId);  // starts with refcount 1 (owned by the map)
    controlGroups_[groupId] = group;
    return group;
}

// The original reaches itself through the service registry (id 0x1b2ce671) before doing this.
bool cHitMan::AddToControlGroup(std::int32_t groupId, std::int32_t trackId) {  // @ 0x1004c011a
    ControlGroup(groupId)->AddTrack(trackId);
    return true;
}

// ---- sequence group sounds ---------------------------------------------------------------------

void cHitMan::SetSequenceGroupSnd(std::uint32_t groupId, cIGZSnd* snd) { sequenceSnds_[groupId] = snd; }  // @ 0x1004bf65a

bool cHitMan::GetSequenceGroupSnd(std::uint32_t groupId, cIGZSnd** out) {  // @ 0x1004bf5be
    const auto it = sequenceSnds_.find(groupId);
    if (it == sequenceSnds_.end()) {
        *out = nullptr;
        return false;
    }
    *out = it->second.get();
    return true;
}

// ---- app data / DB segments / type groups --------------------------------------------------------

bool cHitMan::RegisterSourceDataRequestHandler(cIHitAppDataRequestHandler* handler) {  // @ 0x1004c0082
    sourceDataHandler_ = handler;
    return true;
}

bool cHitMan::GetSourceDataField(std::int32_t sourceId, std::int32_t fieldId, std::int32_t* out) const {  // @ 0x1004bfe92
    return sourceDataHandler_ && sourceDataHandler_->GetSourceDataField(sourceId, fieldId, out);
}

void cHitMan::AddDBSegment(std::uint32_t segmentId, cIGZPersistDBSegment* segment) { dbSegments_[segmentId] = segment; }

bool cHitMan::FindDBSegment(std::uint32_t segmentId, std::uint32_t iid, void** out) {  // @ 0x1004c1ad2
    cIGZPersistDBSegment* seg = Find(dbSegments_, segmentId);
    return seg != nullptr && seg->QueryInterface(iid, out);
}

void cHitMan::RemoveDBSegment(std::uint32_t segmentId) { dbSegments_.erase(segmentId); }

bool cHitMan::AddTypeToGroupAssociation(std::uint32_t resourceType, std::uint32_t group) {  // @ 0x1004c1b8e
    auto& groups = typeGroups_[resourceType];
    if (std::find(groups.begin(), groups.end(), group) == groups.end()) groups.push_back(group);
    return true;
}

// cHitMan::GetHitList @ 0x1004bf956: the hit-list resource type (0x7b1acfcd) may live in several
// groups; the first group that yields the instance wins. `ignored` is unused in the binary.
bool cHitMan::GetHitList(std::uint32_t listId, std::uint32_t, void** out) {
    Hit::cIHitManServices* services = Hit::GetServices();
    if (services == nullptr) return false;
    const auto it = typeGroups_.find(HitIds::kResTypeHitList);
    if (it == typeGroups_.end()) return false;
    for (std::uint32_t group : it->second) {
        const cGZPersistResourceKey key{HitIds::kResTypeHitList, group, listId};
        if (services->GetPersistResource(key, kGZIID_cIHitList, out)) return true;
    }
    return false;
}

// ---- track data blocks -------------------------------------------------------------------------

void cHitMan::ReleaseBlockBuffer(cIHitTrackDataBlock* block) {
    // The binary frees DataStartAddress() of every block it drops. The port frees only buffers
    // ParseHitFile allocated, so a block registered by someone else can never be double freed.
    const std::uint8_t* data = block->DataStartAddress();
    if (data != nullptr && ownedBuffers_.erase(data) != 0) delete[] data;
}

bool cHitMan::RegisterTrackDataBlock(cIHitTrackDataBlock* block) {  // @ 0x1004c01b4
    if (block == nullptr) return false;
    void* checked = nullptr;
    if (!block->QueryInterface(kGZIID_cIHitTrackDataBlock, &checked)) return false;
    RemoveTrackDataBlock(block);        // never registered twice
    blocks_.emplace_front(block);       // newest first: later loads shadow earlier entry points
    static_cast<cIHitTrackDataBlock*>(checked)->Release();  // the QueryInterface reference, dropped last
    return true;
}

bool cHitMan::RemoveTrackDataBlock(cIHitTrackDataBlock* block) {  // @ 0x1004c02ea
    blocks_.remove_if([block](const cRZAutoRefCount<cIHitTrackDataBlock>& b) { return b.get() == block; });
    return true;
}

bool cHitMan::GetTrackDataBlockAndOffset(std::uint32_t entryKey, cIHitTrackDataBlock** outBlock,
                                         std::uint32_t& outOffset) {  // @ 0x1004c0430
    for (auto& block : blocks_) {
        std::uint32_t offset = 0;
        if (block->GetEntryPointOffset(entryKey, offset)) {
            *outBlock = block.get();
            outOffset = offset;
            return true;
        }
    }
    return false;
}

// The original looks the manager up through the service registry; for the single instance this
// is the same object.
const std::uint8_t* cHitMan::TrackDataEntryPoint(std::uint32_t entryKey) const {  // @ 0x1004c04ca
    for (const auto& block : blocks_) {
        std::uint32_t offset = 0;
        if (block->GetEntryPointOffset(entryKey, offset)) return block->DataStartAddress() + offset;
    }
    return nullptr;
}

// ---- hit file loading ---------------------------------------------------------------------------

// cHitMan::FilepathHasEmbeddedKey @ 0x1004c0c9c: finds three 8-hex-digit numbers in a path
// ("...XXXXXXXX-XXXXXXXX-XXXXXXXX..."), type then group then instance. Every number must be
// non-zero. Quirk kept: the last digit of a number is also the first digit counted for the next
// one, so only seven further digits are needed.
bool cHitMan::FilepathHasEmbeddedKey(const cIGZString& path, cGZPersistResourceKey& key) {
    const char* s = path.ToChar();
    const std::uint32_t length = path.Strlen();
    std::uint32_t values[3] = {};
    std::uint32_t index = 0xfffffff9u;  // index of the first character of the candidate number
    std::uint32_t run = 0;
    for (int n = 0; n < 3; ++n) {
        run = 0;
        for (;;) {
            if (!(index + 7 < length)) return false;
            run = IsHexDigit(s[index + 7]) ? run + 1 : 0;
            if (run == 8) break;
            ++index;
        }
        values[n] = ParseHex8(s + index);
        if (values[n] == 0) return false;
    }
    key = {values[0], values[1], values[2]};
    return true;
}

// cHitMan::ParseHitFile @ 0x1004c09ee. The file is read into a new[] buffer that becomes the
// data block's storage; entry points are the (key, offset) dword pairs between the "ENTP" and
// "ENTE" tags. The debug-info service is told about the file afterwards.
bool cHitMan::ParseHitFile(const cIGZString& fileName, const std::uint8_t* bytes, std::uint32_t size) {
    std::uint8_t* buffer = new std::uint8_t[size ? size : 1];
    if (size) std::memcpy(buffer, bytes, size);
    ownedBuffers_.insert(buffer);

    Hit::cIHitManServices* services = Hit::GetServices();
    auto* block = new cHitTrackDataBlock();
    block->AddRef();
    block->SetFilename(fileName, services != nullptr ? services->ApplicationDirectory() : std::string());
    block->SetDataBlock(buffer, size);
    RegisterTrackDataBlock(block);
    block->Release();

    constexpr std::uint32_t kTagEntryStart = 0x50544e45;  // "ENTP"
    constexpr std::uint32_t kTagEntryEnd = 0x544e4545;    // "ENTE"
    auto dwordAt = [&](std::uint32_t off) {
        std::uint32_t v;
        std::memcpy(&v, buffer + off, 4);
        return v;
    };
    std::uint32_t pos = 0;
    while (pos + 4 <= size && dwordAt(pos) != kTagEntryStart) ++pos;  // byte-granular search
    if (pos + 4 <= size) {
        for (pos += 4; pos + 4 <= size && dwordAt(pos) != kTagEntryEnd; pos += 8) {
            if (pos + 8 > size) break;
            block->AddEntryPoint(dwordAt(pos), dwordAt(pos + 4));
        }
    }
    if (services != nullptr) services->ParseDebugInfo(fileName, buffer, size);
    return true;
}

// cHitMan::ParseHitFileFromIGZPersistRecordAsIGZFile @ 0x1004c0e8a
bool cHitMan::ParseHitFileFromIGZPersistRecordAsIGZFile(const cGZPersistResourceKey& key) {
    Hit::cIHitManServices* services = Hit::GetServices();
    if (services == nullptr) return false;
    std::vector<std::uint8_t> bytes;
    std::string name;
    if (!services->ReadPersistRecord(key, bytes, name)) return false;
    cRZString fileName(name.c_str());
    return ParseHitFile(fileName, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

// cHitMan::LoadHitFile @ 0x1004c06ea. Returns false when a block with the same file name is
// already loaded (a second load is refused, not reloaded).
bool cHitMan::LoadHitFile(const cIGZString& path) {
    const std::string name(path.ToChar(), path.Strlen());
    for (auto& block : blocks_) {
        cRZString existing;
        block->GetFilename(existing);
        if (existing.str() == name) return false;
    }
    cGZPersistResourceKey key;
    if (FilepathHasEmbeddedKey(path, key) && ParseHitFileFromIGZPersistRecordAsIGZFile(key)) return true;

    Hit::cIHitManServices* services = Hit::GetServices();
    std::vector<std::uint8_t> bytes;
    if (services == nullptr || !services->ReadFileBytes(name, bytes)) return false;
    cRZString fileName(name.c_str());
    return ParseHitFile(fileName, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

std::string cHitMan::ResolveAgainst(const std::string& name, const std::string& baseDirectory) {
    const bool absolute = !name.empty() && (name[0] == '/' || name[0] == '\\' || (name.size() > 1 && name[1] == ':'));
    if (absolute || baseDirectory.empty()) return name;
    std::string base = baseDirectory;
    if (base.back() != '/' && base.back() != '\\') base += '/';
    return base + name;
}

// cHitMan::UnloadHitFile @ 0x1004c1320: the name is made absolute against the data directory,
// upper-cased, and the first block with exactly that file name is dropped (its buffer freed).
// Always returns true, also when nothing matched.
bool cHitMan::UnloadHitFile(const cIGZString& path) {
    Hit::cIHitManServices* services = Hit::GetServices();
    std::string name(path.ToChar(), path.Strlen());
    name = ResolveAgainst(name, services != nullptr ? services->DataDirectory() : std::string());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
        cRZString existing;
        (*it)->GetFilename(existing);
        if (existing.str() == name) {
            cRZAutoRefCount<cIHitTrackDataBlock> keep = *it;
            ReleaseBlockBuffer(keep.get());
            blocks_.erase(it);
            break;
        }
    }
    return true;
}

// ---- track flow errors ----------------------------------------------------------------------------

void cHitMan::HandleTrackFlowError(cIHitTrackPlayer* player, const char* message) {  // @ 0x1004be400
    if (flowErrorHandler_ != nullptr) flowErrorHandler_(player, message);
}

// HandleTrackFlowControlError @ 0x1004be3b9: the binary asks the track player for its channel
// object (slot +0x18), calls slot +0x48 on it and then slot +0x80 on the player. The track
// player interface is recovered with cHitTrackPlayer (see HitTrackPlayer.cpp), which installs
// the real body through RegisterDefaultTrackFlowErrorHandler.
namespace {
cIHitMan::TrackFlowErrorHandler g_defaultFlowErrorBody = nullptr;
}
void cHitMan::DefaultTrackFlowErrorHandler(cIHitTrackPlayer* player, const char* message) {
    if (g_defaultFlowErrorBody != nullptr) g_defaultFlowErrorBody(player, message);
}
void RegisterDefaultTrackFlowErrorHandler(cIHitMan::TrackFlowErrorHandler body) { g_defaultFlowErrorBody = body; }
