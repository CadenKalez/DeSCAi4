#include "sc4/AutoHelpers.h"

namespace sc4 {

namespace {
constexpr std::uint32_t kIIDSC4DBSegment = 0x499899da;
}

GamePauseServices& GetGamePauseServices() {
    static GamePauseServices services;
    return services;
}

void SC4GamePause(bool pause, bool graphics, bool animation, bool simulator, bool view) {
    GamePauseServices& s = GetGamePauseServices();
    if (graphics && s.graphics) s.graphics(pause);
    if (animation && s.animation) s.animation(pause);
    if (simulator && s.simulator) s.simulator(pause);
    if (view && s.view) s.view(pause);
}

cSC4AutoFlushInputMessageQueue::cSC4AutoFlushInputMessageQueue(std::uint32_t intervalMs)
    : interval_(intervalMs), timer_(cRZTimer::kMilliseconds) {
    timer_.Start();
}

void cSC4AutoFlushInputMessageQueue::Flush() {
    if (timer_.GetElapsedTimeU32() < interval_) return;
    if (GetGamePauseServices().flushInput) GetGamePauseServices().flushInput();
    timer_.Restart();
}

// ---- load stream ------------------------------------------------------------------------------------

cSC4AutoLoadStream::cSC4AutoLoadStream(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance) {
    cISC4DBSegment* sc4Segment = nullptr;
    if (segment) {
        if (segment->QueryInterface(kIIDSC4DBSegment, reinterpret_cast<void**>(&sc4Segment))) openStream(sc4Segment, type, group, instance);
        if (sc4Segment) sc4Segment->Release();
    }
}

bool cSC4AutoLoadStream::openStream(cISC4DBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance) {
    if (segment) {
        const cGZPersistResourceKey key{type, group, instance};
        if (stream_) {
            cISC4DBSegmentIStream* old = stream_;
            stream_ = nullptr;
            old->Release();
        }
        if (segment->OpenIStream(key, &stream_)) {
            stream_->GetUint16(version_);
            if (!stream_->GetError()) {
                if (segment_ != segment) {
                    segment->AddRef();
                    cISC4DBSegment* old = segment_;
                    segment_ = segment;
                    if (old) old->Release();
                }
                return true;
            }
            segment->CloseIStream(stream_);
            if (stream_) {
                cISC4DBSegmentIStream* s = stream_;
                stream_ = nullptr;
                s->Release();
            }
        }
    }
    return false;
}

cSC4AutoLoadStream::~cSC4AutoLoadStream() {
    if (segment_) {
        if (stream_) segment_->CloseIStream(stream_);
        cISC4DBSegment* s = segment_;
        segment_ = nullptr;
        s->Release();
    }
    if (stream_) {
        cISC4DBSegmentIStream* s = stream_;
        stream_ = nullptr;
        s->Release();
    }
}

// ---- save stream ------------------------------------------------------------------------------------

cSC4AutoSaveStream::cSC4AutoSaveStream(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance,
                                       std::uint16_t version) {
    cISC4DBSegment* sc4Segment = nullptr;
    if (segment) {
        if (segment->QueryInterface(kIIDSC4DBSegment, reinterpret_cast<void**>(&sc4Segment))) openStream(sc4Segment, type, group, instance, version);
        if (sc4Segment) sc4Segment->Release();
    }
}

bool cSC4AutoSaveStream::openStream(cISC4DBSegment* segment, std::uint32_t type, std::uint32_t group, std::uint32_t instance,
                                    std::uint16_t version) {
    if (segment) {
        const cGZPersistResourceKey key{type, group, instance};
        if (stream_) {
            cISC4DBSegmentOStream* old = stream_;
            stream_ = nullptr;
            old->Release();
        }
        if (segment->OpenOStream(key, &stream_, true)) {
            version_ = version;
            stream_->SetUint16(version);
            if (!stream_->GetError()) {
                if (segment_ != segment) {
                    segment->AddRef();
                    cISC4DBSegment* old = segment_;
                    segment_ = segment;
                    if (old) old->Release();
                }
            } else {
                segment->CloseOStream(stream_);
                if (stream_) {
                    cISC4DBSegmentOStream* s = stream_;
                    stream_ = nullptr;
                    s->Release();
                }
            }
        }
    }
    return IsOpen();
}

cSC4AutoSaveStream::~cSC4AutoSaveStream() {
    if (segment_) {
        if (stream_) segment_->CloseOStream(stream_);
        cISC4DBSegment* s = segment_;
        segment_ = nullptr;
        s->Release();
    }
    if (stream_) {
        cISC4DBSegmentOStream* s = stream_;
        stream_ = nullptr;
        s->Release();
    }
}

}  // namespace sc4
