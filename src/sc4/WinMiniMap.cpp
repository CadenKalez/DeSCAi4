// cSC4WinMiniMap — see WinMiniMap.h for the object layout, the vtable groups and the quirks kept.
//
// Compile without FMA contraction: every float operation below follows the original's single-precision order.
#include "sc4/WinMiniMap.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "gz/DrawContext.h"
#include "gz/cGZMessageServer.h"
#include "s3d/Camera.h"

namespace sc4 {
namespace {

IMiniMapHost* g_host = nullptr;

// The original inlines the add-12582912 round (ties to even) at 0x1005c6f1e and 0x1005c6f74.
int RoundToInt(float value) {
    const float magic = value + 12582912.0f;                       // 1.5 * 2^23
    return static_cast<int>(std::bit_cast<std::uint32_t>(magic)) - 0x4b400000;
}

// The image ops the block resampler calls through the table at 0x100ad12e0. Each one works on the caller's
// stride in bytes; the low two bits of a stride are dropped like the original does.
std::uint8_t* Advance(std::uint8_t* base, long stride) {
    return base + static_cast<std::uintptr_t>(stride & ~3L);
}
const std::uint8_t* Advance(const std::uint8_t* base, long stride) {
    return base + static_cast<std::uintptr_t>(stride & ~3L);
}

// 0x1005c75c4: a straight copy of `width` pixels per row (the block is already at map resolution).
void Downsample1(std::uint32_t* dst, long dstStride, const std::uint32_t* src, long srcStride, unsigned width,
                 unsigned height) {
    do {
        std::memcpy(dst, src, static_cast<std::size_t>(width) << 2);
        src = reinterpret_cast<const std::uint32_t*>(Advance(reinterpret_cast<const std::uint8_t*>(src), srcStride));
        dst = reinterpret_cast<std::uint32_t*>(Advance(reinterpret_cast<std::uint8_t*>(dst), dstStride));
        --height;
    } while (height != 0);
}

// 0x1005c74c0: each output pixel is the rounded average of a 2x2 source block.
void Downsample4(std::uint32_t* dst, long dstStride, const std::uint32_t* src, long srcStride, unsigned width,
                 unsigned height) {
    do {
        const std::uint8_t* second = Advance(reinterpret_cast<const std::uint8_t*>(src), srcStride);
        for (unsigned i = 0; i < width; ++i) {
            const std::uint32_t p0 = src[2 * i];
            const std::uint32_t p1 = src[2 * i + 1];
            const std::uint32_t p2 = *reinterpret_cast<const std::uint32_t*>(second + 8 * i);
            const std::uint32_t p3 = *reinterpret_cast<const std::uint32_t*>(second + 8 * i + 4);
            const std::uint32_t rb = ((p0 & 0xff00ff) + (p1 & 0xff00ff) + (p2 & 0xff00ff) + (p3 & 0xff00ff) + 0x20002) &
                                     0x3fc03fc;
            const std::uint32_t g = ((p0 & 0xff00) + (p1 & 0xff00) + (p2 & 0xff00) + (p3 & 0xff00) + 0x200) & 0x3fc00;
            dst[i] = (rb | g) >> 2;
        }
        src = reinterpret_cast<const std::uint32_t*>(
            Advance(reinterpret_cast<const std::uint8_t*>(src), srcStride * 2));
        dst = reinterpret_cast<std::uint32_t*>(Advance(reinterpret_cast<std::uint8_t*>(dst), dstStride));
        --height;
    } while (height != 0);
}

// 0x1005c722c: each output pixel is the rounded average of a 4x4 source block.
void Downsample16(std::uint32_t* dst, long dstStride, const std::uint32_t* src, long srcStride, unsigned width,
                  unsigned height) {
    const long rowStep = srcStride & ~3L;
    do {
        for (unsigned i = 0; i < width; ++i) {
            std::uint32_t rb = 0, g = 0;
            for (unsigned row = 0; row < 4; ++row) {
                const std::uint32_t* rowPtr =
                    reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(src) +
                                                           static_cast<std::ptrdiff_t>(row) * rowStep);
                for (unsigned col = 0; col < 4; ++col) {
                    const std::uint32_t p = rowPtr[4 * i + col];
                    rb += p & 0xff00ff;
                    g += p & 0xff00;
                }
            }
            dst[i] = (((rb + 0x80008) & 0xff00ff0) | ((g + 0x8000) & 0xff000)) >> 4;
        }
        src = reinterpret_cast<const std::uint32_t*>(Advance(reinterpret_cast<const std::uint8_t*>(src), srcStride * 4));
        dst = reinterpret_cast<std::uint32_t*>(Advance(reinterpret_cast<std::uint8_t*>(dst), dstStride));
        --height;
    } while (height != 0);
}

// 0x1005c761a: every source pixel becomes a 2x2 block (the map is coarser than the city cells).
void Upsample4(std::uint32_t* dst, long dstStride, const std::uint32_t* src, long srcStride, unsigned width,
               unsigned height) {
    const unsigned srcWidth = width >> 1;
    std::uint32_t* second = reinterpret_cast<std::uint32_t*>(Advance(reinterpret_cast<std::uint8_t*>(dst), dstStride));
    unsigned rows = height >> 1;
    do {
        for (unsigned i = 0; i < srcWidth; ++i) {
            const std::uint32_t value = src[i];
            dst[2 * i] = value;
            dst[2 * i + 1] = value;
            second[2 * i] = value;
            second[2 * i + 1] = value;
        }
        src = reinterpret_cast<const std::uint32_t*>(Advance(reinterpret_cast<const std::uint8_t*>(src), srcStride));
        dst = reinterpret_cast<std::uint32_t*>(Advance(reinterpret_cast<std::uint8_t*>(dst), dstStride * 2));
        second = reinterpret_cast<std::uint32_t*>(Advance(reinterpret_cast<std::uint8_t*>(second), dstStride * 2));
        --rows;
    } while (rows != 0);
}

// 0x1005c7691: every source pixel becomes a 4x4 block.
void Upsample16(std::uint32_t* dst, long dstStride, const std::uint32_t* src, long srcStride, unsigned width,
                unsigned height) {
    const unsigned srcWidth = width >> 2;
    std::uint8_t* rowBase = reinterpret_cast<std::uint8_t*>(dst);
    const long rowStep = dstStride & ~3L;
    unsigned rows = height >> 2;
    do {
        for (unsigned row = 0; row < 4; ++row) {
            std::uint32_t* line = reinterpret_cast<std::uint32_t*>(rowBase + static_cast<std::ptrdiff_t>(row) * rowStep);
            for (unsigned i = 0; i < srcWidth; ++i)
                for (unsigned col = 0; col < 4; ++col) line[4 * i + col] = src[i];
        }
        src = reinterpret_cast<const std::uint32_t*>(Advance(reinterpret_cast<const std::uint8_t*>(src), srcStride));
        rowBase = Advance(rowBase, dstStride * 4);
        --rows;
    } while (rows != 0);
}

// 0x1005c6266: copy a size x size image into a buffer, rotated by a quarter-turn count. The mapping the binary
// implements (destination (column, row) reads source at):
//   0: (col, row)   1: (size-1-row, col)   2: (size-1-col, size-1-row)   3: (row, size-1-col)
void RotateBlt(cIGZBuffer* target, const std::uint32_t* src, [[maybe_unused]] int srcStride, unsigned size,
               int rotation) {
    int stepX, stepY;
    switch (rotation) {
        case 3: stepX = -static_cast<int>(size); stepY = 1; break;
        case 2: stepX = -1; stepY = -static_cast<int>(size); break;
        default:
            stepX = 1;
            stepY = static_cast<int>(size);
            if (rotation == 1) { stepY = -1; stepX = static_cast<int>(size); }
            break;
    }
    if (!target->Lock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly))) return;                       // 0x8040
    std::uint32_t* bits = static_cast<std::uint32_t*>(target->GetColorSurfaceBits());
    const std::uint32_t stride = target->GetColorSurfaceStride();
    if (size != 0) {
        const int last = static_cast<int>(size) - 1;
        const std::ptrdiff_t start =
            -static_cast<std::ptrdiff_t>(((stepX >> 31) & last) * stepX + ((stepY >> 31) & last) * stepY);
        const std::uint32_t* rowStart = src + start;
        for (unsigned row = 0; row < size; ++row) {
            const std::uint32_t* p = rowStart;
            for (unsigned col = 0; col < size; ++col) {
                bits[col] = *p | 0xff000000u;
                p += stepX;
            }
            bits = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(bits) + stride);
            rowStart += stepY;
        }
    }
    target->Unlock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly));
}

