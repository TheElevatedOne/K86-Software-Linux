// Converts images to the keyboard LCD format the same way the original software does:
// fit into the screen keeping the aspect ratio (centred, black/transparent border), crop to
// the bounding box of non-black pixels and emit RGB565 big-endian in column-major order.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "keyboard.hpp"

namespace k86 {

struct RgbaImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4
};

struct LoadedImage {
    std::vector<RgbaImage> frames;  // >1 for animated GIFs
    std::vector<int> delaysMs;      // per frame (GIF only)
};

// Loads PNG, JPEG, BMP, TGA, GIF (animated GIFs return all frames). Throws on failure.
LoadedImage loadImage(const std::string& path);

// Scales `src` into a width x height canvas (aspect fit, centred). Returns 0xRRGGBB per pixel.
std::vector<uint32_t> fitToScreen(const RgbaImage& src, int width, int height);

struct Rect {
    uint16_t left = 0, top = 0, right = 0, bottom = 0;
};

// Bounding box of pixels whose colour is not 0 (right/bottom exclusive).
Rect contentRect(const std::vector<uint32_t>& px, int width, int height);
Rect unionRect(const std::vector<Rect>& rects);

ScreenImage toScreenImage(const std::vector<uint32_t>& px, int width, const Rect& r);

}  // namespace k86
