#include "sc4/RandomIterator.h"

#include <cstdlib>
#include <utility>

namespace {

constexpr int kPrimeCount = 70;
constexpr std::uint16_t kPrimeNumbers[kPrimeCount] = {
    1,    3,    5,    7,    11,   13,   17,   19,   23,   29,    31,    37,    41,    43,    47,    53,    59,    61,
    67,   71,   73,   79,   83,   89,   97,   101,  103,  107,   109,   113,   179,   211,   257,   307,   373,   479,
    577,  641,  719,  857,  977,  1063, 1237, 1471, 1663, 1993,  2221,  2539,  2909,  3259,  3733,  4507,  5281,  5861,
    6481, 6947, 7573, 9013, 10103, 12227, 15683, 17431, 19801, 21569, 23633, 26003, 27529, 29683, 32051, 32611};

constexpr int kMaxCount = 0x1fd8c;

// First prime index tried for a given element count (the table is indexed in size brackets).
int BracketStart(int count) {
    if (count < 0x1d) return 0;
    if (count < 0x47) return 10;
    if (count < 0x71) return 20;
    if (count < 0x359) return 30;
    if (count < 0xcbb) return 40;
    if (count < 0x2fc3) return 50;
    return 60;
}

// A stride that is >= count/4 and does not divide count (the last prime when none qualifies).
// `beyondTable` is what the original reads one slot past the 70 primes (its two copies of the table
// are followed by different bytes: 65024 for RandomIterator, 0 for LinearRandomIterator).
int PickStride(int count, int beyondTable) {
    int index = BracketStart(count) - 1;
    int value;
    do {
        value = kPrimeNumbers[++index];
    } while (value < count / 4 && index < kPrimeCount - 1);
    // the original checked `value < count/4` before advancing past the match; its second search
    // starts at the slot after the first prime >= count/4
    int j = index + 1;
    for (;;) {
        const int candidate = j < kPrimeCount ? kPrimeNumbers[j] : beyondTable;
        if (j > 0x45) return candidate;
        ++j;
        if (count % candidate != 0) return candidate;
    }
}

template <class T>
void Shuffle(std::vector<T>& v) {
    for (std::size_t i = v.size(); i > 1; --i) {
        const std::size_t j = static_cast<std::size_t>(std::rand()) % i;
        std::swap(v[i - 1], v[j]);
    }
}

}  // namespace

void RandomIterator::Reset(bool randomize) {
    count_ = (maxB_ + 1 - minB_) * (maxA_ + 1 - minA_);
    state_ = kRunning;
    index_ = 0;
    if (modular_) {
        if (count_ > 0x1fd8b) count_ = kMaxCount;
        stride_ = PickStride(count_, 65024);
        if (randomize && count_ > 0) current_ = std::rand() % count_;
        return;
    }
    if (static_cast<int>(list_.size()) != count_) list_.resize(static_cast<std::size_t>(count_ > 0 ? count_ : 0));
    std::size_t i = 0;
    for (int a = minA_; a <= maxA_; ++a)
        for (int b = minB_; b <= maxB_ && i < list_.size(); ++b)
            list_[i++] = static_cast<std::uint16_t>((b & 0xff) | (a << 8));
    if (randomize) Shuffle(list_);
}

bool RandomIterator::Next(int& a, int& b) {
    if (state_ == kDone) return false;
    if (!modular_) {
        if (index_ >= static_cast<int>(list_.size())) {
            state_ = kDone;
            return false;
        }
        const std::uint16_t packed = list_[static_cast<std::size_t>(index_++)];
        b = packed & 0xff;
        a = packed >> 8;
        if (index_ == static_cast<int>(list_.size())) state_ = kDone;
        return true;
    }
    const int taken = index_++;
    if (count_ <= taken) {
        state_ = kDone;
        return false;
    }
    current_ = static_cast<int>(static_cast<long>(current_ + stride_) % count_);
    const long width = static_cast<long>(maxA_ + 1 - minA_);
    a = static_cast<int>(current_ % width) + minA_;
    b = static_cast<int>(current_ / width) + minB_;
    if (index_ == count_) state_ = kDone;
    return true;
}

void LinearRandomIterator::Reset(bool randomize) {
    state_ = kRunning;
    index_ = 0;
    if (count_ == 0) {
        state_ = kDone;
        current_ = 0;
        index_ = 0;
        dirty_ = true;
        return;
    }
    if (modular_) {
        if (count_ > 0x1fd8b) count_ = kMaxCount;
        stride_ = PickStride(count_, 0);
        if (randomize) current_ = std::rand() % count_;
        return;
    }
    if (static_cast<int>(list_.size()) != count_) {
        list_.resize(static_cast<std::size_t>(count_));
        dirty_ = true;
    }
    for (int i = 0; i < count_; ++i) list_[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(i);
    if (randomize) Shuffle(list_);
}

bool LinearRandomIterator::Next(std::uint32_t& value) {
    if (state_ == kDone) return false;
    if (!modular_) {
        if (index_ >= static_cast<int>(list_.size())) {
            state_ = kDone;
            return false;
        }
        value = list_[static_cast<std::size_t>(index_++)];
        return true;
    }
    if (count_ <= index_) {
        state_ = kDone;
        return false;
    }
    current_ = static_cast<int>(static_cast<long>(current_ + stride_) % count_);
    value = static_cast<std::uint32_t>(current_);
    ++index_;
    return true;
}