// The image-op table indexed by zoomShift + 2 (0x100ad12e0).
using ImageOp = void (*)(std::uint32_t*, long, const std::uint32_t*, long, unsigned, unsigned);
ImageOp ImageOpForShift(int shift) {
    switch (shift) {
        case -2: return &Upsample16;
        case -1: return &Upsample4;
        case 0: return &Downsample1;
        case 1: return &Downsample4;
        case 2: return &Downsample16;
        default: return nullptr;
    }
}

// The rotation a minimap row/column pair reads from a source image with `stride` pixels per row:
// destination (column, row) reads source at base + column * stepX + row * stepY.
void RotationSteps(int rotation, int size, int stride, int& base, int& stepX, int& stepY) {
    switch (rotation) {
        case 1: base = size - 1; stepX = stride; stepY = -1; break;
        case 2: base = (size - 1) + (size - 1) * stride; stepX = -1; stepY = -stride; break;
        case 3: base = (size - 1) * stride; stepX = -stride; stepY = 1; break;
        default: base = 0; stepX = 1; stepY = stride; break;
    }
}

// Inlined in GZPaint in the original; the table is the anonymous namespace's ditherMat at 0x100a25290.
constexpr std::uint32_t kDitherMatrix[16] = {0x0000000, 0x8000000, 0x2000000, 0xa000000, 0xc000000, 0x4000000,
                                             0xe000000, 0x6000000, 0x3000000, 0xb000000, 0x1000000, 0x9000000,
                                             0xf000000, 0x7000000, 0xd000000, 0x5000000};

}  // namespace

IMiniMapHost* MiniMapHost() { return g_host; }
void SetMiniMapHost(IMiniMapHost* host) { g_host = host; }

