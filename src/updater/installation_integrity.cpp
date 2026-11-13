#include "installation_integrity.h"
#include "../platform/runtime_bootstrap.h"
#include "../platform/text_encoding.h"
#include "crypto.h"
#include "../platform/text_encoding.h"
#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_set>
#include "platform/strprotect/strprotect.h"

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
    case IntegrityStatus::Unknown: return OGS_LIT("unknown");
    case IntegrityStatus::Checking: return OGS_LIT("checking");
    case IntegrityStatus::Healthy: return OGS_LIT("healthy");
    case IntegrityStatus::Damaged: return OGS_LIT("damaged");
    case IntegrityStatus::ManifestMissing: return OGS_LIT("manifest-missing");
    case IntegrityStatus::Error: return OGS_LIT("error");
    }
    return OGS_LIT("unknown");
}

IntegrityReport VerifyInstallationFiles(const fs::path& installDirectory, std::size_t maximumIssues) {
    IntegrityReport report{};
    const fs::path manifest = installDirectory / OGS_WIDE("install-manifest.sha256");
    if (!fs::exists(manifest)) {




        if (fs::is_regular_file(installDirectory / L"OmniGhost.exe")) {
            std::wstring embeddedError;
            if (OmniGhost::RuntimeBootstrap::ValidateEmbeddedRuntimeBundle(embeddedError)) {
                report.status = IntegrityStatus::Healthy;
                report.checkedFiles = 1;
                report.message = OGS_LIT("Instalação single-EXE verificada através do manifesto incorporado.");
            } else {
                report.status = IntegrityStatus::Damaged;
                report.failedFiles = 1;
                report.message = embeddedError.empty()
                    ? OGS_LIT("O manifesto de integridade incorporado é inválido.")
                    : OmniGhost::Platform::WideToUtf8(embeddedError);
            }
        } else {
            report.status = IntegrityStatus::ManifestMissing;
            report.message = OGS_LIT("O executável principal não existe.");
        }
        return report;
    }

    std::ifstream manifestStream(manifest, std::ios::binary);
    if (!manifestStream) {
        report.status = IntegrityStatus::Error;
        report.message = OGS_LIT("Não foi possível abrir install-manifest.sha256.");
        return report;
    }
    const std::string manifestBytes((std::istreambuf_iterator<char>(manifestStream)),
                                    std::istreambuf_iterator<char>());
    if (manifestBytes.empty()) {
        report.status = IntegrityStatus::Error;
        report.message = OGS_LIT("install-manifest.sha256 está vazio.");
        return report;
    }




    std::istringstream input(manifestBytes);
    std::string line;
    std::size_t lineNumber = 0;
    std::unordered_set<std::string> seenPaths;
    bool executableListed = false;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::size_t first = line.find('\t');
        const std::size_t second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first != 64 || second == std::string::npos || second + 1 >= line.size()) {
            report.status = IntegrityStatus::Error;
            report.message = OGS_LIT("install-manifest.sha256 contém uma linha inválida: ") + std::to_string(lineNumber);
            return report;
        }
        const std::string expectedHash = line.substr(0, first);
        std::uint64_t expectedSize = 0;
        const std::string_view sizeText(line.data() + first + 1, second - first - 1);
        const auto parsed = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), expectedSize);
        if (parsed.ec != std::errc{} || parsed.ptr != sizeText.data() + sizeText.size()) {
            report.status = IntegrityStatus::Error;
            report.message = OGS_LIT("install-manifest.sha256 contém um tamanho inválido.");
            return report;
        }
        const std::string relativeUtf8 = line.substr(second + 1);
        const fs::path relative(OmniGhost::Platform::Utf8ToWide(relativeUtf8));
        if (!IsSafeRelative(relative)) {
            report.status = IntegrityStatus::Error;
            report.message = OGS_LIT("install-manifest.sha256 contém um caminho inseguro.");
            return report;
        }
        if (!seenPaths.insert(relativeUtf8).second) {
            report.status = IntegrityStatus::Error;
            report.message = OGS_LIT("install-manifest.sha256 contém um caminho repetido.");
            return report;
        }
        if (relativeUtf8 == "OmniGhost.exe") executableListed = true;

        ++report.checkedFiles;
        const fs::path file = installDirectory / relative;
        std::error_code ec;
        if (fs::is_symlink(file, ec) || ec) {
            ++report.failedFiles;
            if (report.issues.size() < maximumIssues) report.issues.push_back({relativeUtf8, OGS_LIT("reparse")});
            continue;
        }
        if (!fs::exists(file, ec) || !fs::is_regular_file(file, ec)) {
            ++report.failedFiles;
            if (report.issues.size() < maximumIssues) report.issues.push_back({relativeUtf8, OGS_LIT("missing")});
            continue;
        }
        const std::uint64_t actualSize = fs::file_size(file, ec);
        if (ec || actualSize != expectedSize) {
            ++report.failedFiles;
            if (report.issues.size() < maximumIssues) report.issues.push_back({relativeUtf8, OGS_LIT("size")});
            continue;
        }
        std::string actualHash, hashError;
        if (!Sha256File(file, actualHash, hashError) || !ConstantTimeEquals(actualHash, expectedHash)) {
            ++report.failedFiles;
            if (report.issues.size() < maximumIssues) report.issues.push_back({relativeUtf8, hashError.empty() ? OGS_LIT("sha256") : hashError});
        }
    }

    if (!input.eof() && input.fail()) {
        report.status = IntegrityStatus::Error;
        report.message = OGS_LIT("Falha ao ler install-manifest.sha256.");
        return report;
    }
    if (report.checkedFiles == 0) {
        report.status = IntegrityStatus::Error;
        report.message = OGS_LIT("install-manifest.sha256 está vazio.");
        return report;
    }
    if (!executableListed) {
        report.status = IntegrityStatus::Error;
        report.message = OGS_LIT("O manifesto de integridade não inclui OmniGhost.exe.");
        return report;
    }

    if (report.failedFiles == 0) {
        report.status = IntegrityStatus::Healthy;
        report.message = OGS_LIT("Instalação verificada: ") + std::to_string(report.checkedFiles) + OGS_LIT(" ficheiros íntegros.");
    } else {
        report.status = IntegrityStatus::Damaged;
        report.message = OGS_LIT("Foram detetados ") + std::to_string(report.failedFiles) + OGS_LIT(" ficheiros em falta ou alterados.");
    }
    return report;
}
}
