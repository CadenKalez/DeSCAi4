// cSC4SimGrid<T> — the simulator's tract grid: a coarse square grid laid over the city whose
// cells ("tracts") are 2^shift city cells wide. All simulators keep their per-area data in these
// (pollution, land value, crime, ...). Recovered from Sim City 4 Deluxe Editionsub 1.2.1 for the ten
// instantiations the game creates (vtable of the float grid 0x100afe668; cSC4COMDirector::
// CreateSC4SimGrid* @ 0x100436dfa..0x10043716a), all with identical logic:
//
//   T         class id      stream element
//   uint8/bool 0x49b9e602   SetUint8        sint8  0x49b9e603   SetSint8
//   uint16    0x49b9e604    SetUint16       sint16 0x49b9e605   SetSint16
//   uint32    0x49b9e606    SetUint32       sint32 0x49b9e607   SetSint32
//   uint64    0x49b9e608    SetUint64       sint64 0x49b9e609   SetSint64
//   float     0x49b9e60a    SetFloat32      double 0x49b9e60b   SetFloat64
//
// Quirks kept: rectangle averages sum the values converted to 64-bit integers (so float grids
// truncate each sample) and divide by the rectangle area; a rectangle inside one tract returns that
// tract's value unaveraged; SetTractSize only re-lays the grid when the size changes and keeps old
// values (new tracts are zero); Read does not validate the stored counts.
//
// The city geometry the original reads from the global city object (SL::spCity) is injected
// through ISimGridCity.
#pragma once
#include <memory>
#include <type_traits>
#include <cstdint>
#include <vector>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZSerializable.h"

namespace sc4 {

// What a grid needs to know about the city (city vtable slots 0x2e8/0x2f0 and 0x2d8/0x2e0).
class ISimGridCity {
public:
    virtual ~ISimGridCity() = default;
    virtual std::int32_t CitySizeXInCells() const = 0;
    virtual std::int32_t CitySizeZInCells() const = 0;
    virtual float CellSizeXMeters() const = 0;
    virtual float CellSizeZMeters() const = 0;
};
ISimGridCity* SimGridCity();
void SetSimGridCity(ISimGridCity* city);

constexpr std::uint32_t kGZIID_cISC4SimGrid = 0xc9bd939a;
constexpr std::uint32_t kGZIID_cIGZSerializableSimGrid = 0xe4fda3d4;
constexpr std::uint32_t kGZIID_cISC4SimGridInt16 = 0x223e2b67;   // the typed interface of the int16 grid (power line, protection)

template <class T> struct SimGridTraits;
#define SC4_SIMGRID_TRAITS(Type, Id, Put, Get) \
    template <> struct SimGridTraits<Type> { \
        static constexpr std::uint32_t kClassId = Id; \
        static bool Write(cIGZOStream& s, Type v) { return s.Put(v); } \
        static bool Read(cIGZIStream& s, Type& v) { return s.Get(v); } \
    };
SC4_SIMGRID_TRAITS(std::uint8_t, 0x49b9e602, SetUint8, GetUint8)
SC4_SIMGRID_TRAITS(std::int8_t, 0x49b9e603, SetSint8, GetSint8)
SC4_SIMGRID_TRAITS(std::uint16_t, 0x49b9e604, SetUint16, GetUint16)
SC4_SIMGRID_TRAITS(std::int16_t, 0x49b9e605, SetSint16, GetSint16)
SC4_SIMGRID_TRAITS(std::uint32_t, 0x49b9e606, SetUint32, GetUint32)
SC4_SIMGRID_TRAITS(std::int32_t, 0x49b9e607, SetSint32, GetSint32)
SC4_SIMGRID_TRAITS(std::uint64_t, 0x49b9e608, SetUint64, GetUint64)
SC4_SIMGRID_TRAITS(std::int64_t, 0x49b9e609, SetSint64, GetSint64)
SC4_SIMGRID_TRAITS(float, 0x49b9e60a, SetFloat32, GetFloat32)
SC4_SIMGRID_TRAITS(double, 0x49b9e60b, SetFloat64, GetFloat64)
#undef SC4_SIMGRID_TRAITS

// The non-typed part of the grid interface (iid 0xc9bd939a); the typed accessors live on
// cSC4SimGrid<T>.
class cISC4SimGridBase : public cIGZUnknown {};

template <class T>
class cSC4SimGrid : public cISC4SimGridBase, public cIGZSerializable {
public:
    cSC4SimGrid() = default;

