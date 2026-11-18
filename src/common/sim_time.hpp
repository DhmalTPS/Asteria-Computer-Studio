#pragma once
// SimTime: a point in, or a span of, simulated time, measured in femtoseconds (decision D9).
//
// Why femtoseconds? Clock periods are not whole picoseconds. DDR4-2400's command clock runs
// at 1.2 GHz, a period of 833.33 ps. In femtoseconds that is 833'333, an error of 0.00004%.
// A u64 of femtoseconds overflows only after ~5.1 hours of simulated time, trillions of cycles.
//
// Why a class instead of a bare u64? So a time can never be mixed up with a cycle count. That
// confusion is exactly the bug D8 guards against: a 50 ns DRAM access is 63 cycles on the
// 800 ps single-cycle CPU and 228 cycles on the 220 ps pipeline. Converting between the two
// must be explicit (cycles_to_cover).

#include <compare>

#include "common/assert.hpp"
#include "common/types.hpp"

namespace asteria {

class SimTime {
public:
    constexpr SimTime() noexcept = default;

    // Named constructors make the unit visible at every call site: SimTime::ns(50).
    [[nodiscard]] static constexpr SimTime fs(u64 v) noexcept { return SimTime{v}; }
    [[nodiscard]] static constexpr SimTime ps(u64 v) noexcept { return SimTime{v * 1'000}; }
    [[nodiscard]] static constexpr SimTime ns(u64 v) noexcept { return SimTime{v * 1'000'000}; }
    [[nodiscard]] static constexpr SimTime us(u64 v) noexcept { return SimTime{v * 1'000'000'000}; }

    // The period of a clock running at `hertz`, rounded down to a whole femtosecond.
    [[nodiscard]] static constexpr SimTime period_of_hz(u64 hertz) noexcept {
        ASTERIA_ASSERT(hertz != 0, "clock frequency must be non-zero");
        return SimTime{kFemtosecondsPerSecond / hertz};
    }

    [[nodiscard]] constexpr u64 femtoseconds() const noexcept { return fs_; }
    [[nodiscard]] constexpr double nanoseconds() const noexcept {
        return static_cast<double>(fs_) / 1e6;
    }

    constexpr auto operator<=>(const SimTime&) const noexcept = default;

    constexpr SimTime& operator+=(SimTime other) noexcept {
        fs_ += other.fs_;
        return *this;
    }
    [[nodiscard]] friend constexpr SimTime operator+(SimTime a, SimTime b) noexcept {
        return a += b;
    }
    [[nodiscard]] friend constexpr SimTime operator-(SimTime a, SimTime b) noexcept {
        ASTERIA_ASSERT(a >= b, "simulated time cannot be negative");
        return SimTime{a.fs_ - b.fs_};
    }
    [[nodiscard]] friend constexpr SimTime operator*(SimTime t, u64 n) noexcept {
        return SimTime{t.fs_ * n};
    }

private:
    static constexpr u64 kFemtosecondsPerSecond = 1'000'000'000'000'000;

    constexpr explicit SimTime(u64 femtoseconds) noexcept : fs_{femtoseconds} {}

    u64 fs_ = 0;
};

// The number of whole cycles of `period` needed to cover `duration`: ceil(duration / period).
// A component that needs 50 ns cannot finish in a fraction of a cycle. The result is
// visible on the clock edge AFTER the time has elapsed, hence rounding up.
[[nodiscard]] constexpr u64 cycles_to_cover(SimTime duration, SimTime period) noexcept {
    ASTERIA_ASSERT(period.femtoseconds() != 0, "clock period must be non-zero");
    const u64 d = duration.femtoseconds();
    const u64 p = period.femtoseconds();
    return d / p + (d % p != 0 ? 1 : 0);
}

}  // namespace asteria