// ---------------------------------------------------------------------------------------------------------------
// cTZMap: the picture the terrain/zone build works in (+0x1a8).
// ---------------------------------------------------------------------------------------------------------------
bool cSC4WinMiniMap::cTZMap::Init(int width, int height) {   // 0x1005c5b3a
    if (pixels) {
        if (this->width == width && this->height == height) return true;
        delete[] pixels;
    }
    pixels = nullptr;
    this->width = width;
    this->height = height;
    pixels = new std::uint32_t[static_cast<std::size_t>(width) * static_cast<std::size_t>(height)];
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Construction / destruction / the interfaces
// ---------------------------------------------------------------------------------------------------------------
cSC4WinMiniMap::cSC4WinMiniMap(cIGZWinMgr* mgr) : cGZWin(mgr) {   // 0x1005c4664
    zoneColors_.assign(kMiniMapZoneColorCount, 0);
    // The original leaves +0x18c (the zoom shift) alone until InitTZMap; the port defaults it to 0.
}

cSC4WinMiniMap::~cSC4WinMiniMap() {   // 0x1005c474c
    delete[] map_.pixels;
    map_.pixels = nullptr;
    if (overlayBuffer_) {
        overlayBuffer_->Release();
        overlayBuffer_ = nullptr;
    }
    if (mapBuffer_) {
        mapBuffer_->Release();
        mapBuffer_ = nullptr;
    }
}

bool cSC4WinMiniMap::QueryInterface(std::uint32_t iid, void** out) {   // 0x1005c48c6
    if (iid == kMiniMapIID) {                                          // 0xca318385: the +0x150 view
        *out = static_cast<cISC4WinMiniMap*>(this);
        cGZWin::AddRef();
        return true;
    }
    if (iid == 0xe98b2f57 || iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZWin) {
        *out = static_cast<cIGZWin*>(this);
        cGZWin::AddRef();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Life cycle
// ---------------------------------------------------------------------------------------------------------------
bool cSC4WinMiniMap::Init() {   // 0x1005c4946
    if (get_init_flag()) return true;
    IMiniMapHost* host = MiniMapHost();
    cIGZBuffer* buffer = host ? host->CreateBuffer() : nullptr;
    if (mapBuffer_) {
        mapBuffer_->Release();
        mapBuffer_ = nullptr;
    }
    if (!buffer) return false;
    mapBuffer_ = buffer;
    if (!buffer->Init(static_cast<std::uint32_t>(mapSize_), static_cast<std::uint32_t>(mapSize_),
                      cGZBufferColorType{kBufferColorARGB8888, 32})) {
        mapBuffer_ = nullptr;
        buffer->Release();
        return false;
    }
    if (buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadWrite))) {
        const std::uint32_t color = buffer->ConvertRGBValueToNative(100, 100, 0x7f);
        buffer->Fill(color, nullptr);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadWrite));
    }
    if (!cGZWin::Init()) {
        Shutdown();
        return false;
    }
    if (cIGZMessageServer2* server = host ? host->MessageServer() : nullptr) {
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        server->AddNotification(target, kMiniMapMsgTerrainChanged1);
        server->AddNotification(target, kMiniMapMsgTerrainChanged2);
        server->AddNotification(target, kMiniMapMsgTerrainChanged3);
        server->AddNotification(target, kMiniMapMsgRectInvalid);
        server->AddNotification(target, kMiniMapMsgCameraZoomRotationDone);
        server->AddNotification(target, kMiniMapMsgCameraTargetDone);
        server->AddNotification(target, kMiniMapMsgCityInit);
        server->AddNotification(target, kMiniMapMsgCityShutdown);
        server->AddNotification(target, kMiniMapMsgReloadTunables);
    }
    std::memset(terrainRamp_, 0, sizeof(terrainRamp_));
    std::memset(waterRamp_, 0, sizeof(waterRamp_));
    SetDefaultZoneColor(0xe57300);   // vslot 0x4e0
    SetZoneColor(1, 0xec00);
    SetZoneColor(2, 0xec00);
    SetZoneColor(3, 0xec00);
    SetZoneColor(4, 0x7979ff);
    SetZoneColor(5, 0x7979ff);
    SetZoneColor(6, 0x7979ff);
    SetZoneColor(7, 0xc7bc00);
    SetZoneColor(8, 0xc7bc00);
    SetZoneColor(9, 0xc7bc00);
    LoadTunableValues();
    OnCityInit();
    return true;
}

bool cSC4WinMiniMap::Shutdown() {   // 0x1005c4e66
    if (get_init_flag()) {
        UnhookOccupantMessages();
        if (IMiniMapHost* host = MiniMapHost()) {
            if (cIGZMessageServer2* server = host->MessageServer()) {
                cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
                server->RemoveNotification(target, kMiniMapMsgReloadTunables);
                server->RemoveNotification(target, kMiniMapMsgCityShutdown);
                server->RemoveNotification(target, kMiniMapMsgCityInit);
                server->RemoveNotification(target, kMiniMapMsgCameraZoomRotationDone);
                server->RemoveNotification(target, kMiniMapMsgCameraTargetDone);
                server->RemoveNotification(target, kMiniMapMsgRectInvalid);
                server->RemoveNotification(target, kMiniMapMsgTerrainChanged3);
                server->RemoveNotification(target, kMiniMapMsgTerrainChanged2);
                server->RemoveNotification(target, kMiniMapMsgTerrainChanged1);
            }
        }
        if (mapBuffer_) {
            mapBuffer_->Shutdown();
            mapBuffer_->Release();
            mapBuffer_ = nullptr;
        }
        cGZWin::Shutdown();
    }
    return true;
}

void cSC4WinMiniMap::LoadTunableValues() {   // 0x1005c4c5c
    IMiniMapHost* host = MiniMapHost();
    IMiniMapRamp* terrain = host ? host->FindRamp(kMiniMapTerrainRampProperty) : nullptr;
    if (terrain) {
        if (host) host->InterpolateRamp(terrainRamp_, 0x100, terrain->Data(), terrain->Count());
        for (std::uint32_t& entry : terrainRamp_) entry &= 0xffffff;   // alpha cleared, the shade supplies it
    }
    IMiniMapRamp* water = host ? host->FindRamp(kMiniMapWaterRampProperty) : nullptr;
    if (water && host) host->InterpolateRamp(waterRamp_, 0x100, water->Data(), water->Count());
    std::memset(dirtyBlocks_, 0xff, sizeof(dirtyBlocks_));
    terrainDirty_ = true;    // *(u16*)(this + 0x185) = 0x101
    needsUpdate_ = true;
}

void cSC4WinMiniMap::OnCityInit() {   // 0x1005c4dbc
    if (cityInitialised_) return;
    IMiniMapHost* host = MiniMapHost();
    if (!host) return;
    cityInitialised_ = true;
    InitTZMap();
    cityCellsX_ = host->CityCellCountX();
    cityCellsZ_ = host->CityCellCountZ();
    const long bits = static_cast<long>(cityCellsX_) * static_cast<long>(cityCellsZ_) * 2;
    cellFlags_.resize(static_cast<std::size_t>((bits + 0x1f) >> 5));
    host->AddTickNotification(static_cast<cIGZMessageTarget2*>(this));
    HookOccupantMessages();
}

void cSC4WinMiniMap::OnCityShutdown() {   // 0x1005c568a
    if (!cityInitialised_) return;
    UnhookOccupantMessages();
    if (IMiniMapHost* host = MiniMapHost()) host->RemoveTickNotification(static_cast<cIGZMessageTarget2*>(this));
    delete[] map_.pixels;
    map_.pixels = nullptr;
    cityInitialised_ = false;
    cityCellsX_ = 0;   // the original clears the qword at +0x168 (the two cell counts)
    cityCellsZ_ = 0;
}

void cSC4WinMiniMap::HookOccupantMessages() {   // 0x1005c5acc
    if (occupantMessagesHooked_ || !cityInitialised_) return;
    if ((flags_ & 2) != 0) {
        if (IMiniMapHost* host = MiniMapHost()) {
            if (cIGZMessageServer2* server = host->MessageServer()) {
                cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
                server->AddNotification(target, kMiniMapMsgOccupantInserted);
                server->AddNotification(target, kMiniMapMsgOccupantRemoved);
            }
        }
    }
    occupantMessagesHooked_ = true;
}

