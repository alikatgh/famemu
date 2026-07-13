#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../dsp/frame.hpp"

namespace famidec {

// Write a decoded Frame as a 24-bit BMP (bottom-up, BGR). Frame stores R,G,B,A
// in byte order per pixel; the alpha byte is dropped. Handy for headless
// verification without SDL. Returns false if the file can't be opened.
inline bool write_bmp(const Frame& f, const std::string& path) {
    const int w = Frame::kWidth, h = Frame::kHeight;
    const int rowbytes = w * 3;  // 4-aligned for w=640
    const int imgsize = rowbytes * h;
    const int filesize = 54 + imgsize;
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    uint8_t hd[54] = {0};
    auto u32 = [&](int off, uint32_t v) {
        hd[off + 0] = static_cast<uint8_t>(v);
        hd[off + 1] = static_cast<uint8_t>(v >> 8);
        hd[off + 2] = static_cast<uint8_t>(v >> 16);
        hd[off + 3] = static_cast<uint8_t>(v >> 24);
    };
    hd[0] = 'B'; hd[1] = 'M';
    u32(2, static_cast<uint32_t>(filesize));
    hd[10] = 54; hd[14] = 40;
    u32(18, static_cast<uint32_t>(w));
    u32(22, static_cast<uint32_t>(h));
    hd[26] = 1; hd[28] = 24;
    u32(34, static_cast<uint32_t>(imgsize));
    if (std::fwrite(hd, 1, 54, fp) != 54) {
        std::fclose(fp);
        return false;
    }
    std::vector<uint8_t> row(static_cast<size_t>(rowbytes));
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            uint32_t px = f.rgba[static_cast<size_t>(y) * w + x];
            row[x * 3 + 0] = (px >> 16) & 0xff;  // B
            row[x * 3 + 1] = (px >> 8) & 0xff;   // G
            row[x * 3 + 2] = px & 0xff;          // R
        }
        if (std::fwrite(row.data(), 1, static_cast<size_t>(rowbytes), fp) != static_cast<size_t>(rowbytes)) {
            std::fclose(fp);
            return false;
        }
    }
    return std::fclose(fp) == 0;
}

}  // namespace famidec
