#include "audio/CaptureSampleClock.h"
#include <cmath>
#include <stdexcept>
int main()
{
    singlilt::CaptureSampleClock clock;
    auto first = clock.next(10.01, 480, 48000);
    auto delayed = clock.next(10.25, 480, 48000);
    auto third = clock.next(10.30, 960, 48000);
    if (std::abs(first - 10) > 1e-9 || std::abs(delayed - 10.01) > 1e-9 || std::abs(third - 10.02) > 1e-9)
        throw std::runtime_error("callback delivery jitter changed capture clock");
}
