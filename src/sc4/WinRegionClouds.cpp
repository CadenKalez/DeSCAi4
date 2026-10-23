#include "sc4/WinRegionClouds.h"

#include <algorithm>
#include <cmath>

namespace sc4 {

namespace {
IRegionCloudsHost* g_host = nullptr;
constexpr float kCloudSize = 128.0f;
}  // namespace

IRegionCloudsHost* RegionCloudsHost() { return g_host; }
void SetRegionCloudsHost(IRegionCloudsHost* host) { g_host = host; }

cSC4WinRegionClouds::cSC4WinRegionClouds(cIGZWinMgr* mgr) : cGZWin(mgr) {
    timer_.SetUnits(cRZTimer::kMilliseconds);
    SetFlag(0x10000, false);
    SetFlag(0x200000, true);
}

cSC4WinRegionClouds::~cSC4WinRegionClouds() {
    cSC4WinRegionClouds::Shutdown();
    for (int i = 0; i < kRegionCloudsTextureCount; ++i) {
        if (textures_[i]) textures_[i]->Release();
    }
}

bool cSC4WinRegionClouds::Init() {
    if (get_init_flag()) return true;
    cGZWin::Init();
    SetID(kRegionCloudsWindowId);
    if (g_host && g_host->HasTextureFactory()) {
        for (int i = 0; i < kRegionCloudsTextureCount; ++i) {
            if (IRegionCloudTexture* old = textures_[i]) {
                textures_[i] = nullptr;
                old->Release();
            }
            textures_[i] = g_host->GetTexture(kRegionCloudsFirstTexture + i);
        }
    }
    cIGZWin* main = GetMainWindow();
    SetArea(0, 0, main->GetW(), main->GetH());
    area_[0] = 0.0f;
    area_[2] = static_cast<float>(GetW());
    area_[1] = 0.0f;
    area_[3] = static_cast<float>(GetH());
    return true;
}

bool cSC4WinRegionClouds::Shutdown() {
    if (get_init_flag()) {
        set_init_flag(false);
        cGZWin::Shutdown();
    }
    return true;
}

void cSC4WinRegionClouds::InitCloud(cCloud& cloud) {
    cloud.x = area_[0] - kCloudSize;
    const float top = area_[1], bottom = area_[3];
    const double r = random_.RandomDoubleUniform();
    cloud.y = static_cast<float>(r * static_cast<double>(bottom - top) + static_cast<double>(top - kCloudSize));
    const float speed = static_cast<float>(random_.RandomDoubleUniform() * 5.0 + 5.0);
    cloud.vx = drift_[0] * speed;
    cloud.vy = speed * drift_[1];
    cloud.texture = random_.RandomSint32RangeUniform(0, 4);
    cloud.opacity = static_cast<std::uint8_t>(random_.RandomSint32RangeUniform(opacityLow_, opacityHigh_));
}

void cSC4WinRegionClouds::SetCloudArea(float left, float top, float right, float bottom) {
    area_[0] = left;
    area_[1] = top;
    area_[2] = right;
    area_[3] = bottom;
    clouds_.clear();
    int count = static_cast<int>((area_[3] - area_[1]) * (area_[2] - area_[0]) * 6.1035156e-05f);
    while (count != 0) {
        cCloud cloud;
        InitCloud(cloud);
        const double lo = static_cast<double>(area_[0] - kCloudSize);
        const double span = static_cast<double>((area_[2] - area_[0]) - kCloudSize);
        cloud.x = static_cast<float>(random_.RandomDoubleUniform() * span + lo);
        clouds_.push_back(cloud);
        --count;
    }
}

void cSC4WinRegionClouds::SetDriftVector(float dx, float dy) {
    const float inv = 1.0f / std::sqrt(dy * dy + dx * dx);
    drift_[0] = dx * inv;
    drift_[1] = inv * dy;
}

bool cSC4WinRegionClouds::GZPaint() {
    for (int i = 0; i < kRegionCloudsTextureCount; ++i) {
        if (!textures_[i]) return true;
    }
    float seconds;
    if (!timer_.IsTimerRunning()) {
        timer_.Start();
        seconds = 0.0f;
    } else {
        const std::int32_t elapsed = static_cast<std::int32_t>(timer_.GetElapsedTimeU32());
        seconds = static_cast<float>(elapsed < 201 ? elapsed : 200) / 1000.0f;
        timer_.Restart();
    }
    IRegionCloudsDevice* device = g_host ? g_host->Device() : nullptr;
    if (!device) return true;
    device->SetState168(3);
    device->SetState168(0);
    device->SetState160(4);
    device->SetState168(1);
    device->SetState160(5);
    device->SetBlend(4, 5);
    device->SetState198(0);
    device->SetState1a0(0);
    device->SetState1a8(0, 3, 2, 1);
    // The device keeps this array: it is rewritten for every cloud.
    CloudVertex verts[4] = {{0, 0, 0, 0x20ffffff, 0, 0}, {0, 0, 0, 0x20ffffff, 1, 0}, {0, 0, 0, 0x20ffffff, 0, 1}, {0, 0, 0, 0x20ffffff, 1, 1}};
    device->SetVertices(10, 0, verts);
    if (random_.RandomDoubleUniform() < 0.003000000026077032) {
        cCloud cloud;
        InitCloud(cloud);
        clouds_.push_back(cloud);
    }
    for (auto it = clouds_.begin(); it != clouds_.end();) {
        if (area_[2] <= it->x) {
            it = clouds_.erase(it);
            continue;
        }
        const std::uint32_t color = (static_cast<std::uint32_t>(it->opacity) << 24) | 0x00ffffffu;
        for (auto& v : verts) v.color = color;
        const float x = it->x - static_cast<float>(offset_.x);
        const float y = it->y - static_cast<float>(offset_.y);
        verts[0].x = x;                 verts[0].y = y;
        verts[1].x = x + kCloudSize;    verts[1].y = y;
        verts[2].x = x;                 verts[2].y = y + kCloudSize;
        verts[3].x = x + kCloudSize;    verts[3].y = y + kCloudSize;
        device->SetTexture(textures_[it->texture]->TextureHandle(), 0);
        device->SetTextureState(0, 0, 1);
        device->SetTextureState(0, 1, 1);
        device->SetSamplerState(0, 0, 1);
        device->DrawPrimitive(1, 0, 4);
        it->x = it->vx * seconds + it->x;
        it->y = it->vy * seconds + it->y;
        ++it;
    }
    return true;
}

}  // namespace sc4
