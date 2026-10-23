// cS3DModelInstanceAnimatedSprite: see ModelInstanceAnimatedSprite.h. Single-precision arithmetic follows the order
// of the original.
#pragma STDC FP_CONTRACT OFF
#include "s3d/ModelInstanceAnimatedSprite.h"

#include <cmath>
#include <cstring>

#include "s3d/S3DMath.h"

namespace s3d {

static ISpriteInstanceHost* g_spriteHost = nullptr;
ISpriteInstanceHost* SpriteInstanceHost() { return g_spriteHost; }
void SetSpriteInstanceHost(ISpriteInstanceHost* host) { g_spriteHost = host; }

namespace {
constexpr std::uint32_t kFlagHighlighted = 1, kFlagTranslucent = 2, kFlagSelfLit = 4;
constexpr float kPi = 3.1415927f, kTwoPi = 6.2831855f, kInvTwoPi = 0.15915494f, kMagic = 12582912.0f;

// (float)value rounded to the nearest integer through the 1.5 * 2^23 trick the original uses.
std::uint32_t RoundThroughMagic(float value) {
    const float biased = value + kMagic;
    std::uint32_t bits;
    std::memcpy(&bits, &biased, 4);
    return bits + 0xb4c00000u;
}
}  // namespace

cS3DModelInstanceAnimatedSprite::cS3DModelInstanceAnimatedSprite() {
    SetProperties(1);
    renderState_.PackRenderState(true, true, true, true, true, 1, false, 3, 4, 5, 4, 0.5f, 1, -1);
    textureData_ = std::make_unique<cAnimatedSpriteTextureData>();
    for (auto& table : tables_) table = std::make_unique<sc4::cAnimatedSpriteTableData>();
}

cS3DModelInstanceAnimatedSprite::~cS3DModelInstanceAnimatedSprite() {
    for (auto& table : tables_) {
        if (table) table->Init(nullptr);       // releases the table
    }
    if (binding_) {
        binding_->Release();
        binding_ = nullptr;
    }
}

bool cS3DModelInstanceAnimatedSprite::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIS3DModelInstanceAnimated) {
        *out = static_cast<cIS3DModelInstanceAnimated*>(this);
        AddRef();
        return true;
    }
    return cS3DModelInstanceBasic::QueryInterface(iid, out);
}

int cS3DModelInstanceAnimatedSprite::SetPosition(const cS3DVector3& position) {
    nS3D::SetTranslation(transform_, position);   // translation, flag 1, change counter + 1
    return 1;
}

int cS3DModelInstanceAnimatedSprite::SetOrientation(const cS3DVector3& angles) {
    nS3D::SetOrientationEuler(transform_, angles);
    return 0;   // the original returns 0 on every path
}

int cS3DModelInstanceAnimatedSprite::SetOrientation(const cS3DVector3& target, const cS3DVector3& up) {
    const cS3DVector3 forward{transform_.translation[0] - target.x, transform_.translation[1] - target.y,
                              transform_.translation[2] - target.z};
    nS3D::SetOrientationForwardUp(transform_, forward, up);
    transform_.changeCounter = std::uint8_t(transform_.changeCounter + 1);   // the original adds 2 in all
    return 2;
}

void cS3DModelInstanceAnimatedSprite::ModelPointToWorldPoint(cS3DVector3& point) { nS3D::ModelPointToWorldPoint(transform_, point); }

void cS3DModelInstanceAnimatedSprite::ModelPointToWorldPoint(float* point) {
    cS3DVector3 p{point[0], point[1], point[2]};
    nS3D::ModelPointToWorldPoint(transform_, p);
    point[0] = p.x;
    point[1] = p.y;
    point[2] = p.z;
}

void cS3DModelInstanceAnimatedSprite::ModelVectorToWorldVector(cS3DVector3& vector) { nS3D::ModelVectorToWorldVector(transform_, vector); }

void cS3DModelInstanceAnimatedSprite::ModelVectorToWorldVector(float* vector) {
    cS3DVector3 v{vector[0], vector[1], vector[2]};
    nS3D::ModelVectorToWorldVector(transform_, v);
    vector[0] = v.x;
    vector[1] = v.y;
    vector[2] = v.z;
}

