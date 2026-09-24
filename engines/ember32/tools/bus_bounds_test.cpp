#include "ember32/bus.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    ember32::Bus bus;
    constexpr uint32_t end = ember32::Bus::RAM_SIZE;

    bus.w16(end - 2, 0xABCD);
    assert(bus.r16(end - 2) == 0xABCD);
    assert(bus.r16(end - 1) == 0);

    bus.w32(end - 4, 0x12345678);
    assert(bus.r32(end - 4) == 0x12345678);
    assert(bus.r32(end - 3) == 0);

    // These addresses used to wrap during the bounds check and then index far
    // outside the RAM allocation.
    constexpr uint32_t high = std::numeric_limits<uint32_t>::max();
    assert(bus.r16(high) == 0);
    assert(bus.r32(high) == 0);
    bus.w16(high, 0xFFFF);
    bus.w32(high, 0xFFFFFFFF);
    assert(bus.r32(end - 4) == 0x12345678);
}
