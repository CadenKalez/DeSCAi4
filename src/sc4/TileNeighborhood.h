// cSC4TileNeighborhoodCompact (12 functions, 0x1001aee18..0x1001af0fe) — the four edge descriptors of a network
// tile packed into one 32-bit word, byte i = edge i: bits 0..2 the connection kind (0 none, 1 / 3 diagonal, 2
// straight), bit 2 is also the direction flag kept by SetEdgeConnectionDirection, bits 3..7 the minor type.
// Also here: cSC4NetworkLot (iid 0x89a5ba66) and cSC4NetworkLotConfiguration (iid 1), the small reference-counted
// records the network lot manager hands out (0x100409190..0x10040956a).
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"
#include "sc4/LotConfiguration.h"

namespace sc4 {

// What Read needs from the tile file tokenizer (cSC4FileTokenizer::GetToken): the next token or null.
class ITileTokenSource {
public:
    virtual ~ITileTokenSource() = default;
    virtual const char* GetToken() = 0;
};

class cSC4TileNeighborhoodCompact {
public:
    cSC4TileNeighborhoodCompact() = default;
    explicit cSC4TileNeighborhoodCompact(std::uint32_t packed) : packed_(packed) {}

    std::uint32_t Packed() const { return packed_; }
    std::uint32_t GetEdgeCondition(int edge) const { return (packed_ >> ((edge & 3) << 3)) & 0xff; }
    void SetEdgeCondition(int edge, std::uint8_t condition);
    void GetMirrorImageEquivalent(cSC4TileNeighborhoodCompact& out) const;
    // straight edges (kind 2) go to `straight`, diagonal ones (kinds 1 and 3) to `diagonal`
    void GetStraightAndDiagonalEdges(int* straight, int* diagonal, int& straightCount, int& diagonalCount) const;
    bool HasConnectionOnEdge(int edge) const { return HasConnectionOnEdge(packed_, edge); }
    static bool HasConnectionOnEdge(std::uint32_t packed, int edge) { return (packed & (3u << ((edge & 3) << 3))) != 0; }
    void Merge(const cSC4TileNeighborhoodCompact& other);
    bool Read(ITileTokenSource& tokens);
    void ResetEdgeMinorTypes();
    void SetEdgeTypeMinor(int edge, std::uint8_t minor);
    void SetEdgeConnectionDirection(int edge, std::uint8_t direction);

private:
    std::uint32_t packed_ = 0;
};

class cSC4NetworkLotConfiguration {
public:
    cSC4NetworkLotConfiguration();
    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();
    bool QueryInterface(std::uint32_t iid, void** out);
    const std::vector<cSC4LotConfigurationObject>& GetObjectArray() const { return objects_; }
    std::vector<cSC4LotConfigurationObject>& GetObjectArray() { return objects_; }
    std::uint32_t GetID() const { return id_; }
    void SetID(std::uint32_t id) { id_ = id; }
    void GetName(cIGZString& out) const { out.Copy(name_); }
    void SetName(const cIGZString& name) { name_.Copy(name); }
    std::uint32_t GetNetworkPieceId() const { return networkPieceId_; }
    void SetNetworkPieceId(std::uint32_t id) { networkPieceId_ = id; }
    std::uint32_t TileNeighborhood() const { return neighborhood_; }

private:
    std::vector<cSC4LotConfigurationObject> objects_;   // +8
    std::uint32_t id_ = 0;                              // +0x20
    cRZString name_;                                    // +0x28
    std::uint32_t field48_ = 0, networkPieceId_ = 0;    // +0x48, +0x50
    std::uint32_t neighborhood_ = 0x0f0f0f0f;           // +0x54
    std::uint8_t field58_ = 4;                          // +0x58
    std::vector<bool> flags16_, flags4_;                // +0x60, +0x78
    std::uint32_t field90_ = 0;
    float field94_ = 64.0f;
    std::uint8_t field98_ = 0xf, field99_ = 0;
    std::uint32_t refCount_ = 0;                        // +0x9c
};

constexpr std::uint32_t kGZIID_cISC4NetworkLot = 0x89a5ba66;

class cSC4NetworkLot {
public:
    ~cSC4NetworkLot();
    std::uint32_t AddRef() { return ++refCount_; }
    std::uint32_t Release();
    bool QueryInterface(std::uint32_t iid, void** out);
    std::size_t GetLotConfigurationObjectCount() const { return config_ ? config_->GetObjectArray().size() : 0; }
    // an empty shared array when the lot has no configuration
    const std::vector<cSC4LotConfigurationObject>& GetLotConfigurationObjectArray() const;
    cIGZUnknown* GetNetworkOccupant() const { return occupant_; }
    void SetNetworkOccupant(cIGZUnknown* occupant);        // takes a reference
    void SetConfiguration(cSC4NetworkLotConfiguration* configuration);   // takes a reference

private:
    cIGZUnknown* occupant_ = nullptr;               // +8
    cSC4NetworkLotConfiguration* config_ = nullptr; // +0x10
    std::uint32_t refCount_ = 0;                    // +0x1c
};

}  // namespace sc4