int cS3DModelInstanceAnimatedSprite::SetZoomAndRotation(int zoom, int rotation) {
    if (zoom_ != unsigned(zoom) || rotation_ != unsigned(rotation)) {
        zoom_ = std::uint8_t(zoom);
        rotation_ = std::uint8_t(rotation);
        viewDirty_ = true;
        return 2;
    }
    return 0;
}

int cS3DModelInstanceAnimatedSprite::SetFrameset(int index) {
    FramesetInfo* info = framesets_;
    if (!info || index >= int(info->count)) return 0;
    info->current = std::uint16_t(index);
    info->frameCount = std::uint16_t(info->starts[std::size_t(index) + 1] - info->starts[std::size_t(index)]);
    if (frame_ != info->absoluteFrame) {
        frame_ = std::uint8_t(info->absoluteFrame);
        frameDirty_ = true;
    }
    return 2;
}

int cS3DModelInstanceAnimatedSprite::SetFrame(int frame) {
    FramesetInfo* info = framesets_;
    if (!info) {
        if (frame_ == unsigned(frame)) return 2;
        frame_ = std::uint8_t(frame);
    } else {
        if (int(info->frameCount) < frame) frame = frame % int(info->frameCount);
        info->absoluteFrame = std::uint16_t(info->starts[info->current] + frame);
        if (frame_ == info->absoluteFrame) return 2;
        frame_ = std::uint8_t(info->absoluteFrame);
    }
    frameDirty_ = true;
    return 2;
}

int cS3DModelInstanceAnimatedSprite::IncrementFrame() {
    if (!framesets_) {
        IncrementFrameSimple();
    } else {
        framesets_->IncrementFrame();
        if (frame_ != framesets_->absoluteFrame) {
            frame_ = std::uint8_t(framesets_->absoluteFrame);
            frameDirty_ = true;
        }
    }
    return 2;
}

int cS3DModelInstanceAnimatedSprite::IncrementFrameSimple() {
    const int count = GetFrameCount();
    if (count < 2) return 1;
    bool finished = true;
    if (direction_ > 0) {
        if (int(frame_) + 1 < count) {
            frame_ = std::uint8_t(frame_ + 1);
            finished = false;
        } else if (playMode_ == kPlayPingPong) {
            frame_ = std::uint8_t(frame_ - 1);
            direction_ = -1;
            finished = false;
        } else if (playMode_ == kPlayLoop) {
            frame_ = 0;
            finished = false;
        }
    } else if (direction_ == 0) {
        finished = false;               // stopped: nothing moves, the picture is simply refreshed
    } else {
        if (frame_ != 0) {
            frame_ = std::uint8_t(frame_ - 1);
            finished = false;
        } else if (playMode_ == kPlayPingPong) {
            frame_ = 1;
            direction_ = 1;
            finished = false;
        } else if (playMode_ == kPlayLoop) {
            frame_ = std::uint8_t(count - 1);
            finished = false;
        }
    }
    if (finished) {                     // a play-once sprite stops on its last frame
        direction_ = 0;
        accumulator_ = 0.0f;
    }
    frameDirty_ = true;
    return 1;
}

int cS3DModelInstanceAnimatedSprite::SetAutoFrameRate(float rate) {
    autoFrameRate_ = rate;
    return 2;
}

int cS3DModelInstanceAnimatedSprite::DoAutoFrameUpdate(float seconds) {
    float rate = autoFrameRate_;
    if (rate == kAutoFrameRateUnset) {
        SetAutoFrameRate(kAutoFrameRateDefault);   // vslot 0x108
        rate = autoFrameRate_;
    }
    if (rate == 0.0f) return 0;
    float accumulated = float(int(direction_)) * rate * seconds + accumulator_;
    accumulator_ = accumulated;
    const float magnitude = accumulated < 0.0f ? -accumulated : accumulated;
    if (magnitude < 1.0f) return 0;
    if (2.0f <= magnitude) {
        if (catchUp_) {
            float next;
            do {
                IncrementFrame();             // vslot 0x100
                const float before = accumulator_;
                next = float(-int(direction_)) + before;
                accumulator_ = next;
            } while (1.0f <= (next < 0.0f ? -next : next));
            return 2;
        }
        IncrementFrame();
        accumulator_ = 1.0f;                  // quirk: skipped frames reset the accumulator to exactly one
        return 2;
    }
    IncrementFrame();
    accumulator_ = float(-int(direction_)) + accumulator_;
    return 2;
}

