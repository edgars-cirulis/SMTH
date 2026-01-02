#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ImageRGBA8 {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

bool loadImageRGBA8_WIC(const std::string& path, ImageRGBA8& out, std::string& err);
