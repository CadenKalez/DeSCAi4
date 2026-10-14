#include "sc4/cSC4ZoneManager.h"

#include <algorithm>

#include "sc4/DBSegment.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
const cGZPersistResourceKey kZoneTunablesKey{0x6534284a, 0xe7e2c2db, 0xe9482490};
}

cSC4ZoneManager::cSC4ZoneManager(IZoneManagerHost* host) : host_(host) {
    defaultDisplayAlpha_ = 0xff;
    displayAlpha_ = 0xff;
}

std::uint32_t cSC4ZoneManager::GetDevelopmentFailureCount(int zoneType, int result) const {
    if (zoneType >= 16 || result >= 19) return 0;
    auto it = failures_.find({static_cast<std::int8_t>(zoneType), static_cast<std::int8_t>(result)});
    return it == failures_.end() ? 0 : it->second;
}

bool cSC4ZoneManager::GetTextureForZone(int zoneType, cGZPersistResourceKey& key) const {
    if (static_cast<unsigned>(zoneType) >= 16u) return false;
    key.type = 0x7ab50e44;
    key.group = 0x1abe787d;
    key.instance = textureInstance_[zoneType];
    return true;
}

void cSC4ZoneManager::SetZoneDisplayAlpha(std::uint8_t alpha) {
    if (displayAlpha_ == alpha) return;
    displayAlpha_ = alpha;
    host_->OnZoneDisplayAlphaChanged();
}

void cSC4ZoneManager::ReadSimulatorTunableValues() {
    std::fill(std::begin(zoneParameterA_), std::end(zoneParameterA_), 0);
    std::fill(std::begin(zoneParameterB_), std::end(zoneParameterB_), 0);
    cISCPropertyHolder* tunables = host_->GetTunables(kZoneTunablesKey);
    if (!tunables) return;

    auto copy32 = [&](std::uint32_t id, auto* dest) {
        cISCProperty* property = tunables->GetProperty(id);
        if (!property) return;
        const cIGZVariant* value = property->GetPropertyValue();
        using Element = std::remove_pointer_t<decltype(dest)>;
        if constexpr (std::is_same_v<Element, std::int32_t>) {
            const std::int32_t* src = value->RefSint32();
            for (int i = 0; i < kZoneTypeCount; ++i) dest[i] = src[i];
        } else if constexpr (std::is_same_v<Element, std::uint32_t>) {
            const std::uint32_t* src = value->RefUint32();
            for (int i = 0; i < kZoneTypeCount; ++i) dest[i] = src[i];
        } else if constexpr (std::is_same_v<Element, float>) {
            const float* src = value->RefFloat32();
            for (int i = 0; i < kZoneTypeCount; ++i) dest[i] = src[i];
        } else {
            const std::int64_t* src = value->RefSint64();
            for (int i = 0; i < kZoneTypeCount; ++i) dest[i] = src[i];
        }
    };
    copy32(0xe94825b2, minSize_);
    copy32(0xe94825b3, maxSize_);
    copy32(0xe94825b9, developmentCost_);
    copy32(0xe94825b4, zoneParameterA_);
    copy32(0xe94825b5, zoneParameterB_);
    copy32(0xe94825b6, textureInstance_);
    cSCPropertyHelp::GetPropertyValue(tunables, 0xe94825b7, displayAlpha_);
    defaultDisplayAlpha_ = displayAlpha_;
    copy32(0xe94825b8, dragColor_);
}

bool cSC4ZoneManager::Init() {
    if (initialized_) return true;
    ReadSimulatorTunableValues();
    host_->RegisterNotifications();
    if (!countsLoaded_) {
        const std::int32_t cells = host_->CityCellsX() * host_->CityCellsZ();
        std::fill(std::begin(zoneCount_), std::end(zoneCount_), 0);
        std::fill(std::begin(undevelopedByType_), std::end(undevelopedByType_), cells);
        std::fill(std::begin(secondCounts_), std::end(secondCounts_), cells);
        zoneCount_[0] = cells;
        undevelopedByType_[0] = cells;
        undevelopedTotal_ = cells;
    }
    if (!zoneGrid_) {
        zoneGrid_ = new cSC4SimGrid<std::int8_t>();
        zoneGrid_->Init();
        zoneGrid_->SetTractSize(1);
        zoneGrid_->SetTractValues(0);
    }
    initialized_ = true;
    return true;
}

