// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace singlilt
{
// UI delivery time anchors only the first block; subsequent sample times are continuous.
class CaptureSampleClock
{
  public:
    double next(double deliveryTime, int frames, int rate)
    {
        if (!initialized_)
        {
            anchor_ = deliveryTime - double(frames) / rate;
            initialized_ = true;
        }
        double first = anchor_ + double(frames_) / rate;
        frames_ += frames;
        return first;
    }

  private:
    bool initialized_ = false;
    double anchor_ = 0;
    std::int64_t frames_ = 0;
};
} // namespace singlilt