    // One implementation serves both interface bases.
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cISC4SimGrid || iid == kGZIID_cIGZUnknown ||
            (std::is_same_v<T, std::int16_t> && iid == kGZIID_cISC4SimGridInt16))
            *out = static_cast<cISC4SimGridBase*>(this);
        else if (iid == kGZIID_cIGZSerializableSimGrid) *out = static_cast<cIGZSerializable*>(this);
        else return false;
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override {
        if (refCount_ < 2) {
            Shutdown();
            delete this;
            return 0;
        }
        return static_cast<std::uint32_t>(--refCount_);
    }

    bool Init() { if (!initialized_) initialized_ = true; return true; }
    bool Shutdown() { if (initialized_) initialized_ = false; return true; }
    std::uint32_t GetInstanceID() const { return instanceId_; }
    void SetInstanceID(std::uint32_t id) { instanceId_ = id; }

    // City cell (x, z) -> its tract's value.
    T GetCellValue(std::int32_t x, std::int32_t z) const { return data_[x >> shift_][z >> shift_]; }
    float GetAverageValueInCellRect(std::int32_t x, std::int32_t z, std::int32_t w, std::int32_t h) const {
        std::int32_t tx = x >> shift_, tz = z >> shift_;
        if (tx == ((w - 1 + x) >> shift_) && tz == ((h + z - 1) >> shift_)) return static_cast<float>(data_[tx][tz]);
        std::int64_t sum = 0;
        for (std::int32_t i = x; i < x + w; ++i)
            for (std::int32_t j = z; j < z + h; ++j) sum += static_cast<std::int64_t>(data_[i >> shift_][j >> shift_]);
        return static_cast<float>(static_cast<double>(sum) / static_cast<double>(h * w));
    }
    void SetTractSize(std::int32_t size);
    std::int32_t GetTractSize() const { return tractSize_; }
    std::int32_t GetTractShift() const { return shift_; }
    std::int32_t GetTractCountX() const { return countX_; }
    std::int32_t GetTractCountZ() const { return countZ_; }
    float GetTractWidthX() const { return widthX_; }
    float GetTractWidthZ() const { return widthZ_; }
    float GetOneOverTractWidthX() const { return invWidthX_; }
    float GetOneOverTractWidthZ() const { return invWidthZ_; }
    bool TractIsInBounds(std::int32_t x, std::int32_t z) const {
        return static_cast<std::uint32_t>(x) < static_cast<std::uint32_t>(countX_) &&
               static_cast<std::uint32_t>(z) < static_cast<std::uint32_t>(countZ_);
    }
    void PositionToTract(float x, float z, std::int32_t& tx, std::int32_t& tz) const {
        tx = static_cast<std::int32_t>(x * invWidthX_);
        tz = static_cast<std::int32_t>(z * invWidthZ_);
    }
    void TractCornerToPosition(std::int32_t tx, std::int32_t tz, float& x, float& z) const {
        x = static_cast<float>(tx) * widthX_;
        z = static_cast<float>(tz) * widthZ_;
    }
    void TractCenterToPosition(std::int32_t tx, std::int32_t tz, float& x, float& z) const {
        x = (static_cast<float>(tx) + 0.5f) * widthX_;
        z = (static_cast<float>(tz) + 0.5f) * widthZ_;
    }
    T GetTractValue(std::int32_t tx, std::int32_t tz) const { return data_[tx][tz]; }
    void SetTractValue(std::int32_t tx, std::int32_t tz, T value) { data_[tx][tz] = value; }
    void SetTractValues(T value) {
        for (auto& column : data_)
            for (T& cell : column) cell = value;
    }
    float GetAverageValueInTractRect(std::int32_t x, std::int32_t z, std::int32_t w, std::int32_t h) const {
        std::int64_t sum = 0;
        for (std::int32_t i = x; i < x + w; ++i)
            for (std::int32_t j = z; j < z + h; ++j) sum += static_cast<std::int64_t>(data_[i][j]);
        return static_cast<float>(static_cast<double>(sum) / static_cast<double>(h * w));
    }
    std::vector<std::vector<T>>& GetGridData() { return data_; }
    const std::vector<std::vector<T>>& GetGridData() const { return data_; }

    // cIGZSerializable
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return classId_; }

