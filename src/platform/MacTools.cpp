#include "platform/MacTools.h"

#include <time.h>

namespace MacTools {

// MacTools::Sleep(unsigned int) @ 0x100796c1a
void Sleep(std::uint32_t milliseconds) {
    timespec ts{};
    if (milliseconds == 0) {
        ts.tv_nsec = 1;
    } else {
        ts.tv_sec = milliseconds / 1000;
        ts.tv_nsec = static_cast<long>(milliseconds % 1000) * 1'000'000L;
    }
    nanosleep(&ts, nullptr);
}

}  // namespace MacTools