int cS3DModelInstanceAnimatedSprite::SetOpacity(bool translucent, float opacity) {
    SC4DrawAttributes a = GetDrawAttributes();
    if (translucent) {
        if (opacity == kAutoFrameRateUnset) return 2;     // -100: leave the opacity alone
        if ((opacity != kAutoFrameRateDefault || opacity != opacity) && opacity <= 0.99f) {
            a.flags |= kFlagTranslucent;
            a.shade[3] = opacity;
            SetDrawAttributes(a);
            return 2;
        }
    }
    a.flags &= ~kFlagTranslucent;
    a.shade[3] = 1.0f;
    SetDrawAttributes(a);
    return 2;
}

int cS3DModelInstanceAnimatedSprite::SetHighlight(int value) {
    SC4DrawAttributes a = GetDrawAttributes();
    const bool highlighted = (a.flags & kFlagHighlighted) != 0;
    const unsigned stored = (a.flags >> 8) & 0xff;
    if ((value != 0) != highlighted || stored != unsigned(value)) {
        if (value == 0) a.flags &= ~kFlagHighlighted;
        else a.flags |= kFlagHighlighted;
        a.flags = (a.flags & ~0xff00u) | (std::uint32_t(value & 0xff) << 8);
        SetDrawAttributes(a);
        return 2;
    }
    return 0;
}

int cS3DModelInstanceAnimatedSprite::SetSelfIlluminated(bool on) {
    SC4DrawAttributes a = GetDrawAttributes();
    if ((((a.flags & kFlagSelfLit) >> 2) ^ unsigned(on)) == 1) {
        if (on) a.flags |= kFlagSelfLit;
        else a.flags &= ~kFlagSelfLit;
        SetDrawAttributes(a);
        return 2;
    }
    return 0;
}

bool cS3DModelInstanceAnimatedSprite::LoadSpriteResource(const cGZPersistResourceKey& key) {
    ISpriteInstanceHost* host = SpriteInstanceHost();
    if (!host) return false;
    cIS3DResSpriteAnim* anim = host->FindSpriteAnim(key);       // SL::spRM vslot 0x18
    if (!anim) return false;
    const bool loaded = LoadSpriteResource(anim);                // vslot 0x158
    anim->Release();
    return loaded;
}

bool cS3DModelInstanceAnimatedSprite::LoadSpriteResource(cIS3DResSpriteAnim* anim) {
    if (!anim) return false;
    ISpriteInstanceHost* host = SpriteInstanceHost();
    if (!host) return false;
    const SpriteAnimData* data = anim->GetData();
    frameCount_ = data->frameCount;
    unsigned loaded = 0;
    for (; loaded < 5; ++loaded) {
        data = anim->GetData();
        zoomScale_[loaded] = data->zoomScale[loaded];
        sc4::ISpriteAnimTable* table = host->FindSpriteAnimTable(data->tableType, data->tableGroup, data->tableInstance[loaded]);
        if (!table) break;
        tables_[loaded]->Init(table);
        table->Release();
    }
    ISpriteTextureBinding* binding = nullptr;
    if (loaded == 5) {
        data = anim->GetData();
        binding = host->CreateTextureBinding(data->textureKey);
        if (binding_) binding_->Release();
        binding_ = binding;
        if (binding) return true;
    }
    // failure: drop the tables loaded so far (the original walks down from the last loaded one)
    for (unsigned i = loaded == 5 ? 5 : loaded; i-- > 0;) tables_[i]->Init(nullptr);
    return false;
}

bool cS3DModelInstanceAnimatedSprite::GetTextureData(float elevation, float azimuth, float unused, int zoom, int rotation,
                                                     int frame, int unused2, cAnimatedSpriteTextureData* out) {
    (void)unused2;
    const SpriteTextureRecord* record = GetTextureResourceData(elevation, azimuth, unused, zoom, rotation);
    GetTextureData(zoom, frame, 0, record, out);
    const bool changed = record_ != record;
    if (changed) record_ = record;
    return changed;
}