private:
    std::int32_t refCount_ = 0;
    bool initialized_ = false;
    std::uint32_t classId_ = SimGridTraits<T>::kClassId;
    std::uint32_t instanceId_ = 0;
    std::int32_t tractSize_ = 0, shift_ = 0, countX_ = 0, countZ_ = 0;
    float widthX_ = 0, widthZ_ = 0, invWidthX_ = 0, invWidthZ_ = 0;
    std::vector<std::vector<T>> data_;   // [x][z]
};

template <class T>
void cSC4SimGrid<T>::SetTractSize(std::int32_t size) {
    if (tractSize_ == size) return;
    tractSize_ = size;
    shift_ = 0;
    if (size > 1) {
        std::int32_t shift = 0;
        std::uint64_t v = static_cast<std::uint32_t>(size);
        std::uint32_t previous;
        do {
            ++shift;
            previous = static_cast<std::uint32_t>(v);
            v >>= 1;
        } while (previous > 3);
        shift_ = shift;
    }
    const ISimGridCity* city = SimGridCity();
    countX_ = city->CitySizeXInCells() >> shift_;
    countZ_ = city->CitySizeZInCells() >> shift_;
    widthX_ = static_cast<float>(tractSize_) * city->CellSizeXMeters();
    float cellZ = city->CellSizeZMeters();
    widthZ_ = static_cast<float>(tractSize_) * cellZ;
    invWidthX_ = 1.0f / widthX_;
    invWidthZ_ = 1.0f / (static_cast<float>(tractSize_) * cellZ);
    data_.resize(static_cast<std::size_t>(countX_));
    for (auto& column : data_) column.resize(static_cast<std::size_t>(countZ_), T{});
}

template <class T>
bool cSC4SimGrid<T>::Write(cIGZOStream& s) const {
    s.SetUint16(1);
    s.SetUint8(initialized_ ? 1 : 0);
    s.SetUint32(classId_);
    s.SetUint32(instanceId_);
    s.SetSint32(tractSize_);
    s.SetSint32(shift_);
    s.SetSint32(countX_);
    s.SetSint32(countZ_);
    s.SetFloat32(widthX_);
    s.SetFloat32(widthZ_);
    s.SetFloat32(invWidthX_);
    s.SetFloat32(invWidthZ_);
    for (std::int32_t x = 0; x < countX_; ++x)
        for (std::int32_t z = 0; z < countZ_; ++z) SimGridTraits<T>::Write(s, data_[x][z]);
    return true;
}

template <class T>
bool cSC4SimGrid<T>::Read(cIGZIStream& s) {
    std::uint16_t version = 0;
    s.GetUint16(version);
    if (version != 1) return false;
    std::uint8_t init = 0;
    s.GetUint8(init);
    initialized_ = init != 0;
    s.GetUint32(classId_);
    s.GetUint32(instanceId_);
    s.GetSint32(tractSize_);
    s.GetSint32(shift_);
    s.GetSint32(countX_);
    s.GetSint32(countZ_);
    s.GetFloat32(widthX_);
    s.GetFloat32(widthZ_);
    s.GetFloat32(invWidthX_);
    s.GetFloat32(invWidthZ_);
    data_.resize(static_cast<std::size_t>(countX_));
    for (std::int32_t x = 0; x < countX_; ++x) {
        data_[x].resize(static_cast<std::size_t>(countZ_), T{});
        for (std::int32_t z = 0; z < countZ_; ++z) SimGridTraits<T>::Read(s, data_[x][z]);
    }
    return true;
}

// Owner for a grid a simulator keeps. Grids are reference counted: one read from a city file is also held by the
// serializer's object table, so deleting it directly (std::unique_ptr's default) frees it under the table's feet.
// Release() drops this owner's reference and deletes a grid nobody else holds (count below 2).
template <class T>
struct SimGridDeleter {
    void operator()(cSC4SimGrid<T>* grid) const { if (grid) grid->Release(); }
};
template <class T>
using SimGridPtr = std::unique_ptr<cSC4SimGrid<T>, SimGridDeleter<T>>;

// Downcast of a grid read from a stream. The class id in the file decides the element type, so a record that holds
// a different type than the caller expects (or a stream that is out of step) must be refused: treating a uint8 grid
// as an int16 one corrupts the vectors and crashes in the destructor. Releases the reference on a mismatch.
template <class T>
cSC4SimGrid<T>* SimGridCast(void* raw) {
    if (!raw) return nullptr;
    auto* base = static_cast<cISC4SimGridBase*>(raw);
    auto* grid = dynamic_cast<cSC4SimGrid<T>*>(base);
    if (!grid) base->Release();
    return grid;
}

}  // namespace sc4
