



#ifndef QR_TO_PNG_H
#define QR_TO_PNG_H





#if defined(__GNUC__) && __GNUC__ < 9
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include "QrCode.hpp"
#include "qrcode.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

class QrToPng {
public:












    QrToPng(std::string fileName, int imgSize, int minModulePixelSize, std::string text,
            bool overwriteExistingFile, qrcodegen::QrCode::Ecc ecc);



    bool writeToPNG();

private:
    std::string _fileName;
    int _size;
    int _minModulePixelSize;
    std::string _text;
    bool _overwriteExistingFile;
    qrcodegen::QrCode::Ecc _ecc;






    [[nodiscard]] bool _writeToPNG(const qrcodegen::QrCode &qrData) const;




    [[nodiscard]] uint32_t _imgSize(const qrcodegen::QrCode &qrData) const;

    [[nodiscard]] uint32_t _imgSizeWithBorder(const qrcodegen::QrCode &qrData) const;
};

#endif