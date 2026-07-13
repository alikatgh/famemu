// Gate 1 of the clean-room NES core (docs/PLATFORM.md): execute nestest.nes
// in automation mode (entry $C000) and match the golden log instruction-for-
// instruction — PC, A, X, Y, P, SP, and the cycle counter.
//
//   ./nestest_harness nestest.nes nestest.log
//
// Exit 0 = every line matched and the ROM's own result bytes ($0002/$0003)
// report no failure. Any divergence prints expected-vs-got with context.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../cart.hpp"
#include "../cpu.hpp"

using namespace famemu::nes;

namespace {

// nestest automation never renders: RAM + cart is enough. IO reads return 0.
struct NestestBus : Bus {
    uint8_t ram[0x800] = {0};
    uint8_t sram[0x2000] = {0};
    Cart& cart;
    explicit NestestBus(Cart& c) : cart(c) {}

    uint8_t read(uint16_t a) override {
        if (a < 0x2000) return ram[a & 0x7FF];
        if (a >= 0x8000) return cart.cpu_read(a);
        if (a >= 0x6000) return sram[a - 0x6000];
        return 0;  // PPU/APU/IO stubs
    }
    void write(uint16_t a, uint8_t v) override {
        if (a < 0x2000) ram[a & 0x7FF] = v;
        else if (a >= 0x8000) cart.cpu_write(a, v);
        else if (a >= 0x6000) sram[a - 0x6000] = v;
        // else: IO ignored
    }
};

struct GoldenLine {
    uint16_t pc;
    uint8_t a, x, y, p, sp;
    uint64_t cyc;
    std::string text;
};

bool parse_line(const std::string& s, GoldenLine& g) {
    unsigned pc;
    if (std::sscanf(s.c_str(), "%4x", &pc) != 1) return false;
    const char* f = std::strstr(s.c_str(), "A:");
    const char* c = std::strstr(s.c_str(), "CYC:");
    if (!f || !c) return false;
    unsigned a, x, y, p, sp;
    unsigned long long cyc;
    if (std::sscanf(f, "A:%2x X:%2x Y:%2x P:%2x SP:%2x", &a, &x, &y, &p, &sp) != 5)
        return false;
    if (std::sscanf(c, "CYC:%llu", &cyc) != 1) return false;
    g = {static_cast<uint16_t>(pc), static_cast<uint8_t>(a), static_cast<uint8_t>(x),
         static_cast<uint8_t>(y),  static_cast<uint8_t>(p), static_cast<uint8_t>(sp),
         cyc, s};
    return true;
}

std::vector<uint8_t> read_file(const char* path) {
    std::vector<uint8_t> out;
    if (std::FILE* fp = std::fopen(path, "rb")) {
        std::fseek(fp, 0, SEEK_END);
        long sz = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (sz > 0) {
            out.resize(static_cast<size_t>(sz));
            if (std::fread(out.data(), 1, out.size(), fp) != out.size()) out.clear();
        }
        std::fclose(fp);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <nestest.nes> <nestest.log>\n", argv[0]);
        return 2;
    }
    const auto rom = read_file(argv[1]);
    Cart cart;
    if (rom.empty() || !cart.load(rom.data(), rom.size())) {
        std::fprintf(stderr, "cannot load ROM %s\n", argv[1]);
        return 2;
    }

    // Golden log.
    std::vector<GoldenLine> golden;
    if (std::FILE* fp = std::fopen(argv[2], "rb")) {
        char buf[512];
        while (std::fgets(buf, sizeof buf, fp)) {
            GoldenLine g;
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            if (parse_line(line, g)) golden.push_back(std::move(g));
        }
        std::fclose(fp);
    }
    if (golden.empty()) {
        std::fprintf(stderr, "cannot parse golden log %s\n", argv[2]);
        return 2;
    }

    NestestBus bus(cart);
    Cpu6502 cpu(bus);
    // nestest automation entry state (matches the golden log's first line).
    cpu.pc = 0xC000;
    cpu.s = 0xFD;
    cpu.p = Cpu6502::I | Cpu6502::U;  // $24
    cpu.a = cpu.x = cpu.y = 0;
    cpu.cyc = 7;

    for (size_t i = 0; i < golden.size(); ++i) {
        const GoldenLine& g = golden[i];
        const bool ok = cpu.pc == g.pc && cpu.a == g.a && cpu.x == g.x &&
                        cpu.y == g.y && cpu.p == g.p && cpu.s == g.sp &&
                        cpu.cyc == g.cyc;
        if (!ok) {
            std::fprintf(stderr, "MISMATCH at line %zu\n", i + 1);
            for (size_t k = (i >= 3 ? i - 3 : 0); k < i; ++k)
                std::fprintf(stderr, "        %s\n", golden[k].text.c_str());
            std::fprintf(stderr, "expect: %s\n", g.text.c_str());
            std::fprintf(stderr,
                         "got:    %04X%*sA:%02X X:%02X Y:%02X P:%02X SP:%02X "
                         "CYC:%llu\n",
                         cpu.pc, 44, "", cpu.a, cpu.x, cpu.y, cpu.p, cpu.s,
                         static_cast<unsigned long long>(cpu.cyc));
            return 1;
        }
        cpu.step();
    }

    // nestest's own verdict bytes.
    const uint8_t r2 = bus.read(0x0002), r3 = bus.read(0x0003);
    if (r2 != 0 || r3 != 0) {
        std::fprintf(stderr, "trace matched but nestest reports $02=%02X $03=%02X\n",
                     r2, r3);
        return 1;
    }
    std::printf("nestest: %zu/%zu lines matched, result bytes clean — PASS\n",
                golden.size(), golden.size());
    return 0;
}
