#pragma once
#include <filesystem>
#include <cstddef>
#include <string>
#include <string_view>

namespace OmniGhost::Update {
bool Sha256File(const std::filesystem::path& file, std::string& hexDigest, std::string& error);
bool ConstantTimeEquals(const std::string& left, const std::string& right);



bool VerifyAuthenticode(const std::filesystem::path& file,
                        std::string_view expectedPublisherSubject,
                        std::string_view expectedCertificateThumbprint,
                        std::string& error);



bool VerifyDetachedManifestSignature(std::string_view manifestBytes,
                                     std::string_view signatureBase64,
                                     std::string_view publicCertificateDerBase64,
                                     std::string& error);

bool RandomTokenHex(std::size_t byteCount, std::wstring& token, std::string& error);
}