bool cSC4ZoneManager::Shutdown() {
    if (!initialized_) return true;
    initialized_ = false;
    host_->UnregisterNotifications();
    if (zoneGrid_) {
        zoneGrid_->Shutdown();
        zoneGrid_ = nullptr;
    }
    return true;
}

bool cSC4ZoneManager::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    SC4WriteGZSerializable(stream, static_cast<const cIGZSerializable*>(zoneGrid_.get()));
    for (int i = 0; i < kZoneTypeCount; ++i) stream.SetSint32(zoneCount_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.SetSint32(undevelopedByType_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.SetSint32(secondCounts_[i]);
    stream.SetSint32(undevelopedTotal_);
    stream.SetSint32(counter13c_);
    stream.SetSint32(abandonedTotal_);
    stream.SetSint32(counter144_);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.SetSint32(minSize_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.SetSint32(maxSize_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.SetSint64(zoneParameterA_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.SetSint64(zoneParameterB_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.SetUint32(textureInstance_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.SetUint32(dragColor_[i]);
    stream.SetUint8(displayAlpha_);
    stream.SetUint32(static_cast<std::uint32_t>(failures_.size()));
    for (const auto& [key, count] : failures_) {
        stream.SetSint8(key.zoneType);
        stream.SetSint8(key.result);
        stream.SetUint32(count);
    }
    stream.SetUint32(static_cast<std::uint32_t>(successes_.size()));
    for (const auto& [key, count] : successes_) {
        stream.SetSint8(key.zoneType);
        stream.SetSint8(key.result);
        stream.SetUint32(count);
    }
    for (int i = 0; i < 5; ++i) SC4WriteGZSerializable(stream, host_->GetServiceForStream(i));
    return !stream.GetError();
}

bool cSC4ZoneManager::Read(cIGZIStream& stream) {
    countsLoaded_ = true;
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 1) return false;
    void* grid = nullptr;
    if (SC4ReadGZSerializable(stream, kGZIID_cISC4SimGrid, &grid, false)) {
        zoneGrid_ = static_cast<cSC4SimGrid<std::int8_t>*>(static_cast<cISC4SimGridBase*>(grid));
        static_cast<cISC4SimGridBase*>(grid)->Release();
    }
    for (int i = 0; i < kZoneTypeCount; ++i) stream.GetSint32(zoneCount_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.GetSint32(undevelopedByType_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.GetSint32(secondCounts_[i]);
    stream.GetSint32(undevelopedTotal_);
    stream.GetSint32(counter13c_);
    stream.GetSint32(abandonedTotal_);
    stream.GetSint32(counter144_);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.GetSint32(minSize_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.GetSint32(maxSize_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.GetSint64(zoneParameterA_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.GetSint64(zoneParameterB_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.GetUint32(textureInstance_[i]);
    for (int i = 0; i < kZoneTypeCount; ++i) stream.GetUint32(dragColor_[i]);
    stream.GetUint8(displayAlpha_);
    auto readMap = [&](std::map<ZoneDevelopmentResult, std::uint32_t>& map) {
        std::uint32_t count = 0;
        stream.GetUint32(count);
        map.clear();
        for (std::uint32_t i = 0; i < count; ++i) {
            ZoneDevelopmentResult key;
            std::uint32_t value = 0;
            stream.GetSint8(key.zoneType);
            stream.GetSint8(key.result);
            stream.GetUint32(value);
            map.emplace(key, value);   // a duplicate key keeps the first value
        }
    };
    readMap(failures_);
    readMap(successes_);
    for (int i = 0; i < 5; ++i) {
        void* service = nullptr;
        if (SC4ReadGZSerializable(stream, kStreamServiceIid[i], &service, false) && service) {
            auto* object = static_cast<cIGZUnknown*>(service);
            host_->OnServiceLoaded(i, object);
            object->Release();
        }
    }
    return !stream.GetError();
}

}  // namespace sc4
