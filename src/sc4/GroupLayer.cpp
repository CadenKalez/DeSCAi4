#include "sc4/GroupLayer.h"
#include "gz/StreamOps.h"
#include "sc4/QuadGrid.h"
#include <cmath>
#pragma STDC FP_CONTRACT OFF

static float Dist(float ax, float ay, float az, float bx, float by, float bz) {
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dz * dz + dy * dy + dx * dx);   // summation order of the original
}

void cGroup_SC4RH::Clear() {
    for (auto it = instances.begin(); it != instances.end();) {
        cInstance* inst = *it;
        inst->owner->erase(inst->id);
        delete inst;
        it = instances.erase(it);
    }
    peak = count;
    count = 0;
}

void cGroup_SC4RH::RemoveInstance(cInstance* inst) {
    for (auto it = instances.begin(); it != instances.end(); ++it) {
        if (*it == inst) {
            inst->group = nullptr;
            instances.erase(it);
            --count;
            return;
        }
    }
}

void cGroup_SC4RH::AddInstance(cInstance* inst) {
    inst->group = this;
    instances.push_front(inst);
    ++count;
    if (count > peak) peak = count;
}

cGroupLayer::cGroupLayer(float r) : groupRadius(r) {}

bool cGroupLayer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1) { *out = this; AddRef(); }
    return iid == 1;
}

void cGroupLayer::AddRef() { ++refCount; }

std::int32_t cGroupLayer::Release() {
    if (refCount < 2) {
        delete this;      // the group objects themselves are not freed (list nodes only)
        return 0;
    }
    return --refCount;
}

bool cGroupLayer::Shutdown() { Clear(false); return true; }

void cGroupLayer::Clear(bool keepGroups) {
    for (auto it = groups.begin(); it != groups.end();) {
        cGroup_SC4RH* g = *it;
        g->Clear();
        it = std::next(it);
        if (!keepGroups) {
            groups.remove(g);
            delete g;
            --groupCount;
        }
    }
}

void cGroupLayer::ClearEmptyGroups() {
    for (auto it = groups.begin(); it != groups.end();) {
        cGroup_SC4RH* g = *it;
        auto next = std::next(it);
        if (g->count == 0) {
            groups.erase(it);
            delete g;
            --groupCount;
        }
        it = next;
    }
}

void cGroupLayer::AddPositionToGroup(const cS3DVector3& pos, float radius) {
    for (cGroup_SC4RH* g : groups) {
        if (Dist(g->x, g->y, g->z, pos.x, pos.y, pos.z) < g->radius + radius) {
            ++g->count;
            if (g->count > g->peak) g->peak = g->count;
            return;
        }
    }
    auto* g = new cGroup_SC4RH;
    g->x = pos.x; g->y = pos.y; g->z = pos.z;
    g->radius = groupRadius;
    g->count = 1;
    g->peak = 1;
    groups.push_front(g);
    ++groupCount;
}

bool cGroupLayer::InsertInstance(const cS3DVector3& pos, float radius) {
    AddPositionToGroup(pos, radius);
    return true;
}

void cGroupLayer::AddInstanceToGroup(cInstance* inst) {
    for (cGroup_SC4RH* g : groups) {
        if (Dist(g->x, g->y, g->z, inst->x, inst->y, inst->z) < g->radius + inst->radius) {
            g->AddInstance(inst);
            return;
        }
    }
    auto* g = new cGroup_SC4RH;
    g->x = inst->x; g->y = inst->y; g->z = inst->z;
    g->radius = groupRadius;
    g->count = 1;
    g->peak = 1;
    groups.push_front(g);
    ++groupCount;
}

bool cGroupLayer::InsertInstance(std::uint32_t id, const cS3DVector3& pos, float radius) {
    if (instances.find(id) != instances.end()) return false;
    auto* inst = new cInstance;
    inst->id = id;
    inst->x = pos.x; inst->y = pos.y; inst->z = pos.z;
    inst->radius = radius;
    inst->owner = &instances;
    instances[id] = inst;
    AddInstanceToGroup(inst);
    return true;
}

bool cGroupLayer::RemoveInstance(std::uint32_t id) {
    auto it = instances.find(id);
    if (it == instances.end()) return false;
    cInstance* inst = it->second;
    instances.erase(it);
    if (inst->group) inst->group->RemoveInstance(inst);   // the original dereferences the null group of a founding instance
    delete inst;
    return true;
}

void cGroupLayer::UpdateInstance(std::uint32_t id, const cS3DVector3& pos, float radius) {
    RemoveInstance(id);
    InsertInstance(id, pos, radius);
}

void cGroupLayer::Intersect(cGroupLayer* a, cGroupLayer* b, bool swap) {
    cGroupLayer* outer = a;
    if (swap) { outer = b; b = a; }
    Clear(false);
    for (cGroup_SC4RH* g : outer->groups) g->mark = false;
    int remaining = outer->groupCount;
    if (remaining == 0) return;
    for (cGroup_SC4RH* bg : b->groups) {
        for (cGroup_SC4RH* og : outer->groups) {
            if (!og->mark &&
                Dist(og->x, og->y, og->z, bg->x, bg->y, bg->z) < og->radius + bg->radius) {
                auto* g = new cGroup_SC4RH;
                g->x = og->x; g->y = og->y; g->z = og->z;
                g->radius = og->radius;
                g->count = 1;
                g->peak = 1;
                groups.push_front(g);
                // the original bumps only the list size (+0x48), not the group count (+0x50)
                og->mark = true;
                --remaining;
            }
            if (remaining == 0) return;
        }
    }
}

void cGroupLayer::GetPositions(const SC4RectFloat& r, std::list<cS3DVector3>& out, int& nGroups, int& total, bool clip) const {
    nGroups = 0;
    total = 0;
    for (cGroup_SC4RH* g : groups) {
        if (!clip || (r.x0 <= g->x && r.z0 <= g->z && g->x <= r.x1 && g->z <= r.z1)) {
            out.push_front(cS3DVector3{g->x, g->y, g->z});
            ++nGroups;
            total += (int)g->peak;
        }
    }
}
