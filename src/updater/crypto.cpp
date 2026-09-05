#include "crypto.h"
#include "update_log.h"
#include "../platform/file_integrity.h"
#include "../platform/scope_exit.h"
#include <Windows.h>
#include <bcrypt.h>
#include <Softpub.h>
#include <Wintrust.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace OmniGhost::Update {
namespace {
std::string NormalizeThumbprint(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        if (std::isspace(c)) continue;
        out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

bool EqualsAsciiInsensitive(std::string_view left, std::string_view right) {
    auto trim = [](std::string_view value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        return value;
    };
    left = trim(left);
    right = trim(right);
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) return false;
    }
    return true;
}

std::string HexUpper(const unsigned char* bytes, std::size_t count) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(kHex[(bytes[i] >> 4) & 0x0f]);
        out.push_back(kHex[bytes[i] & 0x0f]);
    }
    return out;
}

bool DecodeBase64(std::string_view value, std::vector<unsigned char>& output, std::string& error) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char c : value) {
        if (!std::isspace(c)) normalized.push_back(static_cast<char>(c));
    }
    if (normalized.empty()) {
        error = "Assinatura/certificado público em falta.";
        return false;
    }
    DWORD bytes = 0;
    if (!CryptStringToBinaryA(normalized.c_str(), static_cast<DWORD>(normalized.size()),
                              CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr) || bytes == 0) {
        error = "Base64 de assinatura/certificado inválido.";
        return false;
    }
    output.resize(bytes);
    if (!CryptStringToBinaryA(normalized.c_str(), static_cast<DWORD>(normalized.size()),
                              CRYPT_STRING_BASE64, output.data(), &bytes, nullptr, nullptr)) {
        error = "Não foi possível descodificar Base64.";
        output.clear();
        return false;
    }
    output.resize(bytes);
    return true;
}

bool Sha256Bytes(std::string_view bytes, std::array<unsigned char, 32>& digest, std::string& error) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0, resultLength = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) { error = "BCrypt SHA-256 indisponível."; return false; }
    auto closeAlgorithm = OmniGhost::Platform::MakeScopeExit([&] {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    });
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0);
    if (status < 0 || objectLength == 0) { error = "Não foi possível preparar SHA-256."; return false; }
    std::vector<unsigned char> object(objectLength);
    status = BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0);
    if (status < 0) { error = "Não foi possível criar SHA-256."; return false; }
    auto destroyHash = OmniGhost::Platform::MakeScopeExit([&] {
        if (hash) BCryptDestroyHash(hash);
    });
    status = BCryptHashData(hash,
                            reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                            static_cast<ULONG>(bytes.size()), 0);
    if (status < 0) { error = "Não foi possível calcular SHA-256."; return false; }
    status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (status < 0) { error = "Não foi possível finalizar SHA-256."; return false; }
    return true;
}
}

bool Sha256File(const std::filesystem::path& file, std::string& hexDigest, std::string& error) {
    return OmniGhost::Platform::Sha256File(file, hexDigest, error);
}

bool ConstantTimeEquals(const std::string& left, const std::string& right) {
    return OmniGhost::Platform::ConstantTimeEquals(left, right);
}

bool RandomTokenHex(std::size_t byteCount, std::wstring& token, std::string& error) {
    if (byteCount == 0 || byteCount > 1024) {
        error = "Tamanho de token aleatório inválido.";
        return false;
    }

    std::vector<unsigned char> bytes(byteCount);
    const NTSTATUS status = BCryptGenRandom(
        nullptr,
        bytes.data(),
        static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        error = "Não foi possível obter aleatoriedade criptográfica do Windows.";
        return false;
    }

    static constexpr wchar_t hex[] = L"0123456789abcdef";
    token.clear();
    token.reserve(bytes.size() * 2);
    for (const unsigned char value : bytes) {
        token.push_back(hex[(value >> 4) & 0x0f]);
        token.push_back(hex[value & 0x0f]);
    }
    return true;
}

