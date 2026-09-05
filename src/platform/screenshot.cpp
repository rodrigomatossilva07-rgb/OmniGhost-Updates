#include "screenshot.h"
#include "app_paths.h"
#include <Windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

namespace OmniGhost::Screenshot {

static fs::path GetScreenshotDirectory() {
    return OmniGhost::Paths::Cache() / "screenshots";
}

bool EnsureScreenshotDirectory() {
    try {
        fs::path dir = GetScreenshotDirectory();
        if (!fs::exists(dir)) {
            fs::create_directories(dir);
        }
        return fs::exists(dir) && fs::is_directory(dir);
    } catch (const std::exception& ex) {
        std::cerr << "[Platform] EnsureScreenshotDirectory failed: " << ex.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "[Platform] EnsureScreenshotDirectory failed with unknown exception\n";
        return false;
    }
}

std::string GenerateFilename(const std::string& prefix, Format fmt) {
    if (!EnsureScreenshotDirectory()) {
        return "";
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time_t);
    
    std::ostringstream oss;
    oss << prefix << "_" 
        << std::put_time(&tm, "%Y%m%d_%H%M%S");
    
    // Add milliseconds
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    oss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    
    if (fmt == Format::PNG) {
        oss << ".png";
    } else {
        oss << ".bmp";
    }
    
    return (GetScreenshotDirectory() / oss.str()).string();
}

// BMP file header structures
#pragma pack(push, 1)
struct BMPFileHeader {
    uint16_t signature = 0x4D42; // "BM"
    uint32_t fileSize = 0;
    uint16_t reserved1 = 0;
    uint16_t reserved2 = 0;
    uint32_t dataOffset = 54;
};

struct BMPInfoHeader {
    uint32_t size = 40;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 1;
    uint16_t bitsPerPixel = 32;
    uint32_t compression = 0;
    uint32_t imageSize = 0;
    int32_t xPixelsPerMeter = 0;
    int32_t yPixelsPerMeter = 0;
    uint32_t colorsUsed = 0;
    uint32_t colorsImportant = 0;
};
#pragma pack(pop)

Result SaveBMP(const std::string& filename, const uint8_t* pixels, uint32_t width, uint32_t height) {
    Result result;
    result.width = width;
    result.height = height;
    
    const uint32_t rowSize = ((width * 32 + 31) / 32) * 4;
    const uint32_t imageSize = rowSize * height;
    
    BMPFileHeader fileHeader;
    fileHeader.fileSize = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + imageSize;
    
    BMPInfoHeader infoHeader;
    infoHeader.width = static_cast<int32_t>(width);
    infoHeader.height = -static_cast<int32_t>(height); // Negative = top-down
    infoHeader.bitsPerPixel = 32;
    infoHeader.imageSize = imageSize;
    
    try {
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            result.error = "Failed to open file for writing";
            return result;
        }
        
        file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));
        
        // Write pixel data (BMP expects BGR order, top-down)
        std::vector<uint8_t> rowBuffer(rowSize);
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* srcRow = pixels + y * width * 4;
            uint8_t* dst = rowBuffer.data();
            for (uint32_t x = 0; x < width; ++x) {
                dst[0] = srcRow[2]; // B
                dst[1] = srcRow[1]; // G
                dst[2] = srcRow[0]; // R
                dst[3] = srcRow[3]; // A
                dst += 4;
            }
            file.write(reinterpret_cast<const char*>(rowBuffer.data()), rowSize);
        }
        
        result.path = filename;
        result.fileSize = fs::file_size(filename);
        result.success = true;
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    
    return result;
}

Result CaptureInternal(const std::string& filename, int x, int y, int width, int height, Format fmt) {
    Result result;
    
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        result.error = "No foreground window";
        return result;
    }
    
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) {
        result.error = "Failed to get window rect";
        return result;
    }
    
    int winWidth = rect.right - rect.left;
    int winHeight = rect.bottom - rect.top;
    
    if (width <= 0) width = winWidth;
    if (height <= 0) height = winHeight;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    
    // Adjust for window client area
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    POINT clientTopLeft = { clientRect.left, clientRect.top };
    ClientToScreen(hwnd, &clientTopLeft);
    
    int borderX = clientTopLeft.x - rect.left;
    int borderY = clientTopLeft.y - rect.top;
    
    int captureX = rect.left + borderX + x;
    int captureY = rect.top + borderY + y;
    
    HDC hScreenDC = GetDC(nullptr);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    
    BOOL success = BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, captureX, captureY, SRCCOPY | CAPTUREBLT);
    
    if (!success) {
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(nullptr, hScreenDC);
        result.error = "BitBlt failed";
        return result;
    }
    
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -static_cast<LONG>(height); // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    std::vector<uint8_t> pixels(width * height * 4);
    GetDIBits(hScreenDC, hBitmap, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);
    
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(nullptr, hScreenDC);
    
    if (fmt == Format::BMP) {
        result = SaveBMP(filename, pixels.data(), width, height);
    } else {
        // For PNG, we'll save as BMP for now (PNG would require additional library)
        result = SaveBMP(filename, pixels.data(), width, height);
        // Rename to .png extension if needed
        if (result.success) {
            fs::path bmpPath(filename);
            fs::path pngPath = bmpPath.replace_extension(".png");
            fs::rename(bmpPath, pngPath);
            result.path = pngPath.string();
            result.fileSize = fs::file_size(pngPath);
        }
    }
    
    result.width = width;
    result.height = height;
    return result;
}

Result CaptureWindow(const std::string& filename, Format fmt) {
    if (filename.empty()) {
        return CaptureInternal(GenerateFilename("window", fmt), 0, 0, 0, 0, fmt);
    }
    return CaptureInternal(filename, 0, 0, 0, 0, fmt);
}

Result CaptureRegion(const std::string& filename, int x, int y, int width, int height, Format fmt) {
    return CaptureInternal(filename, x, y, width, height, fmt);
}

Result CaptureViewport(const std::string& filename, Format fmt) {
    // Capture ImGui viewport - for now just capture the window
    return CaptureWindow(filename, fmt);
}

} // namespace OmniGhost::Screenshot