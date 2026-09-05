#include "diagnostics_package.h"

#include "app_paths.h"
#include "session_log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace OmniGhost::Diagnostics {
namespace {
namespace fs = std::filesystem;

constexpr std::uint64_t kMaximumInputBytes = 2ull * 1024ull * 1024ull;
constexpr std::uint64_t kMaximumPackagePayloadBytes = 5ull * 1024ull * 1024ull;

struct Entry {
    std::string name;
    std::string data;
    std::uint32_t crc{};
    std::uint32_t localOffset{};
};

void Put16(std::ostream& output, std::uint16_t value) {
    const char bytes[2] = {static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu)};
    output.write(bytes, 2);
}

void Put32(std::ostream& output, std::uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xffu), static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu), static_cast<char>((value >> 24u) & 0xffu)};
    output.write(bytes, 4);
}

std::uint32_t Crc32(std::string_view data) {
    std::uint32_t crc = 0xffffffffu;
    for (const unsigned char byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

std::string TimestampForFile() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream value;
    value << std::put_time(&local, "%Y%m%d-%H%M%S");
    return value.str();
}

std::string ReadTail(const fs::path& path, std::uint64_t maximumBytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const std::streamoff end = input.tellg();
    if (end <= 0) return {};
    const std::uint64_t size = static_cast<std::uint64_t>(end);
    const std::uint64_t take = (std::min)(size, maximumBytes);
    input.seekg(static_cast<std::streamoff>(size - take), std::ios::beg);
    std::string data(static_cast<std::size_t>(take), '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>((std::max)(std::streamsize{0}, input.gcount())));
    if (size > take)
        data.insert(0, "[... início omitido por limite do pacote de diagnóstico ...]\n");
    return SessionLog::RedactForSupport(data);
}

bool WriteZip(const fs::path& path, std::vector<Entry>& entries, std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Não foi possível criar o ficheiro ZIP de diagnóstico.";
        return false;
    }

    for (Entry& entry : entries) {
        const std::streamoff offset = output.tellp();
        if (offset < 0 || static_cast<std::uint64_t>(offset) > 0xffffffffull ||
            entry.data.size() > 0xffffffffull || entry.name.size() > 0xffffu) {
            error = "O pacote de diagnóstico excedeu os limites ZIP suportados.";
            return false;
        }
        entry.localOffset = static_cast<std::uint32_t>(offset);
        entry.crc = Crc32(entry.data);
        Put32(output, 0x04034b50u);
        Put16(output, 20); Put16(output, 0x0800); Put16(output, 0); // UTF-8, stored
        Put16(output, 0); Put16(output, 0);
        Put32(output, entry.crc);
        Put32(output, static_cast<std::uint32_t>(entry.data.size()));
        Put32(output, static_cast<std::uint32_t>(entry.data.size()));
        Put16(output, static_cast<std::uint16_t>(entry.name.size())); Put16(output, 0);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        output.write(entry.data.data(), static_cast<std::streamsize>(entry.data.size()));
    }

    const std::streamoff centralOffsetRaw = output.tellp();
    if (centralOffsetRaw < 0 || static_cast<std::uint64_t>(centralOffsetRaw) > 0xffffffffull) {
        error = "Offset ZIP inválido.";
        return false;
    }
    const auto centralOffset = static_cast<std::uint32_t>(centralOffsetRaw);

    for (const Entry& entry : entries) {
        Put32(output, 0x02014b50u);
        Put16(output, 20); Put16(output, 20); Put16(output, 0x0800); Put16(output, 0);
        Put16(output, 0); Put16(output, 0);
        Put32(output, entry.crc);
        Put32(output, static_cast<std::uint32_t>(entry.data.size()));
        Put32(output, static_cast<std::uint32_t>(entry.data.size()));
        Put16(output, static_cast<std::uint16_t>(entry.name.size()));
        Put16(output, 0); Put16(output, 0); Put16(output, 0); Put16(output, 0); Put32(output, 0);
        Put32(output, entry.localOffset);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    }

    const std::streamoff centralEndRaw = output.tellp();
    if (centralEndRaw < centralOffsetRaw || static_cast<std::uint64_t>(centralEndRaw) > 0xffffffffull) {
        error = "Diretório central ZIP inválido.";
        return false;
    }
    const auto centralSize = static_cast<std::uint32_t>(centralEndRaw - centralOffsetRaw);
    if (entries.size() > 0xffffu) {
        error = "Demasiados ficheiros no pacote de diagnóstico.";
        return false;
    }
    Put32(output, 0x06054b50u);
    Put16(output, 0); Put16(output, 0);
    Put16(output, static_cast<std::uint16_t>(entries.size()));
    Put16(output, static_cast<std::uint16_t>(entries.size()));
    Put32(output, centralSize); Put32(output, centralOffset); Put16(output, 0);
    output.flush();
    if (!output) {
        error = "Falha ao finalizar o pacote ZIP de diagnóstico.";
        return false;
    }
    return true;
}

} // namespace

bool CreatePackage(std::string_view summary,
                   std::filesystem::path& outputPath,
                   std::string& error) {
    error.clear();
    outputPath.clear();
    SessionLog::Flush();
    if (!Paths::EnsureUserDirectories()) {
        error = "Não foi possível preparar a pasta local de suporte.";
        return false;
    }

    std::vector<Entry> entries;
    std::uint64_t payload = 0;
    auto add = [&](std::string name, std::string data) {
        data = SessionLog::RedactForSupport(data);
        if (data.empty() || payload + data.size() > kMaximumPackagePayloadBytes) return;
        payload += data.size();
        entries.push_back({std::move(name), std::move(data)});
    };

    add("diagnostics.txt", std::string(summary));
    add("README.txt",
        "OmniGhost · pacote de suporte\n\n"
        "Conteúdo limitado e automaticamente anonimizado.\n"
        "Não inclui palavras-passe, tokens, chaves de licença, auth.dat, license.dat, "
        "configurações de jogo nem dumps de memória.\n"
        "Indica ao suporte o código OG-* presente em diagnostics.txt.\n");
    add("logs.txt", ReadTail(SessionLog::CurrentLogPath(), kMaximumInputBytes));

    if (entries.empty()) {
        error = "Não existem dados de diagnóstico para exportar.";
        return false;
    }

    const fs::path support = Paths::LocalData() / L"support";
    std::error_code ec;
    fs::create_directories(support, ec);
    if (ec) {
        error = "Não foi possível criar a pasta de suporte: " + ec.message();
        return false;
    }
    outputPath = support / fs::path("OmniGhost-Diagnostics-" + TimestampForFile() + ".zip");
    if (!WriteZip(outputPath, entries, error)) {
        fs::remove(outputPath, ec);
        outputPath.clear();
        return false;
    }
    return true;
}

} // namespace OmniGhost::Diagnostics