void cSC4WinMiniMap::UnhookOccupantMessages() {   // 0x1005c4f76
    if (!occupantMessagesHooked_) return;
    if ((flags_ & 2) != 0) {
        if (IMiniMapHost* host = MiniMapHost()) {
            if (cIGZMessageServer2* server = host->MessageServer()) {
                cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
                server->RemoveNotification(target, kMiniMapMsgOccupantInserted);
                server->RemoveNotification(target, kMiniMapMsgOccupantRemoved);
            }
        }
    }
    occupantMessagesHooked_ = false;
}

// ---------------------------------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------------------------------
void cSC4WinMiniMap::SetArea(int l, int t, int r, int b) {   // 0x1005c4fda
    cGZWin::SetArea(l, t, r, b);
    unsigned size = static_cast<unsigned>(std::min(r - l, b - t));
    unsigned power = size;
    while (size != 0) {   // largest power of two <= size
        power = size;
        size = (power - 1) & power;
    }
    mapSize_ = static_cast<int>(power);
    // Quirk kept: OnCityInit only does work while the city is not initialised, and this call only happens once
    // the terrain map exists, i.e. after OnCityInit has set the flag (0x1005c5019).
    if (map_.pixels) OnCityInit();
}

void cSC4WinMiniMap::CenterOnMapPoint(int x, int y) {   // 0x1005c506e
    IMiniMapHost* host = MiniMapHost();
    if (!host) return;
    IMiniMapRenderer* renderer = host->Renderer();
    IMiniMapCameraControl* camera = renderer ? renderer->CameraControl() : nullptr;
    if (!camera) return;
    const int rotation = camera->Rotation();   // camera control +0x114
    const float ax = static_cast<float>(x) / static_cast<float>(GetW()) - 0.5f;
    const float by = static_cast<float>(y) / static_cast<float>(GetH()) - 0.5f;
    float u, v;
    if ((rotation & 1) != 0) {
        u = -by;
        v = ax;
    } else {
        u = ax;
        v = -by;
    }
    if ((rotation & 2) != 0) {
        u = -u;
        v = -v;
    }
    u = std::min(u + 0.5f, 1.0f);
    u = std::max(u, 0.0f);
    v = std::min(v + 0.5f, 1.0f);
    v = std::max(v, 0.0f);
    cS3DVector3 position;
    position.x = host->CitySizeX() * u;
    position.z = host->CitySizeZ() * v;
    position.y = 0.0f;
    if (IMiniMapTerrain* terrain = host->Terrain()) position.y = terrain->Altitude(position.x, position.z);
    camera->SetViewTargetPosition(position);
}

bool cSC4WinMiniMap::GZOnMouseDownL(int x, int y, std::uint32_t) {   // 0x1005c5048
    CenterOnMapPoint(x, y);
    dragging_ = true;
    GZSetCapture();
    return true;
}

bool cSC4WinMiniMap::GZOnMouseUpL(int, int, std::uint32_t) {   // 0x1005c520e
    dragging_ = false;
    GZReleaseCapture();
    return true;
}

bool cSC4WinMiniMap::GZOnMouseMove(int x, int y, std::uint32_t) {   // 0x1005c5222
    if (dragging_) CenterOnMapPoint(x, y);
    return true;
}

bool cSC4WinMiniMap::GZOnMouseEnter(cIGZWin*) {   // 0x1005c5238
    dragging_ = false;
    return true;
}

bool cSC4WinMiniMap::GZOnMouseExit(cIGZWin*) {   // 0x1005c5248
    dragging_ = false;
    return true;
}

bool cSC4WinMiniMap::GZOnMouseDownR(int, int, std::uint32_t) { return true; }   // 0x1005c5258

void cSC4WinMiniMap::InvalidateTZMapRect(const cRZRect& rect) {   // 0x1005c56ea
    int x0 = rect.left >> 4, x1 = rect.right >> 4;
    int y0 = rect.top >> 4, y1 = rect.bottom >> 4;
    if (x1 > 15) x1 = 15;
    if (y1 > 15) y1 = 15;
    // The original clamps only the far edges and indexes with the near ones as they are (0x1005c56ea); a
    // negative rectangle corner would read before dirtyBlocks_, so the port drops such blocks.
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) dirtyBlocks_[y] |= 1u << (x & 31);
    terrainDirty_ = true;
    needsUpdate_ = true;
}