const SpriteTextureRecord* cS3DModelInstanceAnimatedSprite::GetTextureResourceData(float elevation, float azimuth, float unused,
                                                                                   int zoom, int rotation) {
    (void)unused;
    sc4::cAnimatedSpriteTableData* data = tables_[std::size_t(zoom)].get();
    if (!data) return nullptr;
    // Guard (not in the original): a table whose header carried countA/countB == 0 divides by zero in
    // the row/column modulo at 0x1001a63ee. The engine never loads such a table; keep the crash out.
    if (data->countA == 0 || data->countB == 0) return nullptr;
    const std::uint32_t countA = data->countA, countB = data->countB;
    const float shifted = (elevation + kPi) - data->startB;
    std::uint32_t row = RoundThroughMagic(float(countB) * shifted * kInvTwoPi) % countB;
    float turn = azimuth + float(rotation) * kTwoPi * -0.25f;
    const float wrapped = turn < 0.0f ? kTwoPi + turn : turn;
    std::uint32_t column = RoundThroughMagic(float(countA) * wrapped * kInvTwoPi);
    if (column >= countA) column %= countA;
    const std::uint32_t index = row * data->countC + column * data->frameCount;
    return reinterpret_cast<const SpriteTextureRecord*>(data->frames) + index;
}

void cS3DModelInstanceAnimatedSprite::GetTextureData(int zoom, int frame, int unused, const SpriteTextureRecord* record,
                                                     cAnimatedSpriteTextureData* out) {
    (void)unused;
    const float scale = zoomScale_[zoom];
    int atlas = 1 << (record->sizeShift & 31);
    const int width = record->width > 0 ? record->width : -int(record->width);
    const int height = record->height > 0 ? record->height : -int(record->height);
    const float texel = 1.0f / float(atlas);
    std::uint32_t x = record->x, y = record->y;
    std::uint32_t texture = record->texture;
    out->textureIndex = texture;
    if (frame != 0) {
        const std::uint32_t maxX = std::uint32_t(atlas - width);
        const int maxY = atlas - height;
        std::uint32_t run = std::uint32_t(frame) * std::uint32_t(width);
        if (maxX < run + record->x) {
            y = std::uint32_t(record->y) + std::uint32_t(height);
            if (maxY < int(y)) {
                texture = record->texture + 1u;
                out->textureIndex = texture;
                y = 0;
            }
            const std::uint32_t room = std::uint32_t(atlas) - record->x;
            run = (run - room) + room % std::uint32_t(width);
            atlas = atlas - atlas % width;
            x = 0;
        }
        while (maxX < run) {
            run -= std::uint32_t(atlas);
            y += std::uint32_t(height);
            if (maxY < int(y)) {
                ++texture;
                out->textureIndex = texture;
                y = 0;
            }
        }
        x = run + x;
    }
    std::uint32_t right = x + std::uint32_t(width);
    std::uint32_t left = x;
    if (record->width < 0) {      // mirrored horizontally
        right = x;
        left = x + std::uint32_t(width);
    }
    const float uLeft = float(int(left)) * texel, uRight = float(int(right)) * texel;
    vertices_[0].u = uLeft;
    vertices_[1].u = uLeft;
    vertices_[2].u = uRight;
    vertices_[3].u = uRight;
    std::uint32_t bottom = y + std::uint32_t(height);
    std::uint32_t top = y;
    if (record->height < 0) {     // mirrored vertically
        bottom = y;
        top = y + std::uint32_t(height);
    }
    vertices_[0].v = float(int(top)) * texel;
    vertices_[1].v = float(int(bottom)) * texel;
    vertices_[2].v = float(int(bottom)) * texel;
    vertices_[3].v = float(int(top)) * texel;
    out->width = float(width);
    out->height = float(height);
    out->offsetX = float(int(record->offsetX));
    out->offsetY = float(int(record->offsetY));
    const float delta = scale - 1.0f;
    if (0.0001f <= (delta < 0.0f ? -delta : delta)) {
        out->width = float(width) * scale;
        out->height = float(height) * scale;
        out->offsetX = float(int(record->offsetX)) * scale;
        out->offsetY = float(int(record->offsetY)) * scale;
    }
}

void cS3DModelInstanceAnimatedSprite::GetSortValue(std::uint32_t& out) const {
    out = 0;
    if (binding_ && textureData_) out = binding_->TextureIds()[std::int32_t(textureData_->textureIndex)];
}

void cS3DModelInstanceAnimatedSprite::GetWorldBounds(cS3DBoundingBox& out) {
    for (int i = 0; i < 3; ++i) {
        out.min[i] = transform_.translation[i];
        out.min[i] = out.min[i] + bounds_.min[i];
        out.max[i] = transform_.translation[i];
        out.max[i] = out.max[i] + bounds_.max[i];
    }
}

