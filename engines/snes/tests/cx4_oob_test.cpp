// CX4 OOB regression: a crafted wireframe header with vertex count > 128 and an
// edge index >= 128 used to read the local px[128]/py[128] stack arrays out of
// bounds in op_draw_wireframe (the guard was `a < nv`, but nv can be 255 while the
// arrays are 128). Now bounded to min(nv,128). Run under ASAN to catch it:
//   clang++ -std=c++20 -O1 -g -fsanitize=address,undefined -I.. \
//     tests/cx4_oob_test.cpp -o /tmp/cx4_oob && /tmp/cx4_oob
#include "../cx4.hpp"
#include <cstdio>

using famemu::snes::Cx4;

int main() {
    Cx4 cx;
    // Vertex header: nv=200 (>128), ne=1; only px[0..127] get computed.
    cx.write(0x0000, 200);
    cx.write(0x0001, 1);
    // Edge list at 0x0010 + nv*6; give it an index of 150 (>=128, <nv) — the OOB.
    const int edges = 0x0010 + 200 * 6;
    cx.write(static_cast<uint16_t>(edges), 150);
    cx.write(static_cast<uint16_t>(edges + 1), 10);
    cx.write(0x1F4F, 0x01);  // trigger op_draw_wireframe

    // Also exercise the other draw ops with degenerate inputs — must not OOB.
    cx.write(0x1F4F, 0x00);  // sprite
    cx.write(0x1F4F, 0x0D);  // taylor inverse
    std::printf("cx4 oob test: no out-of-bounds access — PASS\n");
    return 0;
}