void cSC4WinMiniMap::InitTZMap() {   // 0x1005c5a08
    IMiniMapHost* host = MiniMapHost();
    if (!host) return;
    const int cellsX = host->CityCellCountX();
    const int cellsZ = host->CityCellCountZ();
    if (cellsX < 1 || cellsZ < 1) return;
    map_.Init(mapSize_, mapSize_);
    zoomShift_ = 0;
    if (cellsX > mapSize_) {
        int shift = 0;
        int value = cellsX;
        do {
            value >>= 1;
            ++shift;
        } while (value > mapSize_);
        zoomShift_ = shift;
    } else if (cellsX < mapSize_) {
        int shift = 0;
        int value = cellsX;
        do {
            value += value;
            --shift;
        } while (value < mapSize_);
        zoomShift_ = shift;
    }
    std::memset(dirtyBlocks_, 0xff, sizeof(dirtyBlocks_));
    terrainDirty_ = true;
    needsUpdate_ = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Zone colours and flags
// ---------------------------------------------------------------------------------------------------------------
void cSC4WinMiniMap::SetDefaultZoneColorToTrans() {   // 0x1005c5842
    std::fill(zoneColors_.begin(), zoneColors_.end(), 0u);
}

void cSC4WinMiniMap::SetDefaultZoneColor(std::uint32_t color) {   // 0x1005c58d4
    const std::uint32_t opaque = color | 0xff000000;
    std::fill(zoneColors_.begin(), zoneColors_.end(), opaque);
    if (!zoneColors_.empty()) zoneColors_[0] = 0;   // zone 0 ("none") stays transparent
}

void cSC4WinMiniMap::SetZoneColor(std::uint32_t index, std::uint32_t color) {   // 0x1005c5956
    if (index >= zoneColors_.size()) return;
    zoneColors_[index] = color == 0 ? 0u : (color | 0xff000000);
}

std::uint32_t cSC4WinMiniMap::SetFlags(std::uint32_t mask, std::uint32_t value) {   // 0x1005c599a
    UnhookOccupantMessages();
    flags_ = (~mask & flags_) ^ value;
    std::memset(dirtyBlocks_, 0xff, sizeof(dirtyBlocks_));
    terrainDirty_ = true;
    needsUpdate_ = true;
    return flags_;
}

void cSC4WinMiniMap::SetOverlay(cIGZBuffer* overlay) {   // 0x1005c57a6
    if (overlayBuffer_ != overlay) {
        if (overlay) overlay->AddRef();
        cIGZBuffer* previous = overlayBuffer_;
        overlayBuffer_ = overlay;
        if (previous) previous->Release();
    }
    needsUpdate_ = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------------------------------------------
bool cSC4WinMiniMap::DoMessage(cIGZMessage2* message) {   // 0x1005c5260
    const std::uint32_t type = message->GetType();
    cIGZMessage2Standard* standard = static_cast<cIGZMessage2Standard*>(message);
    switch (type) {
        case kMiniMapMsgTick: {
            for (cIGZWin* window = this; window != nullptr; window = window->GetParentWin()) {
                if (!window->IsVisible()) {   // the minimap is hidden: drop the picture until it returns
                    delete[] map_.pixels;
                    map_.pixels = nullptr;
                    return true;
                }
            }
            const float elapsed = std::bit_cast<float>(static_cast<std::uint32_t>(standard->GetData3()));
            tickAccumulator_ += elapsed;
            if (tickAccumulator_ > kMiniMapTickPeriodMs) {
                tickAccumulator_ = 0.0f;
                Update();
                if ((flags_ & 0x10) != 0) {
                    IMiniMapHost* host = MiniMapHost();
                    IMiniMapSituation* situation = host ? host->CitySituationManager() : nullptr;
                    if (situation && situation->HasActiveSituation()) repaintRequested_ = true;
                }
            }
            if (repaintRequested_) {
                InvalidateSelfAndParents();
                repaintRequested_ = false;
            }
            return true;
        }
        case kMiniMapMsgTerrainChanged1:
        case kMiniMapMsgTerrainChanged2:
        case kMiniMapMsgTerrainChanged3: {
            const std::uint64_t data1 = standard->GetData1();
            const std::uint64_t data2 = standard->GetData2();
            cRZRect rect;
            rect.left = static_cast<std::int32_t>(data1 >> 16);
            rect.top = static_cast<std::int32_t>(data2 >> 16);
            rect.right = static_cast<std::int32_t>(static_cast<std::uint16_t>(data1));
            rect.bottom = static_cast<std::int32_t>(static_cast<std::uint16_t>(data2));
            InvalidateTZMapRect(rect);
            return true;
        }
        case kMiniMapMsgRectInvalid: {
            const auto* rect = static_cast<const cRZRect*>(standard->GetVoid2());
            if (rect) InvalidateTZMapRect(*rect);
            return true;
        }
        case kMiniMapMsgCameraZoomRotationDone:
        case kMiniMapMsgCameraTargetDone:
            repaintRequested_ = true;
            return true;
        case kMiniMapMsgReloadTunables:
            if (static_cast<std::uint32_t>(standard->GetData1()) == kMiniMapTunableGroup &&
                static_cast<std::uint32_t>(standard->GetData2()) == kMiniMapTunableInstance)
                LoadTunableValues();
            return true;
        case kMiniMapMsgCityInit:
            OnCityInit();
            Update();
            InvalidateSelfAndParents();
            return true;
        case kMiniMapMsgCityShutdown:
            OnCityShutdown();
            return true;
        case kMiniMapMsgOccupantInserted:
        case kMiniMapMsgOccupantRemoved: {
            cIGZUnknown* object = static_cast<cIGZUnknown*>(standard->GetVoid1());
            const std::uint32_t occupantType = static_cast<std::uint32_t>(standard->GetData3());
            if (object && occupantType == kMiniMapNetworkOccupantType) {
                IMiniMapHost* host = MiniMapHost();
                int cellX = 0, cellZ = 0;
                if (host && host->NetworkOccupantCell(object, cellX, cellZ) &&
                    static_cast<std::uint32_t>(cellX) < static_cast<std::uint32_t>(cityCellsX_) &&
                    static_cast<std::uint32_t>(cellZ) < static_cast<std::uint32_t>(cityCellsZ_)) {
                    const std::uint32_t cell =
                        static_cast<std::uint32_t>(cellZ) * static_cast<std::uint32_t>(cityCellsX_) +
                        static_cast<std::uint32_t>(cellX);
                    const unsigned shift = (cell * 2) & 0x1e;
                    if (cell / 16 < cellFlags_.size()) {
                        std::uint32_t& word = cellFlags_[cell / 16];
                        // rotate_left(0xfffffffe, shift): clear the "queried" bit of the cell's pair, keep the rest
                        word &= shift == 0 ? 0xfffffffeu
                                           : ((0xfffffffeu << shift) | (0xfffffffeu >> (32 - shift)));
                    }
                    if (type == kMiniMapMsgOccupantRemoved) {
                        InvalidateTZMapRect(cRZRect{cellX, cellZ, cellX, cellZ});
                    } else {
                        needsUpdate_ = true;
                    }
                }
            }
            return true;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The picture
// ---------------------------------------------------------------------------------------------------------------
void cSC4WinMiniMap::Update() {   // 0x1005c5ba4
    for (cIGZWin* window = this; window != nullptr; window = window->GetParentWin()) {
        if (!window->IsVisible()) {
            delete[] map_.pixels;
            map_.pixels = nullptr;
            break;
        }
    }
    if (!map_.pixels) return;
    if (!needsUpdate_) return;
    IMiniMapHost* host = MiniMapHost();
    if (!host) return;
    const int cityCellsX = host->CityCellCountX();
    const int cityCellsZ = host->CityCellCountZ();
    IMiniMapRenderer* renderer = host->Renderer();
    IMiniMapCameraControl* cameraControl = renderer ? renderer->CameraControl() : nullptr;
    const int rotation = cameraControl ? cameraControl->Rotation() : 0;

    if (overlayBuffer_ != nullptr && (flags_ & 8) != 0) {
        // The overlay replaces the picture: rotate-blit its pixels straight into the map buffer.
        if (overlayBuffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly))) {
            const auto* overlayBits = static_cast<const std::uint32_t*>(overlayBuffer_->GetColorSurfaceBits());
            const std::uint32_t overlayStride = overlayBuffer_->GetColorSurfaceStride() >> 2;
            if (mapBuffer_)
                RotateBlt(mapBuffer_, overlayBits, static_cast<int>(overlayStride), static_cast<unsigned>(mapSize_),
                          rotation);
            overlayBuffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly));
        }
    } else {
        if (terrainDirty_) UpdateTerrainAndZoneMap();
        if ((flags_ & 2) != 0) {
            // Occupant pass: paint a network colour on every cell that holds a surface-network occupant and
            // cache the answer in the per-cell flag vector.
            IMiniMapOccupantQuery* query = host->CreateSurfaceNetworkOccupantQuery();
            std::uint32_t* pixels = map_.pixels;
            int index = 0;
            for (int z = 0; z < cityCellsZ; ++z) {
                for (int x = 0; x < cityCellsX; ++x, ++index) {
                    const std::uint32_t cell = static_cast<std::uint32_t>(index);
                    const std::size_t wordIndex = cell / 16;
                    if (wordIndex >= cellFlags_.size()) continue;
                    const unsigned shift = (cell * 2) & 0x1e;
                    const std::uint32_t queriedBit = 1u << shift;
                    const std::uint32_t occupiedBit = queriedBit << 1;
                    std::uint32_t& word = cellFlags_[wordIndex];
                    if ((word & queriedBit) == 0) {
                        cIGZUnknown* occupant = nullptr;
                        word &= ~occupiedBit;
                        if (query != nullptr && query->GetFirstOccupant(x, z, occupant)) {
                            word |= occupiedBit;
                            occupant->Release();
                        }
                        word |= queriedBit;
                    }
                    if ((word & occupiedBit) != 0) {
                        const int shiftDown = zoomShift_ + 4;
                        const int pixelX = (x << 4) >> shiftDown;
                        const int pixelY = ((z << 4) >> shiftDown) * map_.width;
                        pixels[pixelY + pixelX] = kMiniMapNetworkCellColor;
                    }
                }
            }
            if (query) query->Release();
        }
        if (mapBuffer_) {
            if (overlayBuffer_ == nullptr) {
                RotateBlt(mapBuffer_, map_.pixels, map_.width, static_cast<unsigned>(mapSize_), rotation);
            } else if (overlayBuffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly))) {
                // Composite the overlay over the terrain picture into the map buffer (the original locks both
                // buffers and applies the same rotation mapping to each source).
                const auto* overlayBits = static_cast<const std::uint32_t*>(overlayBuffer_->GetColorSurfaceBits());
                const int overlayStride = static_cast<int>(overlayBuffer_->GetColorSurfaceStride() >> 2);
                if (mapBuffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly))) {
                    auto* dstBits = static_cast<std::uint32_t*>(mapBuffer_->GetColorSurfaceBits());
                    const std::uint32_t dstStride = mapBuffer_->GetColorSurfaceStride();
                    if (mapSize_ > 0) {
                        int tzBase, tzStepX, tzStepY;
                        int ovBase, ovStepX, ovStepY;
                        RotationSteps(rotation, mapSize_, map_.width, tzBase, tzStepX, tzStepY);
                        RotationSteps(rotation, mapSize_, overlayStride, ovBase, ovStepX, ovStepY);
                        std::uint8_t* rowBytes = reinterpret_cast<std::uint8_t*>(dstBits);
                        for (int row = 0; row < mapSize_; ++row) {
                            auto* out = reinterpret_cast<std::uint32_t*>(rowBytes);
                            for (int col = 0; col < mapSize_; ++col) {
                                const std::uint32_t overlayPixel = overlayBits[ovBase + col * ovStepX + row * ovStepY];
                                const std::uint32_t terrainPixel = map_.pixels[tzBase + col * tzStepX + row * tzStepY];
                                const std::uint32_t alpha = overlayPixel >> 24;
                                const std::uint32_t inverse = 0x100 - alpha;
                                std::uint32_t r = ((((terrainPixel & 0xff0000) * inverse + 0x800000) >> 8) +
                                                   (overlayPixel & 0xff0000)) & 0x1ff0000;
                                std::uint32_t g = ((((terrainPixel & 0xff00) * inverse + 0x8000) >> 8) +
                                                   (overlayPixel & 0xff00)) & 0x7ff00;
                                std::uint32_t b = (((terrainPixel & 0xff) * inverse + 0x80) >> 8) + (overlayPixel & 0xff);
                                if (r > 0xff0000) r = 0xff0000;
                                if (g > 0xff00) g = 0xff00;
                                if (b > 0xff) b = 0xff;
                                out[col] = r | g | b | 0xff000000u;
                            }
                            rowBytes += dstStride;
                        }
                    }
                    mapBuffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly));
                }
                overlayBuffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly));
            }
        }
    }
    needsUpdate_ = false;
    InvalidateSelfAndParents();
    repaintRequested_ = false;
    HookOccupantMessages();
}

