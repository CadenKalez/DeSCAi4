#include "sc4/TileNeighborhood.h"

#include <cstdlib>

namespace sc4 {

namespace {
// 0x100a1a3a8: connection direction code per (direction + 2 * edge) & 7
constexpr std::uint8_t kDirectionTable[8] = {2, 3, 0, 1, 2, 3, 0, 1};
}  // namespace

void cSC4TileNeighborhoodCompact::SetEdgeCondition(int edge, std::uint8_t condition) {
    const unsigned shift = (static_cast<std::uint8_t>(edge) << 3) & 0x1f;
    packed_ = (static_cast<std::uint32_t>(condition) << shift) | (~(0xffu << shift) & packed_);
}

void cSC4TileNeighborhoodCompact::GetMirrorImageEquivalent(cSC4TileNeighborhoodCompact& out) const {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const std::uint8_t b = static_cast<std::uint8_t>(packed_ >> (i * 8));
        std::uint8_t high = b & 0xf8, low = b & 7;
        if (low == 1) {
            low = 3;
        } else if (low == 2) {
            high = static_cast<std::uint8_t>((b * 2) & 0x20 | (b & 8) | ((b >> 1) & 0x10));
            low = 2;
        } else if (low == 3) {
            low = 1;
        }
        unsigned destination = i;
        if ((i | 2) != 3) destination += 2;   // edges 0 and 2 trade places (index 4 wraps back to 0), 1 and 3 stay
        const unsigned shift = (destination << 3) & 0x1f;
        result = (result & ~(0xffu << shift)) | (static_cast<std::uint32_t>(low | (b & 4) | high) << shift);
    }
    out.packed_ = result;
}

void cSC4TileNeighborhoodCompact::GetStraightAndDiagonalEdges(int* straight, int* diagonal, int& straightCount,
                                                              int& diagonalCount) const {
    straightCount = 0;
    diagonalCount = 0;
    for (int i = 0; i < 4; ++i) {
        const unsigned kind = (packed_ >> (i * 8)) & 7;
        if (kind == 2) straight[straightCount++] = i;
        else if (kind == 3 || kind == 1) diagonal[diagonalCount++] = i;
    }
}

void cSC4TileNeighborhoodCompact::Merge(const cSC4TileNeighborhoodCompact& other) {
    const std::uint32_t source = other.packed_;
    if (source == 0) return;
    std::uint32_t value = packed_;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const std::uint32_t current = value >> shift;
        const std::uint8_t incoming = static_cast<std::uint8_t>(source >> shift);
        const unsigned incomingKind = incoming & 7, currentKind = current & 7;
        if (currentKind == 3) {
            if (static_cast<std::uint8_t>(incomingKind - 1) < 2) value = (2u << shift) | (((current & 7) << shift) ^ value);
        } else if (currentKind == 1) {
            if ((incoming & 6) == 2) value = (2u << shift) | (((current & 7) << shift) ^ value);
        } else if (currentKind == 0) {
            value |= static_cast<std::uint32_t>(incomingKind) << shift;
        }
    }
    packed_ = value;
}

bool cSC4TileNeighborhoodCompact::Read(ITileTokenSource& tokens) {
    packed_ = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const char* token = tokens.GetToken();
        if (!token) return false;
        const std::uint32_t number = static_cast<std::uint32_t>(std::atoi(token)) & 0xff;
        const std::uint32_t entry = (((number / 10) * 8) & 0xf8) | (number % 10);
        packed_ = (entry << shift) | (~(0xffu << shift) & packed_);
    }
    return true;
}

void cSC4TileNeighborhoodCompact::ResetEdgeMinorTypes() {
    std::uint32_t value = packed_;
    for (unsigned shift = 0; shift < 32; shift += 8) value = (((value >> shift) & 7) << shift) | (~(0xffu << shift) & value);
    packed_ = value;
}

void cSC4TileNeighborhoodCompact::SetEdgeTypeMinor(int edge, std::uint8_t minor) {
    const unsigned shift = (static_cast<std::uint8_t>(edge) << 3) & 0x1f;
    packed_ = ((((packed_ >> shift) & 7) + (minor & 0x1fu) * 8) << shift) | (~(0xffu << shift) & packed_);
}

void cSC4TileNeighborhoodCompact::SetEdgeConnectionDirection(int edge, std::uint8_t direction) {
    const unsigned shift = (static_cast<std::uint8_t>(edge) << 3) & 0x1f;
    const std::uint32_t flag = (packed_ & (4u << shift)) != 0 ? 4u : 0u;
    const std::uint8_t entry = static_cast<std::uint8_t>(flag | kDirectionTable[(static_cast<unsigned>(direction) + edge * 2) & 7]);
    packed_ = (static_cast<std::uint32_t>(entry) << shift) | (~(0xffu << shift) & packed_);
}

// ---- lot configuration ---------------------------------------------------------------------------------

cSC4NetworkLotConfiguration::cSC4NetworkLotConfiguration() : flags16_(16, false), flags4_(4, false) {}

std::uint32_t cSC4NetworkLotConfiguration::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4NetworkLotConfiguration::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

// ---- lot -----------------------------------------------------------------------------------------------

cSC4NetworkLot::~cSC4NetworkLot() {
    if (config_) config_->Release();
    if (occupant_) occupant_->Release();
}

std::uint32_t cSC4NetworkLot::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4NetworkLot::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cISC4NetworkLot) return false;
    *out = this;
    AddRef();
    return true;
}

const std::vector<cSC4LotConfigurationObject>& cSC4NetworkLot::GetLotConfigurationObjectArray() const {
    static const std::vector<cSC4LotConfigurationObject> empty;
    return config_ ? config_->GetObjectArray() : empty;
}

std::vector<cSC4LotConfigurationObject>& cSC4NetworkLot::GetLotConfigurationObjectArray() {
    static std::vector<cSC4LotConfigurationObject> empty;
    return config_ ? config_->GetObjectArray() : empty;
}

void cSC4NetworkLot::SetNetworkOccupant(cIGZUnknown* occupant) {
    if (occupant) occupant->AddRef();
    if (occupant_) occupant_->Release();
    occupant_ = occupant;
}

void cSC4NetworkLot::SetConfiguration(cSC4NetworkLotConfiguration* configuration) {
    if (configuration) configuration->AddRef();
    if (config_) config_->Release();
    config_ = configuration;
}

}  // namespace sc4
