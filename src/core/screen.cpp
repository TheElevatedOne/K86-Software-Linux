#include "screen.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

namespace k86 {

LoadedImage loadImage(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.empty()) throw std::runtime_error(path + " is empty");

    LoadedImage out;
    bool isGif = bytes.size() > 6 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F';
    if (isGif) {
        int* delays = nullptr;
        int w = 0, h = 0, frames = 0, comp = 0;
        stbi_uc* data = stbi_load_gif_from_memory(bytes.data(), int(bytes.size()), &delays, &w, &h, &frames, &comp, 4);
        if (!data) throw std::runtime_error(std::string("cannot decode GIF: ") + stbi_failure_reason());
        size_t frameBytes = size_t(w) * h * 4;
        for (int i = 0; i < frames; ++i) {
            RgbaImage img{w, h, std::vector<uint8_t>(data + i * frameBytes, data + (i + 1) * frameBytes)};
            out.frames.push_back(std::move(img));
            out.delaysMs.push_back(delays ? delays[i] : 0);
        }
        stbi_image_free(data);
        STBI_FREE(delays);
        return out;
    }
    int w = 0, h = 0, comp = 0;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4);
    if (!data) throw std::runtime_error(std::string("cannot decode image: ") + stbi_failure_reason());
    out.frames.push_back({w, h, std::vector<uint8_t>(data, data + size_t(w) * h * 4)});
    out.delaysMs.push_back(0);
    stbi_image_free(data);
    return out;
}

std::vector<uint32_t> fitToScreen(const RgbaImage& src, int W, int H) {
    std::vector<uint32_t> out(size_t(W) * H, 0);
    if (src.width <= 0 || src.height <= 0) return out;
    // calculateAutoSize() of the original software.
    double n = src.width, a = src.height;
    int o, l;
    if (double(W) / H < n / a) {
        o = W;
        l = int(std::ceil(W * (a / n)));
    } else {
        o = int(std::ceil(H * (n / a)));
        l = H;
    }
    int x0 = int(std::round((W - o) / 2.0)), y0 = int(std::round((H - l) / 2.0));

    // Area-average sampling of the source rectangle covered by each destination pixel.
    for (int y = 0; y < l; ++y) {
        int dy = y + y0;
        if (dy < 0 || dy >= H) continue;
        double sy0 = y * a / l, sy1 = (y + 1) * a / l;
        for (int x = 0; x < o; ++x) {
            int dx = x + x0;
            if (dx < 0 || dx >= W) continue;
            double sx0 = x * n / o, sx1 = (x + 1) * n / o;
            double acc[4] = {0, 0, 0, 0}, wsum = 0;
            for (int sy = int(sy0); sy < int(std::ceil(sy1)) && sy < src.height; ++sy) {
                double wy = std::min(sy1, sy + 1.0) - std::max(sy0, double(sy));
                for (int sx = int(sx0); sx < int(std::ceil(sx1)) && sx < src.width; ++sx) {
                    double wx = std::min(sx1, sx + 1.0) - std::max(sx0, double(sx));
                    double w = wx * wy;
                    const uint8_t* p = &src.rgba[(size_t(sy) * src.width + sx) * 4];
                    double alpha = p[3] / 255.0;
                    for (int c = 0; c < 3; ++c) acc[c] += p[c] * alpha * w;
                    acc[3] += alpha * w;
                    wsum += w;
                }
            }
            if (wsum <= 0 || acc[3] <= 0) continue;  // fully transparent stays black
            // Colours are un-premultiplied like canvas getImageData().
            uint32_t r = uint32_t(std::lround(std::clamp(acc[0] / acc[3], 0.0, 255.0)));
            uint32_t g = uint32_t(std::lround(std::clamp(acc[1] / acc[3], 0.0, 255.0)));
            uint32_t b = uint32_t(std::lround(std::clamp(acc[2] / acc[3], 0.0, 255.0)));
            out[size_t(dy) * W + dx] = r << 16 | g << 8 | b;
        }
    }
    return out;
}

Rect contentRect(const std::vector<uint32_t>& px, int W, int H) {
    int minX = W, minY = H, maxX = -1, maxY = -1;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (px[size_t(y) * W + x] != 0) {
                minX = std::min(minX, x), maxX = std::max(maxX, x);
                minY = std::min(minY, y), maxY = std::max(maxY, y);
            }
    if (maxX < 0) return {};
    return {uint16_t(minX), uint16_t(minY), uint16_t(maxX + 1), uint16_t(maxY + 1)};
}

Rect unionRect(const std::vector<Rect>& rects) {
    if (rects.empty()) return {};
    Rect u = rects[0];
    for (const auto& r : rects) {
        u.left = std::min(u.left, r.left), u.top = std::min(u.top, r.top);
        u.right = std::max(u.right, r.right), u.bottom = std::max(u.bottom, r.bottom);
    }
    return u;
}

ScreenImage toScreenImage(const std::vector<uint32_t>& px, int W, const Rect& r) {
    ScreenImage img;
    img.left = r.left, img.top = r.top, img.right = r.right, img.bottom = r.bottom;
    for (int x = r.left; x < r.right; ++x)
        for (int y = r.top; y < r.bottom; ++y) {
            uint32_t c = px[size_t(y) * W + x];
            uint16_t v = uint16_t(((c >> 16 & 0xFF) >> 3) << 11 | ((c >> 8 & 0xFF) >> 2) << 5 | ((c & 0xFF) >> 3));
            img.data.push_back(uint8_t(v >> 8));
            img.data.push_back(uint8_t(v & 0xFF));
        }
    return img;
}

}  // namespace k86