void cSC4WinMiniMap::UpdateTerrainAndZoneMap() {   // 0x1005c637a
    IMiniMapHost* host = MiniMapHost();
    IMiniMapTerrain* terrain = host ? host->Terrain() : nullptr;
    if (!terrain) return;
    const int cellsX = terrain->CellCountX();
    const int cellsZ = terrain->CellCountZ();
    const float seaLevel = terrain->SeaLevel();
    IMiniMapRenderer* renderer = host->Renderer();
    IMiniMapCameraControl* cameraControl = renderer ? renderer->CameraControl() : nullptr;
    const int rotation = cameraControl ? cameraControl->Rotation() : 0;
    // The light direction is the rotated (-1, 1, -1) / sqrt(3); note the X coefficient is scaled by 63.75
    // while both other terms use 36.80608 (0x1005c63ff..0x1005c646d).
    float lightX = ((rotation & 1) != 0) ? 0.57735026f : -0.57735026f;
    if ((rotation & 2) != 0) lightX = -lightX;
    const float lightZ = ((rotation & 2) != 0) ? 36.80608f : -36.80608f;
    const float lightXScaled = lightX * 63.75f;
    IMiniMapZoneGrid* zoneGrid = host->ZoneGrid();
    const int blocksX = cellsX >> 4;
    const int blocksZ = cellsZ >> 4;
    for (int blockZ = 0; blockZ < blocksZ; ++blockZ) {
        for (int blockX = 0; blockX < blocksX; ++blockX) {
            if ((dirtyBlocks_[blockZ] & (1u << blockX)) == 0) continue;
            const cRZRect rect{blockX << 4, blockZ << 4, (blockX << 4) | 15, (blockZ << 4) | 15};
            float altitudes[256];
            float depths[256];
            terrain->Altitudes(rect, altitudes, 16, 1);
            terrain->WaterDepths(rect, depths, 16, 1);
            std::uint32_t block[256];
            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 16; ++col) {
                    const int index = row * 16 + col;
                    const float relative = altitudes[index] - seaLevel;
                    const float scaled = relative * 0.2f;
                    // floor(scaled) + 64, clamped to the 256-entry terrain ramp.
                    int rampIndex = static_cast<int>(std::floor(scaled)) + 0x40;
                    rampIndex = std::clamp(rampIndex, 0, 255);
                    const std::uint32_t color = terrainRamp_[rampIndex];
                    // Below sea level the original uses the fixed normal (0, 1, 0) (0x1005c6689..0x1005c669b).
                    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
                    if (relative >= 0.0f) {
                        cS3DVector3 normal;
                        terrain->VertexNormal((blockX << 4) + col, (blockZ << 4) + row, normal);
                        const float scaledY = normal.y * 0.4f;
                        const float length =
                            std::sqrt(normal.x * normal.x + scaledY * scaledY + normal.z * normal.z);
                        const float inverse = 1.0f / length;
                        nx = normal.x * inverse;
                        ny = scaledY * inverse;
                        nz = normal.z * inverse;
                    }
                    const float shade = nx * lightXScaled + ny * 36.80608f + nz * lightZ;
                    const int shadeIndex = RoundToInt(std::fabs(shade) + shade + 127.75f);
                    std::uint32_t shaded = (((color & 0xff00ff) * shadeIndex + 0x800080) & 0xff00ff00) |
                                           (((color & 0xff00) * shadeIndex + 0x8000) & 0xff0000);
                    shaded >>= 8;
                    const float depth = depths[index];
                    int waterIndex = std::isfinite(depth) ? static_cast<int>(std::floor(depth)) : 0;
                    waterIndex = std::clamp(waterIndex, 0, 255);
                    const std::uint32_t water = waterRamp_[waterIndex];
                    const std::uint32_t alpha = water >> 24;
                    if (alpha != 0) {
                        const std::uint32_t inverse = 0x100 - alpha;
                        std::uint32_t r = ((((shaded & 0xff0000) * inverse + 0x800000) >> 8) + (water & 0xff0000)) &
                                          0x1ff0000;
                        std::uint32_t g =
                            ((((shaded & 0xff00) * inverse + 0x8000) >> 8) + (water & 0xff00)) & 0x7ff00;
                        std::uint32_t b = (((shaded & 0xff) * inverse + 0x80) >> 8) + (water & 0xff);
                        if (r > 0xff0000) r = 0xff0000;
                        if (g > 0xff00) g = 0xff00;
                        if (b > 0xff) b = 0xff;
                        shaded = r | g | b;
                    }
                    block[index] = shaded;
                }
            }
            if ((flags_ & 1) != 0 && zoneGrid) {
                // The zone pass walks columns in the outer loop (0x1005c68bc): block element index is
                // row * 16 + col with the zone read at (x = blockX*16 + col, z = blockZ*16 + row).
                for (int col = 0; col < 16; ++col) {
                    for (int row = 0; row < 16; ++row) {
                        const int zone = zoneGrid->ZoneAt((blockX << 4) + col, (blockZ << 4) + row);
                        if (zone < 0 || zone >= kMiniMapZoneColorCount) continue;
                        std::uint32_t& pixel = block[row * 16 + col];
                        const std::uint32_t zoneColor = zoneColors_[static_cast<std::size_t>(zone)];
                        if (pixel < 0x1000000 && zoneColor != 0) pixel = zoneColor;
                    }
                }
            }
            const int shift = zoomShift_ + 4;
            if (ImageOp op = ImageOpForShift(zoomShift_)) {
                const unsigned size = static_cast<unsigned>(0x100 >> (shift & 31));
                std::uint32_t* dst = map_.pixels + ((blockZ * 256) >> shift) * map_.width + ((blockX * 256) >> shift);
                op(dst, static_cast<long>(mapSize_) << 2, block, 0x40, size, size);
            }
        }
    }
    terrainDirty_ = false;
    std::memset(dirtyBlocks_, 0, sizeof(dirtyBlocks_));
}