bool VerifyAuthenticode(const std::filesystem::path& file,
                        std::string_view expectedPublisherSubject,
                        std::string_view expectedCertificateThumbprint,
                        std::string& error) {
    if (expectedPublisherSubject.empty() || expectedCertificateThumbprint.empty()) {
        error = "A identidade do publisher Authenticode não está configurada no cliente.";
        return false;
    }

    WINTRUST_FILE_INFO info{};
    info.cbStruct = sizeof(info);
    info.pcwszFilePath = file.c_str();

    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &info;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &policy, &data);
    if (result != ERROR_SUCCESS) {
        data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, &policy, &data);
        error = "A assinatura digital do executável não é válida ou não pertence a uma cadeia confiável.";
        return false;
    }

    PCCERT_CONTEXT signerCertificate = nullptr;
    if (CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData)) {
        if (CRYPT_PROVIDER_SGNR* signer = WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0)) {
            if (signer->csCertChain > 0 && signer->pasCertChain && signer->pasCertChain[0].pCert)
                signerCertificate = CertDuplicateCertificateContext(signer->pasCertChain[0].pCert);
        }
    }

    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &data);

    if (!signerCertificate) {
        error = "A assinatura é válida, mas não foi possível identificar o certificado do publisher.";
        return false;
    }
    auto freeCertificate = OmniGhost::Platform::MakeScopeExit([&] {
        if (signerCertificate) CertFreeCertificateContext(signerCertificate);
    });

    DWORD subjectChars = CertGetNameStringA(signerCertificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
    std::string simpleSubject(subjectChars > 0 ? subjectChars : 1, '\0');
    if (subjectChars > 0)
        CertGetNameStringA(signerCertificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, simpleSubject.data(), subjectChars);
    if (!simpleSubject.empty() && simpleSubject.back() == '\0') simpleSubject.pop_back();

    char fullSubject[1024]{};
    CertNameToStrA(X509_ASN_ENCODING, &signerCertificate->pCertInfo->Subject,
                   CERT_X500_NAME_STR, fullSubject, static_cast<DWORD>(sizeof(fullSubject)));

    std::array<unsigned char, 64> hash{};
    DWORD hashSize = static_cast<DWORD>(hash.size());
    if (!CertGetCertificateContextProperty(signerCertificate, CERT_SHA1_HASH_PROP_ID, hash.data(), &hashSize)) {
        error = "Não foi possível calcular o thumbprint do publisher.";
        return false;
    }
    const std::string actualThumbprint = HexUpper(hash.data(), hashSize);
    const std::string expectedThumbprint = NormalizeThumbprint(expectedCertificateThumbprint);
    if (actualThumbprint != expectedThumbprint) {
        error = "A assinatura é válida, mas o certificado não corresponde ao publisher confiável configurado.";
        return false;
    }

    const std::string expectedSubject(expectedPublisherSubject);
    const std::string actualFullSubject(fullSubject);
    if (!EqualsAsciiInsensitive(actualFullSubject, expectedSubject) &&
        !EqualsAsciiInsensitive(simpleSubject, expectedSubject)) {
        error = "O subject do certificado Authenticode não corresponde ao publisher esperado.";
        return false;
    }
    return true;
}

bool VerifyDetachedManifestSignature(std::string_view manifestBytes,
                                     std::string_view signatureBase64,
                                     std::string_view publicCertificateDerBase64,
                                     std::string& error) {
    std::vector<unsigned char> certificateBytes;
    std::vector<unsigned char> signatureBytes;
    if (!DecodeBase64(publicCertificateDerBase64, certificateBytes, error) ||
        !DecodeBase64(signatureBase64, signatureBytes, error)) {
        return false;
    }

    PCCERT_CONTEXT certificate = CertCreateCertificateContext(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        certificateBytes.data(),
        static_cast<DWORD>(certificateBytes.size()));
    if (!certificate) {
        error = "O certificado público incorporado no cliente é inválido.";
        return false;
    }
    auto freeCertificate = OmniGhost::Platform::MakeScopeExit([&] {
        if (certificate) CertFreeCertificateContext(certificate);
    });

    if (!certificate->pCertInfo || !certificate->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId ||
        std::string_view(certificate->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId) != szOID_RSA_RSA) {
        error = "A assinatura do manifesto requer um certificado RSA.";
        return false;
    }

    BCRYPT_KEY_HANDLE publicKey = nullptr;
    if (!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING,
                                     &certificate->pCertInfo->SubjectPublicKeyInfo,
                                     0, nullptr, &publicKey) || !publicKey) {
        error = "Não foi possível importar a chave pública do certificado de Release.";
        return false;
    }
    auto destroyKey = OmniGhost::Platform::MakeScopeExit([&] {
        if (publicKey) BCryptDestroyKey(publicKey);
    });

    std::array<unsigned char, 32> digest{};
    if (!Sha256Bytes(manifestBytes, digest, error)) return false;

    BCRYPT_PKCS1_PADDING_INFO padding{};
    padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;
    const NTSTATUS status = BCryptVerifySignature(
        publicKey,
        &padding,
        digest.data(),
        static_cast<ULONG>(digest.size()),
        signatureBytes.data(),
        static_cast<ULONG>(signatureBytes.size()),
        BCRYPT_PAD_PKCS1);
    if (status < 0) {
        error = "A assinatura criptográfica de update.json é inválida.";
        return false;
    }
    return true;
}
}
