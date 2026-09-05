#include "installation_integrity.h"
#include "crypto.h"
#include "release_trust.h"
#include "../platform/text_encoding.h"
#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>

namespace OmniGhost::Update {
namespace fs = std::filesystem;
namespace {
bool IsSafeRelative(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) {
        if (part == L".." || part == L".") return false;
    }
    return true;
}
}

const char* IntegrityStatusName(IntegrityStatus status) {
    switch (status) {
    case IntegrityStatus::Unknown: return "unknown";
    case IntegrityStatus::Checking: return "checking";
    case IntegrityStatus::Healthy: return "healthy";
    case IntegrityStatus::Damaged: return "damaged";
    case IntegrityStatus::ManifestMissing: return "manifest-missing";
    case IntegrityStatus::Error: return "error";
    }
    return "unknown";
}

IntegrityReport VerifyInstallationFiles(const fs::path& installDirectory, std::size_t maximumIssues) {
    IntegrityReport report{};
    const fs::path manifest = installDirectory / L"install-manifest.sha256";
    if (!fs::exists(manifest)) {
        report.status = IntegrityStatus::ManifestMissing;
        report.message = "O manifesto de instalação não existe nesta compilação. Cria ou instala um pacote recente da aplicação.";
        return report;
    }

    std::ifstream manifestStream(manifest, std::ios::binary);
    if (!manifestStream) {
        report.status = IntegrityStatus::Error;
        report.message = "Não foi possível abrir install-manifest.sha256.";
        return report;
    }
    const std::string manifestBytes((std::istreambuf_iterator<char>(manifestStream)),
                                    std::istreambuf_iterator<char>());
    if (manifestBytes.empty()) {
        report.status = IntegrityStatus::Error;
        report.message = "install-manifest.sha256 está vazio.";
        return report;
    }

    // In commercial builds the local integrity manifest is itself authenticated.
    // Otherwise an attacker able to edit both a runtime file and its local hash
    // could make a plain SHA-256-only verification report a false success.
    if (ReleaseTrust::Configured) {
        const fs::path signaturePath = installDirectory / L"install-manifest.sha256.sig";
        std::ifstream signatureStream(signaturePath, std::ios::binary);
        if (!signatureStream) {
            report.status = IntegrityStatus::Error;
            report.message = "A assinatura do manifesto de instalação está em falta.";
            return report;
        }
        const std::string signature((std::istreambuf_iterator<char>(signatureStream)),
                                    std::istreambuf_iterator<char>());
        std::string signatureError;
        bool signatureValid = VerifyDetachedManifestSignature(
            manifestBytes, signature, ReleaseTrust::PublicCertificateDerBase64, signatureError);
        if (!signatureValid && ReleaseTrust::SecondaryConfigured) {
            std::string secondaryError;
            signatureValid = VerifyDetachedManifestSignature(
                manifestBytes, signature, ReleaseTrust::SecondaryPublicCertificateDerBase64,
                secondaryError);
            if (!signatureValid) signatureError += "; a âncora de confiança secundária rejeitou a assinatura: " + secondaryError;
        }
        if (!signatureValid) {
            report.status = IntegrityStatus::Error;
            report.message = "A assinatura do manifesto de instalação é inválida: " + signatureError;
            return report;
        }
    }

    std::istringstream input(manifestBytes);
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::size_t first = line.find('\t');
        const std::size_t second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first != 64 || second == std::string::npos || second + 1 >= line.size()) {
            report.status = IntegrityStatus::Error;
            report.message = "install-manifest.sha256 contém uma linha inválida: " + std::to_string(lineNumber);
            return report;
        }
        const std::string expectedHash = line.substr(0, first);
        std::uint64_t expectedSize = 0;
        const std::string_view sizeText(line.data() + first + 1, second - first - 1);
        const auto parsed = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), expectedSize);
        if (parsed.ec != std::errc{} || parsed.ptr != sizeText.data() + sizeText.size()) {
            report.status = IntegrityStatus::Error;
            report.message = "install-manifest.sha256 contém um tamanho inválido.";
            return report;
        }
        const std::string relativeUtf8 = line.substr(second + 1);
        const fs::path relative(OmniGhost::Platform::Utf8ToWide(relativeUtf8));
        if (!IsSafeRelative(relative)) {
            report.status = IntegrityStatus::Error;
            report.message = "install-manifest.sha256 contém um caminho inseguro.";
            return report;
        }

        ++report.checkedFiles;
        const fs::path file = installDirectory / relative;
        std::error_code ec;
        if (!fs::exists(file, ec) || !fs::is_regular_file(file, ec)) {
            ++report.failedFiles;
            if (report.issues.size() < maximumIssues) report.issues.push_back({relativeUtf8, "missing"});
            continue;
        }
        const std::uint64_t actualSize = fs::file_size(file, ec);
        if (ec || actualSize != expectedSize) {
            ++report.failedFiles;
            if (report.issues.size() < maximumIssues) report.issues.push_back({relativeUtf8, "size"});
            continue;
        }
        std::string actualHash, hashError;
        if (!Sha256File(file, actualHash, hashError) || !ConstantTimeEquals(actualHash, expectedHash)) {
            ++report.failedFiles;
            if (report.issues.size() < maximumIssues) report.issues.push_back({relativeUtf8, hashError.empty() ? "sha256" : hashError});
        }
    }

    if (!input.eof() && input.fail()) {
        report.status = IntegrityStatus::Error;
        report.message = "Falha ao ler install-manifest.sha256.";
        return report;
    }
    if (report.checkedFiles == 0) {
        report.status = IntegrityStatus::Error;
        report.message = "install-manifest.sha256 está vazio.";
        return report;
    }

    if (report.failedFiles == 0) {
        report.status = IntegrityStatus::Healthy;
        report.message = "Instalação verificada: " + std::to_string(report.checkedFiles) + " ficheiros íntegros.";
    } else {
        report.status = IntegrityStatus::Damaged;
        report.message = "Foram detetados " + std::to_string(report.failedFiles) + " ficheiros em falta ou alterados.";
    }
    return report;
}
}
