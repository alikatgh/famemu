// famemu NES engine — tiny save-state serializer. Explicit field lists per
// component (no object memcpy: classes hold references/pointers), one
// versioned blob. Deterministic and endian-stable enough for same-platform
// suspend/resume, which is what the store titles need.
#pragma once

#include <cstdint>
#include <cstring>

namespace famemu::nes {

// Both sides expose io(T&)/raw(...) so components declare ONE field list:
//   template <class S> void serialize(S& s) { s.io(a_); s.io(b_); ... }
struct StateWriter {
    uint8_t* p;
    size_t left;
    bool ok = true;
    void raw(const void* src, size_t n) {
        if (!ok || n > left) { ok = false; return; }
        std::memcpy(p, src, n);
        p += n;
        left -= n;
    }
    template <class T>
    void io(const T& v) { raw(&v, sizeof v); }
    void io_raw(const void* src, size_t n) { raw(src, n); }
};

struct StateReader {
    const uint8_t* p;
    size_t left;
    bool ok = true;
    void raw(void* dst, size_t n) {
        if (!ok || n > left) { ok = false; return; }
        std::memcpy(dst, p, n);
        p += n;
        left -= n;
    }
    template <class T>
    void io(T& v) { raw(&v, sizeof v); }
    // A corrupt/tampered state can hold a byte other than 0/1 where a bool was
    // written; loading that straight into a bool is undefined behavior (UBSan
    // flags it, and a future compiler may optimize on the "bool is 0/1" premise).
    // Normalize on read. Valid states hold 0/1, so this round-trips identically.
    void io(bool& v) {
        uint8_t bytes[sizeof(bool)] = {0};   // consume the same width the writer wrote
        raw(bytes, sizeof bytes);
        if (ok) {
            bool any = false;
            for (uint8_t x : bytes) any = any || (x != 0);
            v = any;
        }
    }
    void io_raw(void* dst, size_t n) { raw(dst, n); }
};

}  // namespace famemu::nes