bool cSC4WinMiniMap::GZPaint() {   // 0x1005c6a38
    if (cityInitialised_ && map_.pixels == nullptr) {
        InitTZMap();
        Update();
    }
    auto* context = static_cast<gz::cIGZDrawContext*>(GetDrawContext());
    if (!context) return true;
    gz::cGZBufferSurfaceDesc desc;
    const bool locked = context->Lock(desc, false);
    if (locked) {
        if (mapBuffer_) {
            if (desc.format == 7) {
                // The target is ARGB4444: write the map into the locked target surface with the dither table.
                if (mapBuffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly))) {
                    const auto* src = static_cast<const std::uint32_t*>(mapBuffer_->GetColorSurfaceBits());
                    const std::uint32_t srcStride = mapBuffer_->GetColorSurfaceStride();
                    const int width = accumulatedArea_.right - accumulatedArea_.left;
                    const int height = accumulatedArea_.bottom - accumulatedArea_.top;
                    if (width > 0 && height > 0) {
                        auto* dstRow = reinterpret_cast<std::uint8_t*>(desc.pixels) +
                                       static_cast<std::ptrdiff_t>(accumulatedArea_.top) * desc.pitch +
                                       static_cast<std::ptrdiff_t>(accumulatedArea_.left) * 2;
                        const unsigned ditherY = static_cast<unsigned>(height) & 3u;
                        for (int row = 0; row < height; ++row) {
                            auto* out = reinterpret_cast<std::uint16_t*>(dstRow);
                            for (int i = 0; i < width; ++i) {
                                const std::uint32_t pixel = src[i];
                                const std::uint32_t dither =
                                    kDitherMatrix[ditherY * 4u + (static_cast<unsigned>(width - i) & 3u)];
                                const std::uint32_t r = ((((pixel >> 16) & 0xff) * 0xf1f1f1u + dither) >> 20) & 0xfu;
                                const std::uint32_t g = ((((pixel >> 8) & 0xff) * 0xf1f1f1u + dither) >> 24) & 0xf0u;
                                const std::uint32_t b = (((pixel & 0xff) * 0xf1f1f1u + dither) >> 28) & 0xfu;
                                out[i] = static_cast<std::uint16_t>(0xf000u | (r << 8) | g | b);
                            }
                            dstRow += desc.pitch;
                            src = reinterpret_cast<const std::uint32_t*>(
                                reinterpret_cast<const std::uint8_t*>(src) + srcStride);
                        }
                    }
                    mapBuffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockReadOnly));
                }
            } else if (mapBuffer_->Lock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockWriteOnly))) {
                // The target is a wider format: blit the map over the draw area. The original calls the draw
                // buffer's Blt (0xf8) with the draw area as the clip; the port's draw context performs the
                // same blit into the buffer it is attached to.
                const cRZRect* area = mapBuffer_->GetBufferArea();
                const int left = drawArea_.left, top = drawArea_.top;
                const cRZRect destination{left, top, left + mapSize_, top + mapSize_};
                if (area) context->Blt(mapBuffer_, *area, destination);
                mapBuffer_->Unlock(static_cast<cIGZBuffer::eLockFlags>(kMiniMapLockWriteOnly));
            }
        }
        context->Unlock();
    }

    IMiniMapHost* host = MiniMapHost();
    IMiniMapRenderer* renderer = host ? host->Renderer() : nullptr;
    IMiniMapCameraControl* cameraControl = renderer ? renderer->CameraControl() : nullptr;
    const int rotation = cameraControl ? cameraControl->Rotation() : 0;

    if ((flags_ & 4) != 0) {
        // The camera's view rectangle: project the four screen corners of the 3D view onto the ground,
        // normalise them by the city size, rotate with the camera and scale to map pixels.
        IMiniMapView3D* view3d = host ? host->View3DWindow() : nullptr;
        s3d::cS3DCamera* camera = renderer ? renderer->Camera() : nullptr;
        if (view3d && camera) {
            const int viewW = view3d->Width();
            const int viewH = view3d->Height();
            const cS3DVector2 screen[4] = {{0.0f, 0.0f},
                                           {static_cast<float>(viewW - 1), 0.0f},
                                           {static_cast<float>(viewW - 1), static_cast<float>(viewH - 1)},
                                           {0.0f, static_cast<float>(viewH - 1)}};
            cS3DVector3 origins[4], directions[4], corners[4];
            for (int i = 0; i < 4; ++i) camera->GetEyeRay(screen[i], origins[i], directions[i]);
            for (int i = 0; i < 4; ++i)
                renderer->PickTerrainAlways(&origins[i].x, &directions[i].x, &corners[i].x, true);
            const float sizeX = host->CitySizeX() - 1.0f;   // the original normalises by size - 1 (0x100a251e0)
            const float sizeZ = host->CitySizeZ() - 1.0f;
            const float last = static_cast<float>(mapSize_ - 1);
            float px[4], py[4];
            for (int i = 0; i < 4; ++i) {
                const float ax = corners[i].x / sizeX - 0.5f;
                const float by = corners[i].z / sizeZ - 0.5f;
                float u, v;
                if ((rotation & 1) != 0) {
                    u = -by;
                    v = ax;
                } else {
                    u = ax;
                    v = -by;
                }
                if ((rotation & 2) != 0) {
                    u = -u;
                    v = -v;
                }
                // The original truncates the scaled coordinate with the add-12582912 floor trick (0x1005c6f3d).
                px[i] = static_cast<float>(static_cast<int>(std::floor((u + 0.5f) * last)) + drawArea_.left);
                py[i] = static_cast<float>(static_cast<int>(std::floor((v + 0.5f) * last)) + drawArea_.top);
            }
            if (drawBuffer_ && host) {
                const std::uint32_t white = drawBuffer_->MakeNativeColor(0xff, 0xff, 0xff);
                for (int i = 1; i <= 4; ++i) {
                    const int next = i & 3;
                    host->DrawLineAntialiased(drawBuffer_, static_cast<int>(px[i - 1]), static_cast<int>(py[i - 1]),
                                              static_cast<int>(px[next]), static_cast<int>(py[next]), white);
                }
            }
        }
    }
    if ((flags_ & 0x10) != 0) {
        // The active city-situation target: a blinking 3x3 marker at its position on the map.
        IMiniMapSituation* situation = host ? host->CitySituationManager() : nullptr;
        float position[3] = {0.0f, 0.0f, 0.0f};
        if (situation && situation->CurrentTargetPosition(position)) {
            const float sizeX = host->CitySizeX();
            const float sizeZ = host->CitySizeZ();
            float x = position[0], z = position[2];
            if ((rotation & 1) != 0) {
                const float oldX = x;
                x = z;
                z = sizeZ - oldX;
            }
            if ((rotation & 2) != 0) {
                x = sizeX - x;
                z = sizeZ - z;
            }
            const float windowW = static_cast<float>(area_.right - area_.left);
            const float windowH = static_cast<float>(area_.bottom - area_.top);
            float mapX = 0.0f, mapY = 0.0f;
            if (sizeX != 0.0f) mapX = windowW * x / sizeX;
            if (sizeZ != 0.0f) mapY = windowH * z / sizeZ;
            const bool wasOff = !markerBlink_;
            markerBlink_ = !markerBlink_;
            context->SetColor(wasOff ? 0xffc00000u : 0xffff4040u);
            const float points[4] = {mapX - 1.0f, mapY - 1.0f, mapX + 1.0f, mapY + 1.0f};
            context->Draw(gz::eDrawingOp::FillRect, points, 2);
        }
    }
    return true;
}

}  // namespace sc4
