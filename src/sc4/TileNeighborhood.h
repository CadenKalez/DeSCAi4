// cSC4TileNeighborhoodCompact (12 functions, 0x1001aee18..0x1001af0fe) — the four edge descriptors of a network
// tile packed into one 32-bit word, byte i = edge i: bits 0..2 the connection kind (0 none, 1 / 3 diagonal, 2
// straight), bit 2 is also the direction flag kept by SetEdgeConnectionDirection, bits 3..7 the minor type.
// Also here: cSC4NetworkLot (iid 0x89a5ba66) and cSC4NetworkLotConfiguration (iid 1), the small reference-counted
// records the network lot manager hands out (0x100409190..0x10040956a). The configuration record is 0xa0 bytes:
// objects at +8, id +0x20, name +0x28, network piece id +0x50, 4x4 tile mask +0x54, side count +0x58,
// zone-type bits +0x60, wealth-type bits +0x78, min/max slope +0x90/+0x94, rotation mask +0x98, flip mode +0x99,
// reference count +0x9c. The lot record is 0x28 bytes: occupant +8, configuration +0x10, the serialised
// configuration id +0x18, reference count +0x1c and the "built from the forced configuration" byte +0x20.
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
    cRZString& Name() { return name_; }                   // +0x28 (property 0x20 is read into it)
    const cRZString& Name() const { return name_; }
    std::uint32_t GetNetworkPieceId() const { return networkPieceId_; }
    void SetNetworkPieceId(std::uint32_t id) { networkPieceId_ = id; }
    std::uint32_t TileNeighborhood() const { return neighborhood_; }

    // The network lot manager reads and writes the configuration fields in place, exactly like the
    // original (which passes the object's own members to cSCPropertyHelp / the property writers).
    std::vector<bool>& ZoneTypeBits() { return flags16_; }        // +0x60 (property 0x88edc793)
    std::vector<bool>& WealthTypeBits() { return flags4_; }       // +0x78 (property 0x88edc795)
    std::uint32_t& Neighborhood() { return neighborhood_; }       // +0x54 (property 0x49d55951, 4 bytes)
    std::uint8_t* NeighborhoodBytes() { return reinterpret_cast<std::uint8_t*>(&neighborhood_); }
    std::uint8_t& Field58() { return field58_; }                  // +0x58 (property 0xca81b8d4)
    float& MinimumSlope() { return minSlope_; }                   // +0x90 (property 0xaa120972)
    float& MaximumSlope() { return maxSlope_; }                   // +0x94 (property 0xaa120973)
    std::uint8_t& RotationMask() { return rotationMask_; }        // +0x98 (property 0xec3bd470)
    std::uint8_t& FlipMode() { return flipMode_; }                // +0x99 (property 0xcc3e4755)

private:
    std::vector<cSC4LotConfigurationObject> objects_;   // +8
    std::uint32_t id_ = 0;                              // +0x20
    cRZString name_;                                    // +0x28
    std::uint32_t field48_ = 0, networkPieceId_ = 0;    // +0x48, +0x50
    std::uint32_t neighborhood_ = 0x0f0f0f0f;           // +0x54
    std::uint8_t field58_ = 4;                          // +0x58
    std::vector<bool> flags16_, flags4_;                // +0x60, +0x78
    float minSlope_ = 0.0f;                             // +0x90
    float maxSlope_ = 64.0f;                            // +0x94
    std::uint8_t rotationMask_ = 0xf;                   // +0x98
    std::uint8_t flipMode_ = 0;                         // +0x99
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
    std::vector<cSC4LotConfigurationObject>& GetLotConfigurationObjectArray();
    cIGZUnknown* GetNetworkOccupant() const { return occupant_; }
    void SetNetworkOccupant(cIGZUnknown* occupant);        // takes a reference
    void SetConfiguration(cSC4NetworkLotConfiguration* configuration);   // takes a reference
    cSC4NetworkLotConfiguration* GetConfiguration() const { return config_; }

    // The id of the configuration the lot was serialised with (+0x18). Read fills it in; Write reads
    // the configuration object instead, and Init reattaches the configuration by this id.
    std::uint32_t GetConfigurationId() const { return configurationId_; }
    void SetConfigurationId(std::uint32_t id) { configurationId_ = id; }
    // +0x20: the lot was built from the configuration forced with SetLotConfigurationForNextPiece, so
    // UpdateLot never re-evaluates it.
    bool UsesNextPieceConfiguration() const { return usesNextPieceConfiguration_; }
    void SetUsesNextPieceConfiguration(bool value) { usesNextPieceConfiguration_ = value; }

private:
    cIGZUnknown* occupant_ = nullptr;               // +8
    cSC4NetworkLotConfiguration* config_ = nullptr; // +0x10
    std::uint32_t configurationId_ = 0;             // +0x18
    std::uint32_t refCount_ = 0;                    // +0x1c
    bool usesNextPieceConfiguration_ = false;       // +0x20
};

}  // namespace sc4
