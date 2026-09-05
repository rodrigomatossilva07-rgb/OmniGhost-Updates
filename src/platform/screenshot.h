#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace OmniGhost::Screenshot {

// Screenshot format
enum class Format {
    PNG,
    BMP
};

// Screenshot result
struct Result {
    bool success = false;
    std::string path;
    std::string error;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t fileSize = 0;
};

// Capture the entire window
Result CaptureWindow(const std::string& filename, Format fmt = Format::PNG);

// Capture a specific region
Result CaptureRegion(const std::string& filename, int x, int y, int width, int height, Format fmt = Format::PNG);

// Capture the current ImGui viewport
Result CaptureViewport(const std::string& filename, Format fmt = Format::PNG);

// Generate a timestamped filename
std::string GenerateFilename(const std::string& prefix, Format fmt = Format::PNG);

// Ensure screenshot directory exists
bool EnsureScreenshotDirectory();

} // namespace OmniGhost::Screenshot