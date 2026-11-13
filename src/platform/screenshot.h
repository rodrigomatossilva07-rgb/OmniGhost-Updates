#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace OmniGhost::Screenshot {


enum class Format {
    PNG,
    BMP
};


struct Result {
    bool success = false;
    std::string path;
    std::string error;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t fileSize = 0;
};


Result CaptureWindow(const std::string& filename, Format fmt = Format::PNG);


Result CaptureRegion(const std::string& filename, int x, int y, int width, int height, Format fmt = Format::PNG);


Result CaptureViewport(const std::string& filename, Format fmt = Format::PNG);


std::string GenerateFilename(const std::string& prefix, Format fmt = Format::PNG);


bool EnsureScreenshotDirectory();

}