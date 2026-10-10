#include "screenshot.h"
#include "app_paths.h"
#include "scope_exit.h"
#include <Windows.h>
#include <wincodec.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <climits>

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
    
    if (!pixels || !width || !height || width > INT_MAX || height > INT_MAX ||
        uint64_t{width} * height * 4u > UINT_MAX - 54u) {
        result.error = "Invalid screenshot dimensions";
        return result;
    }
    const uint32_t rowSize = width * 4u;
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
        
        // GetDIBits already returns top-down BGRA, which is the BMP layout.
        file.write(reinterpret_cast<const char*>(pixels), imageSize);
        file.flush();
        if (!file.good()) {
            result.error = "Failed to write screenshot pixels";
            return result;
        }
        file.close();
        
        result.path = filename;
        result.fileSize = fs::file_size(filename);
        result.success = true;
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    
    return result;
}

Result SavePNG(const std::string& filename, const uint8_t* pixels, uint32_t width, uint32_t height) {
    Result result;
    result.width = width;
    result.height = height;
    if (!pixels || !width || !height || width > INT_MAX || height > INT_MAX ||
        uint64_t{width} * height * 4u > UINT_MAX) {
        result.error = "Invalid screenshot dimensions";
        return result;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        result.error = "Failed to initialize screenshot encoder";
        return result;
    }
    const auto uninitialize = OmniGhost::Platform::MakeScopeExit([com] {
        if (SUCCEEDED(com)) CoUninitialize();
    });
    (void)uninitialize;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;
    auto releaseAll = [&] {
        if (properties) properties->Release();
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        if (factory) factory->Release();
    };
    auto encoderCleanup = OmniGhost::Platform::MakeScopeExit([&] { releaseAll(); });

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
    const fs::path path(filename);
    bool fileOpened = false;
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        fileOpened = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(hr)) hr = frame->Initialize(properties);
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr) && format != GUID_WICPixelFormat32bppBGRA)
        hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (SUCCEEDED(hr)) {
        const UINT stride = width * 4u;
        hr = frame->WritePixels(height, stride, stride * height,
                                const_cast<BYTE*>(pixels));
    }
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    releaseAll();
    encoderCleanup.Release();
    if (FAILED(hr)) {
        result.error = "PNG encoding failed: HRESULT=" + std::to_string(static_cast<unsigned long>(hr));
        std::error_code ignored;
        if (fileOpened) fs::remove(path, ignored);
        return result;
    }
    result.path = filename;
    std::error_code sizeError;
    result.fileSize = fs::file_size(path, sizeError);
    if (sizeError) {
        result.error = "Failed to inspect screenshot: " + sizeError.message();
        return result;
    }
    result.success = true;
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
    if (width <= 0 || height <= 0 || uint64_t{static_cast<unsigned>(width)} *
        static_cast<unsigned>(height) * 4u > UINT_MAX) {
        result.error = "Invalid capture dimensions";
        return result;
    }
    
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
    if (!hScreenDC || !hMemoryDC || !hBitmap) {
        if (hBitmap) DeleteObject(hBitmap);
        if (hMemoryDC) DeleteDC(hMemoryDC);
        if (hScreenDC) ReleaseDC(nullptr, hScreenDC);
        result.error = "Failed to allocate screenshot resources";
        return result;
    }
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    if (!hOldBitmap || hOldBitmap == HGDI_ERROR) {
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(nullptr, hScreenDC);
        result.error = "Failed to select screenshot bitmap";
        return result;
    }
    auto captureCleanup = OmniGhost::Platform::MakeScopeExit([&] {
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(nullptr, hScreenDC);
    });
    
    BOOL success = BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, captureX, captureY, SRCCOPY | CAPTUREBLT);
    
    if (!success) {
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

    std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
    // GetDIBits requires the bitmap to be deselected from its DC.
    SelectObject(hMemoryDC, hOldBitmap);
    const int copiedRows = GetDIBits(hScreenDC, hBitmap, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);
    
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(nullptr, hScreenDC);
    captureCleanup.Release();
    if (copiedRows != height) {
        result.error = "Failed to read screenshot pixels";
        return result;
    }
    for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    
    if (fmt == Format::BMP) {
        result = SaveBMP(filename, pixels.data(), width, height);
    } else {
        result = SavePNG(filename, pixels.data(), width, height);
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
