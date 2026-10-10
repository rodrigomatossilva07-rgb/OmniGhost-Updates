#pragma once
#include <filesystem>
#include <cstddef>
#include <string>
#include <string_view>

namespace OmniGhost::Update {
bool Sha256File(const std::filesystem::path& file, std::string& hexDigest, std::string& error);
bool ConstantTimeEquals(const std::string& left, const std::string& right);

// Authenticode requires a valid Windows signature and the exact configured
// publisher pin. A self-signed root is accepted only when that exact pin matches.
bool VerifyAuthenticode(const std::filesystem::path& file,
                        std::string_view expectedPublisherSubject,
                        std::string_view expectedCertificateThumbprint,
                        std::string& error);

// update.json is signed as raw UTF-8 bytes with RSA/SHA-256 PKCS#1 v1.5.
// The public certificate is compiled into the client; its private key never is.
bool VerifyDetachedManifestSignature(std::string_view manifestBytes,
                                     std::string_view signatureBase64,
                                     std::string_view publicCertificateDerBase64,
                                     std::string& error);

bool RandomTokenHex(std::size_t byteCount, std::wstring& token, std::string& error);
}
