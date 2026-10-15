#include "s3d/AnimatedInstance.h"

#include <cmath>

#pragma STDC FP_CONTRACT OFF

namespace s3d {

bool FramesetInfo::IncrementFrame() {
    const std::uint16_t start = starts[current];
    const unsigned end = unsigned(frameCount) + start;
    if (end <= unsigned(start) + 1) return true;              // a set of at most one frame never moves
    if (direction <= 0) {
        if (direction >= 0) return true;                      // stopped
        // moving backwards
        if (start < absoluteFrame) {
            absoluteFrame = std::uint16_t(absoluteFrame - 1);
            return true;
        }
        if (playMode == kPlayPingPong) {
            absoluteFrame = std::uint16_t(absoluteFrame + 1);
            direction = 1;
        } else if (playMode == kPlayLoop) {
            absoluteFrame = std::uint16_t(end - 1);
        } else {
            direction = 0;
        }
        return true;
    }
    // moving forwards
    if (end <= unsigned(absoluteFrame) + 1) {
        if (playMode == kPlayPingPong) {
            absoluteFrame = std::uint16_t(absoluteFrame - 1);
            direction = -1;
        } else if (playMode == kPlayLoop) {
            absoluteFrame = start;
        } else {
            direction = 0;
        }
        return true;
    }
    absoluteFrame = std::uint16_t(absoluteFrame + 1);
    return true;
}

int cAnimatedInstanceState::SetFrameset(int index) {
    FramesetInfo* info = framesets;
    if (!info || info->current == index || index >= int(info->count)) return 0;
    info->current = std::uint16_t(index);
    info->frameCount = std::uint16_t(info->starts[std::size_t(index) + 1] - info->starts[std::size_t(index)]);
    const std::uint16_t frame = info->absoluteFrame;
    if (loadedFrame != frame && host_->LoadModelFrame(frame)) loadedFrame = frame;
    return 2;
}

int cAnimatedInstanceState::SetFrame(int frame) {
    FramesetInfo* info = framesets;
    if (!info) {
        if (loadedFrame == frame) return 2;
        if (!host_->LoadModelFrame(frame)) return 0;
        loadedFrame = std::uint16_t(frame);
        return 2;
    }
    if (int(info->frameCount) < frame) frame = frame % int(info->frameCount);
    info->absoluteFrame = std::uint16_t(info->starts[info->current] + frame);
    const std::uint16_t absolute = info->absoluteFrame;
    if (loadedFrame != absolute && host_->LoadModelFrame(absolute)) loadedFrame = absolute;
    return 2;
}

int cAnimatedInstanceState::GetFrame() const {
    if (!framesets) return loadedFrame;
    return int(framesets->absoluteFrame) - int(framesets->starts[framesets->current]);
}

int cAnimatedInstanceState::GetFrameCount() const {
    return framesets ? framesets->frameCount : host_->ModelFrameCount();
}

int cAnimatedInstanceState::IncrementFrame() {
    if (framesets) {
        framesets->IncrementFrame();
        const std::uint16_t absolute = framesets->absoluteFrame;
        if (loadedFrame != absolute && host_->LoadModelFrame(absolute)) loadedFrame = absolute;
        return 2;
    }
    const unsigned count = unsigned(host_->ModelFrameCount()) & 0xffff;
    if (count < 2) return 2;
    if (direction <= 0) {
        if (direction >= 0) return 2;
        if (loadedFrame != 0) {
            SetFrame(loadedFrame - 1);
            return 2;
        }
        if (playMode == kPlayPingPong) {
            SetFrame(1);
            direction = 1;
            return 2;
        }
        if (playMode != kPlayLoop) {
            direction = 0;
            frameAccumulator = 0.0f;
            return 2;
        }
        SetFrame(int(count) - 1);
        return 2;
    }
    const unsigned next = unsigned(loadedFrame) + 1;
    if (next < count) {
        SetFrame(int(next));
        return 2;
    }
    if (playMode == kPlayPingPong) {
        SetFrame(loadedFrame - 1);
        direction = -1;
        return 2;
    }
    if (playMode != kPlayLoop) {
        direction = 0;
        frameAccumulator = 0.0f;
        return 2;
    }
    SetFrame(0);
    return 2;
}

int cAnimatedInstanceState::SetAutoFrameRate(float rate) {
    if (rate == kAutoFrameRateDefault) rate = float(host_->ModelDefaultRate()) * 0.001f;
    autoFrameRate = rate;
    return 0;
}

int cAnimatedInstanceState::DoAutoFrameUpdate(float seconds) {
    if (autoFrameRate == kAutoFrameRateUnset) SetAutoFrameRate(kAutoFrameRateDefault);
    const float rate = autoFrameRate;
    if (rate == 0.0f) return 0;
    frameAccumulator = float(int(direction)) * rate * seconds + frameAccumulator;
    float magnitude = std::fabs(frameAccumulator);
    if (magnitude < 1.0f) return 0;
    if (2.0f <= magnitude) {
        if (catchUp) {
            do {
                IncrementFrame();
                frameAccumulator = float(-int(direction)) + frameAccumulator;
            } while (1.0f <= std::fabs(frameAccumulator));
            return 2;
        }
        IncrementFrame();
        frameAccumulator = 1.0f;
        return 2;
    }
    IncrementFrame();
    frameAccumulator = float(-int(direction)) + frameAccumulator;
    return 2;
}

void cAnimatedInstanceState::SetPlayMode(int mode) {
    playMode = std::uint8_t(mode);
    if (framesets) framesets->playMode = std::uint8_t(mode);
}

int cAnimatedInstanceState::SetOpacity(bool translucent, float value) {
    const std::uint8_t before = flags;
    flags = std::uint8_t(before & 0xfd);
    opacity = 1.0f;
    if (modelFlags & 4) flags = std::uint8_t(before | 2);
    if (translucent && value <= 0.99f && !(value == -100.0f) && !(value == -200.0f)) {
        flags = std::uint8_t(flags | 2);
        opacity = value;
    }
    return 2;
}

int cAnimatedInstanceState::SetHighlight(int value) {
    if ((value != 0) == bool(flags & 1) && highlight == value) return 0;
    flags = value == 0 ? std::uint8_t(flags & 0xfe) : std::uint8_t(flags | 1);
    highlight = std::uint8_t(value);
    return 2;
}

int cAnimatedInstanceState::SetSelfIlluminated(bool on) {
    if (bool((flags & 4) >> 2) == on) return 0;
    flags = on ? std::uint8_t(flags | 4) : std::uint8_t(flags & 0xfb);
    return 2;
}

int cAnimatedInstanceState::SetZoomAndRotation(int newZoom, int newRotation) {
    rotation = std::uint8_t(newRotation);
    if (zoomInstances.empty() || zoom == newZoom) return 0;
    zoom = std::uint8_t(newZoom);
    const std::uint32_t instance = zoomInstances[std::size_t(newZoom)];
    if (currentInstance == instance) return 0;
    currentInstance = instance;
    host_->LoadModelInstance(instance, loadedFrame);
    return 2;
}

}  // namespace s3d
