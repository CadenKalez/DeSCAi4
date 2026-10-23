#include "sc4/PathFinder.h"

#include <algorithm>

#include "sc4/GeometryUtilities.h"

namespace sc4 {

// ---- ParseTripLocation ----------------------------------------------------------------------------------------------------

// 0x1000d3ccc
void ParseTripLocation(std::uint32_t key, std::uint8_t& travelType, std::int16_t& x, std::int16_t& z) {
    travelType = std::uint8_t(key >> 16);
    x = std::int16_t((key >> 8) & 0xff);
    z = std::int16_t(key & 0xff);
    if (std::int32_t(key) < 0) {
        const std::uint32_t direction = (key >> 24) & 3;
        x = std::int16_t(x + kCellStepX[direction]);
        z = std::int16_t(z + kCellStepZ[direction]);
    }
}

// ---- TripNodeHashMap ------------------------------------------------------------------------------------------------------

// 0x1000d4dd4: one zeroed block of 0x1000 nodes to start with.
TripNodeHashMap::TripNodeHashMap() {
    blocks_.emplace_back(new TripNode[kBlockNodes]());
    next_ = blocks_[0].get();
    blockEnd_ = next_ + kBlockNodes;
}
TripNodeHashMap::~TripNodeHashMap() = default;   // 0x1000d4e86

// 0x1000d2468: the node for `key`, created (zeroed, location parsed) at the head of its bucket if missing.
TripNode* TripNodeHashMap::GetNode(std::uint32_t key) {
    const std::uint32_t bucket = std::uint32_t((std::uint64_t(key) * 0x9e3779b1ull) % kBuckets);
    for (TripNode* n = buckets_[bucket]; n; n = n->chain)
        if (n->key == key) return n;
    if (next_ == blockEnd_) {
        ++blockIndex_;
        if (blocks_.size() == std::size_t(blockIndex_)) blocks_.emplace_back(new TripNode[kBlockNodes]());
        next_ = blocks_[std::size_t(blockIndex_)].get();
        blockEnd_ = next_ + kBlockNodes;
    }
    TripNode* node = next_++;
    node->chain = buckets_[bucket];
    buckets_[bucket] = node;
    node->bucket = bucket;
    node->key = key;
    node->inQueue = 0;
    node->closed = 0;
    ParseTripLocation(key, node->travelType, node->x, node->z);
    return node;
}

// 0x1000d169c: zero every block (the used ones keep their memory), rewind to block 0 and empty the buckets.
void TripNodeHashMap::Clear() {
    for (auto& block : blocks_) std::fill(block.get(), block.get() + kBlockNodes, TripNode{});
    next_ = blocks_[0].get();
    blockEnd_ = next_ + kBlockNodes;
    blockIndex_ = 0;
    std::fill(std::begin(buckets_), std::end(buckets_), nullptr);
}

// ---- TripNodePriorityQueue ------------------------------------------------------------------------------------------------

// 0x1000d4d44: 100 slots, slot 0 unused.
TripNodePriorityQueue::TripNodePriorityQueue() : heap_(100, nullptr), count_(0), capacity_(99) {}

// 0x1000d25bc
void TripNodePriorityQueue::Push(TripNode* node) {
    std::uint32_t index = ++count_;
    if (capacity_ < index) {
        heap_.resize(std::size_t(index) * 2, nullptr);
        capacity_ = std::uint32_t(heap_.size()) - 1;
    }
    for (;;) {
        const std::uint32_t parent = index >> 1;
        if (parent == 0) break;
        TripNode* above = heap_[parent];
        if (above->score <= node->score) break;
        heap_[index] = above;
        above->heapIndex = index;
        index = parent;
    }
    heap_[index] = node;
    node->heapIndex = index;
}

// 0x1000d375c: move the last node to the top and sift it down (the smaller child wins, the right child only when strictly smaller).
TripNode* TripNodePriorityQueue::Pop() {
    TripNode* top = heap_[1];
    if (count_ == 1) {
        count_ = 0;
        return top;
    }
    heap_[1] = heap_[count_];
    heap_[1]->heapIndex = 1;
    --count_;
    if (count_ > 1) {
        std::uint32_t i = 1;
        const std::uint32_t n = count_;
        do {
            std::uint32_t left = i * 2;
            if (n < left) return top;
            TripNode* current = heap_[i];
            std::uint32_t best = left;
            if (current->score <= heap_[left]->score) best = i;
            const std::uint32_t right = left | 1;
            if (right <= n && heap_[right]->score < heap_[best]->score) best = right;
            if (best == i) return top;
            heap_[i] = heap_[best];
            heap_[i]->heapIndex = i;
            heap_[best] = current;
            current->heapIndex = best;
            i = best;
        } while (i < n);
    }
    return top;
}

// 0x1000d3c72
void TripNodePriorityQueue::UpdateNode(TripNode* node) {
    std::uint32_t index = node->heapIndex;
    for (;;) {
        const std::uint32_t parent = index >> 1;
        if (parent == 0) break;
        TripNode* above = heap_[parent];
        if (above->score <= node->score) break;
        heap_[index] = above;
        above->heapIndex = index;
        index = parent;
    }
    heap_[index] = node;
    node->heapIndex = index;
}

// ---- cSC4PathFinder -------------------------------------------------------------------------------------------------------

cSC4PathFinder::cSC4PathFinder(IPathFinderHost* host) : host_(host) {}   // 0x1000d0c62

bool cSC4PathFinder::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cISC4PathFinder) return false;
    if (out) *out = static_cast<cISC4PathFinder*>(this);
    AddRef();
    return true;
}
std::uint32_t cSC4PathFinder::AddRef() { return ++refCount_; }
// 0x1000d0eb6: the last reference shuts the finder down and destroys it.
std::uint32_t cSC4PathFinder::Release() {
    if (refCount_ < 2) {
        if (initialized_) Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

// 0x1000d0ef2: reads the city size and sizes the two distance tables (width + 2 / depth + 2 entries, element 1 is cell 0).
bool cSC4PathFinder::Init() {
    if (initialized_) return false;
    initialized_ = true;
    cityWidth_ = host_->CityWidthInCells();
    cityDepth_ = host_->CityDepthInCells();
    xDistance_.assign(std::size_t(cityWidth_) + 2, 0);
    zDistance_.assign(std::size_t(cityDepth_) + 2, 0);
    return true;
}

// 0x1000d10ca
bool cSC4PathFinder::Shutdown() {
    if (!initialized_) return false;
    initialized_ = false;
    ReleasePathNodes();
    return true;
}

// 0x1000d1664
void cSC4PathFinder::ReleasePathNodes() {
    nodes_.Clear();
    while (!queue_.Empty()) queue_.Pop();
    goal_ = nullptr;
}

// 0x1000d5190 / 0x1000d51ae
void cSC4PathFinder::SetTravelTypeFlag(std::uint16_t flag, bool alsoStart) {
    std::uint16_t allowed = std::uint16_t(travelTypes_ & 0xffff) | flag;
    std::uint16_t start = std::uint16_t(travelTypes_ >> 16);
    if (alsoStart) start = std::uint16_t(start | flag);
    travelTypes_ = std::uint32_t(allowed) | (std::uint32_t(start) << 16);
}
void cSC4PathFinder::ClearTravelTypeFlag(std::uint16_t flag) {
    std::uint16_t allowed = std::uint16_t((travelTypes_ & 0xffff) & ~flag);
    std::uint16_t start = std::uint16_t((travelTypes_ >> 16) & ~flag);
    travelTypes_ = std::uint32_t(allowed) | (std::uint32_t(start) << 16);
}

// 0x1000d1606: does the destination lot still have free trip capacity of this kind? Records how many trips fit and the kind.
bool cSC4PathFinder::CheckDestinationCapacity(cISCPropertyHolder* holder, std::uint8_t kind) {
    std::uint32_t limit = host_->TripCapacityLimit();
    if (limit == 0) return false;
    const std::uint32_t used = host_->UsedTripCapacity(holder, kind);
    const std::uint32_t freeTrips = limit - used;
    if (used <= limit && freeTrips != 0) {
        trafficArrived_ = std::min(trafficDensity_, freeTrips);
        capacityKind_ = kind;
        return true;
    }
    return false;
}

namespace {
// One axis of SetupDistanceTables: element i + 1 holds the distance of coordinate i to [lo, hi]; entries are clamped to the table
// (the original writes past it when the rectangle leaves the city).
void FillAxis(std::vector<int>& table, int lo, int hi, int size) {
    auto at = [&](int coordinate) -> int* {
        const long index = long(coordinate) + 1;
        return index >= 0 && index < long(table.size()) ? &table[std::size_t(index)] : nullptr;
    };
    int next = lo;
    if (lo >= 0) {
        for (int c = -1; c < lo; ++c)
            if (int* p = at(c)) *p = lo - c;
    } else {
        next = -1;
    }
    for (int c = next; c <= hi; ++c)
        if (int* p = at(c)) *p = 0;
    next = hi + 1;
    for (int c = next; c < size + 1 && c <= size; ++c)
        if (int* p = at(c)) *p = c - hi;
}
}  // namespace

// 0x1000d3556: the heuristic tables - how many cells each x / z lies from the end rectangle.
void cSC4PathFinder::SetupDistanceTables() {
    FillAxis(xDistance_, endRect_.x0, endRect_.x1, cityWidth_);
    FillAxis(zDistance_, endRect_.z0, endRect_.z1, cityDepth_);
}

// 0x1000d364c: recompute every allocated node's score from its cost and the (new) heuristic; queued nodes sift up.
void cSC4PathFinder::RecalculateNodeScores() {
    nodes_.ForEachAllocated([&](TripNode* n) {
        float h = 0.0f;
        const bool outsideX = std::uint32_t(nearRect_.x1 - nearRect_.x0) < std::uint32_t(n->x - nearRect_.x0);
        const bool outsideZ = std::uint32_t(nearRect_.z1 - nearRect_.z0) < std::uint32_t(n->z - nearRect_.z0);
        if (outsideX || outsideZ) h = float(zDistance_[std::size_t(n->z + 1)] + xDistance_[std::size_t(n->x + 1)]) * heuristicWeight_;
        n->score = h + n->cost;
        if (n->inQueue) queue_.UpdateNode(n);
    });
}

// 0x1000d3976: direction code from one cell to another (0 west, 1 north, 2 east, 3 south).
std::uint8_t cSC4PathFinder::EdgeTo(int fromX, int fromZ, int toX, int toZ) {
    if (toX < fromX) return 0;
    if (toX > fromX) return 2;
    return fromZ <= toZ ? 3 : 1;
}

// 0x1000d3994: relax the state (x, z, direction, travel type) reached from `parent`.
void cSC4PathFinder::AddTripNode(int x, int z, std::uint8_t direction, std::uint8_t travelType, float stepCost, TripNode* parent,
                                 float limit, bool outside) {
    if ((blockedTypes_ >> (travelType & 0x1f)) & 1) return;
    std::uint32_t key = std::uint32_t(z & 0xff) | (std::uint32_t(x & 0xff) << 8) | (std::uint32_t(direction) << 24) |
                        (std::uint32_t(travelType) << 16) | (std::uint32_t(parent->direction) << 26);
    if (outside) key |= 0x80000000u;
    if (parent->parent && parent->parent->key == key) return;   // straight back where we came from
    const float newCost = stepCost + parent->cost;
    if (newCost <= limit || (tripFlags_ & 1)) {
        TripNode* node = nodes_.GetNode(key);
        if ((!node->inQueue || newCost < node->cost) && (!node->closed || newCost < node->cost)) {
            node->parent = parent;
            node->closed = 0;
            node->cost = newCost;
            float h = 0.0f;
            if (x < nearRect_.x0 || z < nearRect_.z0 || nearRect_.x1 < x || nearRect_.z1 < z)
                h = float(zDistance_[std::size_t(z + 1)] + xDistance_[std::size_t(x + 1)]);
            node->score = newCost + h * heuristicWeight_;
            node->direction = direction;
            if (node->inQueue) {
                queue_.UpdateNode(node);
            } else {
                queue_.Push(node);
                node->inQueue = 1;
            }
        }
    } else {
        // over the cost limit: count it, and after 64 such nodes stop trying this travel type
        const std::uint32_t seen = ++congestion_[travelType];
        if (seen > 0x40) blockedTypes_ = std::uint8_t(blockedTypes_ | (1u << (travelType & 0x1f)));
    }
}

// 0x1000d3b32: distance (in cells, corner distances Euclidean) from a cell to a rectangle.
float cSC4PathFinder::DistanceToRectangle(int x, int z, SC4RectInt32 r) {
    if (!(x > r.x1 || x < r.x0 || z < r.z0 || z > r.z1)) return 0.0f;
    if (r.x0 <= x && x <= r.x1) return float(r.z0 <= z ? z - r.z1 : r.z0 - z);
    int cornerX;
    if (r.x1 < x) {
        if (r.z0 <= z && z <= r.z1) return float(x - r.x1);
        cornerX = r.x1;
    } else {
        cornerX = r.x0;
        if (r.z0 <= z && z <= r.z1) return float(r.x0 - x);
    }
    const int cornerZ = z < r.z0 ? r.z0 : r.z1;
    const float a[3] = {float(x), 0.0f, float(z)};
    const float b[3] = {float(cornerX), 0.0f, float(cornerZ)};
    return SC4GeometryUtilities::LengthOfLine(a, b);
}

// 0x1000d382c: the goal node has been reached - total cost, where the walk changes from foot (type 1) to a vehicle, the cost
// of the start node taken off, neighbour-connection toll, and the route as a list of cells.
void cSC4PathFinder::DoSuccessfulTrip() {
    TripNode* goal = goal_;
    tripLength_ = goal->cost;
    TripNode* cur = goal;
    for (TripNode* p = goal->parent; p; p = p->parent) {
        if (p->travelType == 1 && cur->travelType != 1) {
            reversalX_ = cur->x;
            reversalZ_ = cur->z;
            TripNode* next = cur->parent;
            std::uint8_t direction;
            if (next->x < cur->x) direction = 0;
            else if (next->x > cur->x) direction = 2;
            else direction = cur->z <= next->z ? 3 : 1;
            reversalDirection_ = direction;
        }
        cur = p;
    }
    float length = tripLength_ - cur->cost;
    if (!(0.0f <= length)) length = 0.0f;
    tripLength_ = length;
    if (destinationFilter_ == 0 && capacityKind_ != 3) {
        if (std::uint32_t(int(goal->x)) >= std::uint32_t(cityWidth_) || std::uint32_t(int(goal->z)) >= std::uint32_t(cityDepth_)) {
            float toll = 0.0f;
            if (host_->NeighborConnectionCost(goal->x, goal->z, toll)) tripLength_ += toll;
        }
    }
    if (route_) {
        route_->clear();
        for (TripNode* n = goal; n; n = n->parent) {
            RouteData data;
            data.x = std::uint8_t(n->x);
            data.z = std::uint8_t(n->z);
            data.travelType = n->travelType;
            route_->push_front(data);
        }
    }
}

}  // namespace sc4
