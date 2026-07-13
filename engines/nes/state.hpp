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
    void io_raw(void* dst, size_t n) { raw(dst, n); }
};

}  // namespace famemu::nes