void cS3DModelInstanceAnimatedSprite::GetBounds(cS3DBoundingBox& out) {
    for (int i = 0; i < 3; ++i) {
        out.max[i] = bounds_.max[i];
        out.min[i] = bounds_.min[i];
    }
}

void cS3DModelInstanceAnimatedSprite::GetTransform(cS3DTransform& out) { nS3D::AssignTransform(out, transform_); }
void cS3DModelInstanceAnimatedSprite::SetTransform(const cS3DTransform& transform) { nS3D::AssignTransform(transform_, transform); }

void cS3DModelInstanceAnimatedSprite::UpdateView(SC4DrawContext* context) {
    ISpriteDrawContext* ctx = SpriteInstanceHost()->DrawContext(context);
    cS3DCamera* camera = ctx->Camera();
    float magnification = ctx->CameraMagnification();
    float pixelScale;
    if (!ctx->ViewPixelScale(pixelScale)) {
        float left, right, top, bottom, nearPlane, farPlane;
        camera->GetViewVolume(left, right, top, bottom, nearPlane, farPlane);
        pixelScale = (right - left) / float(ctx->ViewportWidth());
    }
    const cS3DTransform& xf = camera->LocalXform();
    const float r00 = xf.rotation[0][0], r02 = xf.rotation[0][2];
    const float r10 = xf.rotation[1][0], r11 = xf.rotation[1][1], r12 = xf.rotation[1][2];
    const float r20 = xf.rotation[2][0], r22 = xf.rotation[2][2];
    const cAnimatedSpriteTextureData& info = *textureData_;
    const float spanY = info.height - info.offsetY;
    const float unitsPerPixel = (pixelScale / r11) * magnification;
    float cornerX = 0.0f, cornerY = 0.0f, cornerZ = 0.0f;
    if (0.0f <= spanY) {
        const float along = (spanY * unitsPerPixel) / r12;
        cornerX = r02 * along;
        cornerY = r12 * along;
        cornerZ = along * r22;
    }
    const float rightX = r00 * pixelScale * magnification;
    const float rightY = r10 * pixelScale * magnification;
    const float rightZ = pixelScale * r20 * magnification;
    bounds_ = cS3DBoundingBox{{0, 0, 0}, {0, 0, 0}};
    const float top = info.offsetY * unitsPerPixel + cornerY;        // y of the upper corners
    const float offsetX = info.offsetX;
    const float rightYoff = rightY * offsetX;
    const float widthRest = info.width - offsetX;
    const float rightYwide = rightY * widthRest;
    const float x0 = cornerX - rightX * offsetX;
    const float y0 = top - rightYoff;
    const float z0 = cornerZ - rightZ * offsetX;
    corners_[0][0] = x0;
    corners_[0][1] = y0;
    corners_[0][2] = z0;
    float maxY = 0.0f <= y0 ? y0 : 0.0f, minY = y0 <= 0.0f ? y0 : 0.0f;
    float maxX = 0.0f <= x0 ? x0 : 0.0f, minX = x0 <= 0.0f ? x0 : 0.0f;
    float maxZ = 0.0f <= z0 ? z0 : 0.0f, minZ = z0 <= 0.0f ? z0 : 0.0f;
    const float x3 = rightX * widthRest + cornerX;
    const float z3 = widthRest * rightZ + cornerZ;
    const float y3 = top + rightYwide;
    corners_[3][0] = x3;
    corners_[3][1] = y3;
    corners_[3][2] = z3;
    if (minX <= maxX) {
        if (maxX <= x3) maxX = x3;
        if (x3 <= minX) minX = x3;
        if (maxY <= y3) maxY = y3;
        if (y3 <= minY) minY = y3;
        if (maxZ <= z3) maxZ = z3;
        if (z3 <= minZ) minZ = z3;
    } else {                    // NaN input: the extents collapse to the last corner
        maxX = x3;
        maxY = y3;
        maxZ = z3;
        minX = x3;
        minY = y3;
        minZ = z3;
    }
    const float bottomBase = cornerY - spanY * unitsPerPixel;
    corners_[1][0] = x0;
    corners_[1][1] = bottomBase - rightYoff;
    corners_[1][2] = z0;
    corners_[2][0] = x3;
    corners_[2][1] = bottomBase + rightYwide;
    corners_[2][2] = z3;
    // mirror the x / z extents about the origin
    const float mirrorX = -maxX, mirrorZ = -maxZ;
    if (minX <= maxX) {
        if (maxX <= mirrorX) maxX = mirrorX;
        if (mirrorX <= minX) minX = mirrorX;
        if (maxY <= minY) minY = maxY;         // min(y) against max(y): a no-op for sane input
        if (maxZ <= mirrorZ) maxZ = mirrorZ;
        if (mirrorZ <= minZ) minZ = mirrorZ;
    } else {
        maxX = mirrorX;
        minX = mirrorX;
        minY = maxY;
        maxZ = mirrorZ;
        minZ = mirrorZ;
    }
    bounds_.min[0] = minX;
    bounds_.min[1] = minY;
    bounds_.min[2] = minZ;
    bounds_.max[0] = maxX;
    bounds_.max[1] = maxY;
    bounds_.max[2] = maxZ;
}

