#include "sc4/SimStreamOps.h"

#include "sc4/DBSegment.h"

cIGZIStream& operator>>(cIGZIStream& s, cRZRandom& v) {
    v.Read(s);
    return s;
}
cIGZOStream& operator<<(cIGZOStream& s, const cRZRandom& v) {
    v.Write(s);
    return s;
}

cIGZIStream& operator>>(cIGZIStream& s, BoundaryIterator& v) {
    s.GetSint8(v.state);
    s.GetSint8(v.direction);
    s.GetSint32(v.side);
    s.GetSint32(v.step);
    s.GetSint32(v.length);
    s.GetSint32(v.rect.left);
    s.GetSint32(v.rect.top);
    s.GetSint32(v.rect.right);
    s.GetSint32(v.rect.bottom);
    return s;
}
cIGZOStream& operator<<(cIGZOStream& s, const BoundaryIterator& v) {
    s.SetSint8(v.state);
    s.SetSint8(v.direction);
    s.SetSint32(v.side);
    s.SetSint32(v.step);
    s.SetSint32(v.length);
    s << v.rect;
    return s;
}

cIGZIStream& operator>>(cIGZIStream& s, RandomIterator& v) {
    std::int8_t state = 0, modular = 0;
    s.GetSint8(state);
    s.GetSint8(modular);
    v.state_ = static_cast<RandomIterator::State>(state);
    v.modular_ = modular != 0;
    std::uint8_t dirty = 0;
    s.GetUint8(dirty);
    v.dirty_ = dirty != 0;
    s.GetSint32(v.minA_);
    s.GetSint32(v.minB_);
    s.GetSint32(v.maxA_);
    s.GetSint32(v.maxB_);
    s.GetSint32(v.count_);
    std::uint32_t listSize = 0;
    s.GetUint32(listSize);
    v.list_.resize(listSize);
    for (std::uint32_t i = 0; i < listSize; ++i) s.GetUint16(v.list_[i]);
    s.GetSint32(v.index_);
    s.GetSint32(v.current_);
    s.GetSint32(v.stride_);
    return s;
}
cIGZOStream& operator<<(cIGZOStream& s, const RandomIterator& v) {
    s.SetSint8(static_cast<std::int8_t>(v.state_));
    s.SetSint8(v.modular_ ? 1 : 0);
    s.SetUint8(v.dirty_ ? 1 : 0);
    s << cRZRect{v.minA_, v.minB_, v.maxA_, v.maxB_};
    s.SetSint32(v.count_);
    s.SetUint32(static_cast<std::uint32_t>(v.list_.size()));
    for (std::uint16_t e : v.list_) s.SetUint16(e);
    s.SetSint32(v.index_);
    s.SetSint32(v.current_);
    s.SetSint32(v.stride_);
    return s;
}

cIGZIStream& operator>>(cIGZIStream& s, ScanningIterator& v) {
    s.GetSint8(v.state);
    s.GetSint32(v.x);
    s.GetSint32(v.z);
    s.GetUint32(v.remaining);
    s.GetSint32(v.rect.left);
    s.GetSint32(v.rect.top);
    s.GetSint32(v.rect.right);
    s.GetSint32(v.rect.bottom);
    return s;
}
cIGZOStream& operator<<(cIGZOStream& s, const ScanningIterator& v) {
    s.SetSint8(v.state);
    s.SetSint32(v.x);
    s.SetSint32(v.z);
    s.SetUint32(v.remaining);
    s << v.rect;
    return s;
}

cIGZIStream& operator>>(cIGZIStream& s, std::list<sc4::cISC4Occupant*>& v) {
    v.clear();
    std::uint32_t count = 0;
    s.GetUint32(count);
    if (count < 10000000 && count != 0) {
        do {
            --count;
            void* object = nullptr;
            sc4::SC4ReadGZSerializable(s, 0x26bb767e, &object, false);
            v.push_back(static_cast<sc4::cISC4Occupant*>(object));
        } while (count != 0);
    }
    return s;
}
cIGZOStream& operator<<(cIGZOStream& s, const std::list<sc4::cISC4Occupant*>& v) {
    s.SetUint32(static_cast<std::uint32_t>(v.size()));
    for (sc4::cISC4Occupant* occupant : v) sc4::SC4WriteGZSerializable(s, reinterpret_cast<const cIGZUnknown*>(occupant));
    return s;
}

cIGZIStream& operator>>(cIGZIStream& s, cSC4ResponseCurve& v) {
    std::vector<SC4Point> points;
    std::uint32_t count = 0;
    s.GetUint32(count);
    points.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        s.GetFloat32(points[i].x);
        s.GetFloat32(points[i].y);
    }
    v.SetResponseCurveData(points);
    return s;
}
cIGZOStream& operator<<(cIGZOStream& s, const cSC4ResponseCurve& v) {
    std::vector<SC4Point> points;
    v.GetResponseCurveData(points);
    s.SetUint32(static_cast<std::uint32_t>(points.size()));
    for (const SC4Point& p : points) {
        s.SetFloat32(p.x);
        s.SetFloat32(p.y);
    }
    return s;
}

cIGZIStream& operator>>(cIGZIStream& s, DispatchInfo& v) {
    s.GetSint32(v.id);
    s.GetUint32(v.source);
    s.GetUint32(v.destination);
    s >> v.active;
    for (float& f : v.values) s.GetFloat32(f);
    sc4::SC4ReadGZSerializable(s, 0x26bb767e, reinterpret_cast<void**>(&v.vehicle), false);
    sc4::SC4ReadGZSerializable(s, 0xa9b40f05, reinterpret_cast<void**>(&v.target), false);
    sc4::SC4ReadGZSerializable(s, 0xc9b6d7d6, reinterpret_cast<void**>(&v.route), false);
    return s;
}
cIGZOStream& operator<<(cIGZOStream& s, const DispatchInfo& v) {
    s.SetSint32(v.id);
    s.SetUint32(v.source);
    s.SetUint32(v.destination);
    s << v.active;
    for (float f : v.values) s.SetFloat32(f);
    sc4::SC4WriteGZSerializable(s, v.vehicle);
    sc4::SC4WriteGZSerializable(s, v.target);
    sc4::SC4WriteGZSerializable(s, v.route);
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const SC4RectByte& v) {
    for (std::uint8_t b : v.v) s.SetUint8(b);
    return s;
}
cIGZOStream& operator<<(cIGZOStream& s, const SC4RectInt& v) {
    s.SetSint32(v.left);
    s.SetSint32(v.top);
    s.SetSint32(v.right);
    s.SetSint32(v.bottom);
    return s;
}
