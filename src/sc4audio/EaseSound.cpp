#include "sc4audio/EaseSound.h"

#include <cmath>

void cEaseSound::SetPosition(const float position[3], bool snap) {
    target_[0] = position[0]; target_[1] = position[1]; target_[2] = position[2];
    if (snap || !updated_) {
        current_[0] = position[0]; current_[1] = position[1]; current_[2] = position[2];
    }
}

void cEaseSound::GetPosition(float out[3]) const {
    out[0] = current_[0]; out[1] = current_[1]; out[2] = current_[2];
}

void cEaseSound::SetVolume(int volume, bool snap) {
    targetVolume_ = volume;
    if (snap || !updated_) currentVolume_ = volume;
}

void cEaseSound::Update(float seconds) {
    const float d[3] = {target_[0] - current_[0], target_[1] - current_[1], target_[2] - current_[2]};
    const float distance = std::sqrt(d[2] * d[2] + d[0] * d[0] + d[1] * d[1]);
    if (distance != 0.0f) {
        const float inverse = 1.0f / distance;
        const float step = std::fmin(positionStep_ * seconds, distance);
        current_[0] += d[0] * inverse * step;
        current_[1] += d[1] * inverse * step;
        current_[2] += d[2] * inverse * step;
    }
    const int volumeDelta = targetVolume_ - currentVolume_;
    if (volumeDelta != 0) {
        const float delta = static_cast<float>(volumeDelta);
        const float magnitude = std::fabs(delta);
        const float step = std::fmin(seconds * volumeStep_, magnitude);
        currentVolume_ += static_cast<int>(step * (delta / magnitude));
    }
    updated_ = true;
}