int cS3DModelInstanceAnimatedSprite::Draw(SC4DrawContext* context, bool flag) {
    if (flag) return 1;
    ISpriteDrawContext* ctx = SpriteInstanceHost()->DrawContext(context);
    if (ctx->Zoom() < 2) return 1;
    bool update = false;       // the original's local `cVar9`: true when the view was refreshed
    bool skipRefresh = false;
    if (!viewDirty_) {
        if (unsigned(ctx->Zoom()) == zoom_) {
            const unsigned rotation = unsigned(ctx->Rotation());
            viewDirty_ = rotation != rotation_;
            if (rotation == rotation_ && !frameDirty_) skipRefresh = true;
        } else {
            viewDirty_ = true;
        }
    }
    if (!skipRefresh) {
        const unsigned zoom = unsigned(ctx->Zoom());
        zoom_ = std::uint8_t(zoom);
        const unsigned rotation = unsigned(ctx->Rotation());
        rotation_ = std::uint8_t(rotation);
        const float elevation = std::asin(transform_.rotation[1][2]);
        const float azimuth = std::atan2(transform_.rotation[2][2], transform_.rotation[0][2]);
        GetTextureData(elevation, azimuth, 0.0f, int(zoom & 0xff), int(rotation & 0xff), int(frame_), 0, textureData_.get());
        if (viewDirty_) {
            UpdateView(context);
            update = viewDirty_;
        }
        frameDirty_ = false;
    }
    if (update || lastTransformCounter_ != transform_.changeCounter) {
        const float px = transform_.translation[0], py = transform_.translation[1], pz = transform_.translation[2];
        for (int i = 0; i < 4; ++i) {
            vertices_[i].x = corners_[i][0] + px;
            vertices_[i].y = corners_[i][1] + py;
            vertices_[i].z = corners_[i][2] + pz;
        }
        lastTransformCounter_ = transform_.changeCounter;
        viewDirty_ = false;
    }
    ctx->ResetModelViewTransform();
    const SC4DrawAttributes attributes = GetDrawAttributes();
    if ((attributes.flags & kFlagSelfLit) == 0) ctx->SetModelShade(AsModelInstance(), attributes.shade);
    else ctx->SetSelfLitShade(attributes.shade);
    ctx->SetRenderState(renderState_);
    if (attributes.flags & kFlagTranslucent) {
        ctx->SetTransparency(attributes.shade[3]);
        ctx->SetAlphaFunc(4, attributes.shade[3] * 0.5f);
    }
    ctx->SetTexture(binding_->TextureIds()[std::int32_t(textureData_->textureIndex)], 0);
    ctx->DrawPrims(2, 10, 4, vertices_);
    ctx->ResetShade();
    ctx->ResetTransparency();
    return 1;
}

bool cS3DModelInstanceAnimatedSprite::Pick(cSC4PickContext* context) {
    float p[4][3];
    for (int i = 0; i < 4; ++i) {
        p[i][0] = vertices_[i].x;
        p[i][1] = vertices_[i].y;
        p[i][2] = vertices_[i].z;
    }
    ISpriteInstanceHost* host = SpriteInstanceHost();
    if (host->TrianglePick(context, p[0], p[1], p[2])) return true;
    return host->TrianglePick(context, p[2], p[3], p[0]);
}

}  // namespace s3d
