#pragma once

namespace bakugan {

/* Subtract a small CPU wake margin when VSync is active so Present(1, 0) absorbs
 * the final sub-vblank wait instead of overshooting onto the next vblank
 * (e.g. 5 vblanks / 28.80 FPS instead of 4.8 vblanks / 30.00 FPS at 144 Hz).
 * `next` still advances by exact `1.0 / cap` so the long-term rate never accelerates. */
inline double fps_cap_step(double now, double interval, bool vsync_on, double* next)
{
    constexpr double kVsyncWakeMarginSec = 0.0015;
    if (*next == 0.0) *next = now;
    *next += interval;
    const double margin = (vsync_on && interval > kVsyncWakeMarginSec * 2.0) ? kVsyncWakeMarginSec : 0.0;
    const double target = *next - margin;
    if (target > now) return target - now;
    if (now - *next > interval) *next = now; /* a long stall is not replayed as a burst of frames */
    return 0.0;
}

} // namespace bakugan
