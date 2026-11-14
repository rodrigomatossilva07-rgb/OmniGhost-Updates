#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "build_profiles.hpp"
#include "memory/memory.hpp"
#include "patterns.hpp"
#include "process/process.hpp"
#include "utils/utils.hpp"

namespace {
std::string Hex(uintptr_t value);

struct CandidateResult {
    const PatternCandidate* candidate = nullptr;
    std::vector<uintptr_t> matches;
    std::vector<uintptr_t> raw_targets;
    std::vector<uintptr_t> resolved;
    std::string status;
};

enum class GameFamily { Legacy, Enhanced, Unknown };

struct SemanticResult {
    bool passed = false;
    bool runtime_available = false;
    std::string detail;
};

const char* FamilyName(GameFamily family) {
    switch (family) {
    case GameFamily::Legacy: return "legacy";
    case GameFamily::Enhanced: return "enhanced";
    default: return "unknown";
    }
}

GameFamily DetectFamily(const std::string& module_name, int build) {
    std::string lower = module_name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("enhanced") != std::string::npos) return GameFamily::Enhanced;

    if (build >= 1604) return GameFamily::Legacy;
    return GameFamily::Unknown;
}

template <typename T>
bool ReadValue(HANDLE process, uintptr_t address, T& value) {
    SIZE_T read = 0;
    return address && ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address),
        &value, sizeof(value), &read) && read == sizeof(value);
}

bool FiniteVector3(HANDLE process, uintptr_t address, float maximum = 100000.0f) {
    std::array<float, 3> value{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), value.data(),
            sizeof(value), &read) || read != sizeof(value)) return false;
    return std::all_of(value.begin(), value.end(), [maximum](float item) {
        return std::isfinite(item) && std::abs(item) <= maximum;
    });
}

uintptr_t GlobalObject(HANDLE process, uintptr_t global_address) {
    const uintptr_t indirect = memory::dereference_pointer(process, global_address);
    if (indirect && memory::is_readable(process, indirect, sizeof(uintptr_t))) return indirect;
    return memory::is_readable(process, global_address, sizeof(uintptr_t)) ? global_address : 0;
}

std::string Hex(uintptr_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << value;
    return stream.str();
}

int ParseBuild(const std::string& process_name) {
    const auto marker = process_name.find("_b");
    if (marker == std::string::npos) return 0;
    try { return std::stoi(process_name.substr(marker + 2)); }
    catch (...) { return 0; }
}

bool SupportsBuild(const PatternCandidate& candidate, int build) {
    if (!build) return true;
    if (candidate.minimum_build && build < candidate.minimum_build) return false;
    if (candidate.maximum_build && build > candidate.maximum_build) return false;
    return true;
}

CandidateResult Evaluate(HANDLE process_handle, const process::module_info& module,
                         const std::vector<std::uint8_t>& module_image,
                         const PatternCandidate& candidate, int build) {
    CandidateResult result{&candidate};
    result.matches = memory::pattern_scan_all(module_image, module.base, candidate.pattern, 16);
    if (result.matches.empty()) {
        result.status = "pattern_not_found";
        return result;
    }

    std::set<uintptr_t> raw_unique;
    std::set<uintptr_t> valid_unique;
    const BuildOffset* expected = FindBuildOffset(build, candidate.symbol);
    for (const uintptr_t match : result.matches) {
        uintptr_t address = match;
        if (candidate.resolve == ResolveKind::RipRelative) {
            address = memory::get_rip_relative_address(process_handle, match,
                candidate.displacement_offset, candidate.instruction_length);
        }
        if (!address) continue;
        raw_unique.insert(address);

        if (expected && address >= module.base && address - module.base == expected->rva) {
            valid_unique.insert(address);
            continue;
        }
        bool valid = candidate.resolve == ResolveKind::MatchAddress
            ? address >= module.base && address < module.base + module.size
            : memory::is_readable(process_handle, address);
        uintptr_t current = address;
        for (int level = 0; valid && level < candidate.dereference_count; ++level) {
            current = memory::dereference_pointer(process_handle, current);
            valid = current && memory::is_readable(process_handle, current);
        }
        if (valid && candidate.addend) {
            current = static_cast<uintptr_t>(static_cast<std::intptr_t>(current) + candidate.addend);
            valid = memory::is_readable(process_handle, current, 1);
        }
        if (valid) valid_unique.insert(address);
    }
    result.raw_targets.assign(raw_unique.begin(), raw_unique.end());
    result.resolved.assign(valid_unique.begin(), valid_unique.end());
    const bool profile_confirmed = expected && result.raw_targets.size() == 1 &&
        result.raw_targets.front() >= module.base &&
        result.raw_targets.front() - module.base == expected->rva;
    if (profile_confirmed) result.status = "profile_confirmed";
    else if (expected && !result.raw_targets.empty()) result.status = "profile_mismatch";
    else if (result.resolved.empty() && result.raw_targets.size() == 1)
        result.status = "target_matched_unvalidated";
    else if (result.resolved.empty()) result.status = "validation_failed";
    else if (result.resolved.size() > 1 || result.matches.size() > 1) result.status = "ambiguous";
    else result.status = "candidate_matched";
    return result;
}

SemanticResult ValidateWorld(HANDLE process, uintptr_t global) {
    const uintptr_t world = memory::dereference_pointer(process, global);
    if (!world) return {false, false, "world pointer unavailable"};
    const uintptr_t local = memory::dereference_pointer(process, world + 0x8);
    if (!local) return {false, false, "join a server: local player unavailable"};
    float health = 0.0f;
    if (!ReadValue(process, local + 0x280, health) || !std::isfinite(health) ||
        health < 0.0f || health > 10000.0f || !FiniteVector3(process, local + 0x90))
        return {false, true, "local-player health/position validation failed"};
    return {true, true, "local player, health and position confirmed"};
}

SemanticResult ValidateReplay(HANDLE process, uintptr_t global) {
    const uintptr_t replay = GlobalObject(process, global);
    if (!replay) return {false, false, "replay interface unavailable"};
    const uintptr_t interface_object = memory::dereference_pointer(process, replay + 0x18);
    if (!interface_object) return {false, false, "join a server: replay ped interface unavailable"};
    const uintptr_t list = memory::dereference_pointer(process, interface_object + 0x100);
    int capacity = -1, count = -1;
    if (!ReadValue(process, interface_object + 0x108, capacity) ||
        !ReadValue(process, interface_object + 0x110, count) || capacity < 0 ||
        capacity > 4096 || count < 0 || count > capacity)
        return {false, true, "replay capacity/count outside valid range"};
    if (count > 0 && (!list || !memory::is_readable(process, list, sizeof(uintptr_t))))
        return {false, true, "replay list is unreadable"};
    return {true, true, "replay interface, list, capacity and count confirmed"};
}

SemanticResult ValidatePool(HANDLE process, uintptr_t global) {
    const uintptr_t pool = memory::dereference_pointer(process, global);
    if (!pool) return {false, false, "join a populated server: pool unavailable"};
    struct Layout { std::size_t list, bitmap, capacity, count; };
    constexpr Layout layouts[] = {{0x0, 0x8, 0x10, 0x14}, {0x8, 0x0, 0x10, 0x14}};
    for (const auto& layout : layouts) {
        uintptr_t list = 0, bitmap = 0;
        std::uint32_t capacity = 0, count = 0;
        if (!ReadValue(process, pool + layout.list, list) ||
            !ReadValue(process, pool + layout.bitmap, bitmap) ||
            !ReadValue(process, pool + layout.capacity, capacity) ||
            !ReadValue(process, pool + layout.count, count)) continue;
        if (capacity == 0 || capacity > 131072 || count > capacity) continue;
        if (!memory::is_readable(process, list, sizeof(uintptr_t)) ||
            !memory::is_readable(process, bitmap, std::max<std::size_t>(1, (capacity + 7) / 8))) continue;
        return {true, true, "pool list, bitmap, counter and capacity confirmed"};
    }
    std::ostringstream detail;
    detail << "pool layout validation failed; object=" << Hex(pool);
    for (const std::size_t offset : {std::size_t{0}, std::size_t{8}, std::size_t{0x10},
                                     std::size_t{0x14}, std::size_t{0x18}, std::size_t{0x20}}) {
        uintptr_t value = 0;
        if (ReadValue(process, pool + offset, value))
            detail << " +" << Hex(offset) << '=' << Hex(value);
    }
    return {false, true, detail.str()};
}

SemanticResult ValidateMatrix(HANDLE process, uintptr_t object, const std::size_t offset) {
    std::array<float, 16> matrix{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(object + offset), matrix.data(),
            sizeof(matrix), &read) || read != sizeof(matrix)) return {false, true, "matrix unreadable"};
    int finite = 0, non_zero = 0;
    for (const float value : matrix) {
        if (std::isfinite(value) && std::abs(value) < 1000000.0f) ++finite;
        if (std::isfinite(value) && std::abs(value) > 0.000001f) ++non_zero;
    }
    if (finite != 16 || non_zero < 4) return {false, true, "matrix is non-finite or degenerate"};
    return {true, true, "finite non-degenerate matrix confirmed at +" + Hex(offset)};
}

SemanticResult ValidateViewport(HANDLE process, uintptr_t global) {
    const uintptr_t viewport = GlobalObject(process, global);
    if (!viewport) return {false, false, "viewport unavailable"};
    for (const std::size_t offset : {std::size_t{0x24C}, std::size_t{0x250}}) {
        const auto result = ValidateMatrix(process, viewport, offset);
        if (result.passed) return result;
    }
    return {false, true, "viewport matrix validation failed"};
}

SemanticResult ValidateCamera(HANDLE process, uintptr_t global) {
    const uintptr_t camera = GlobalObject(process, global);
    if (!camera) return {false, false, "camera unavailable"};
    const bool position = FiniteVector3(process, camera + 0x60);
    const bool direction = FiniteVector3(process, camera + 0x3D0, 1000.0f);
    if (!position || !direction) return {false, true, "camera position/direction validation failed"};
    return {true, true, "camera position and direction confirmed"};
}

SemanticResult ValidateSymbol(HANDLE process, const std::string& symbol, uintptr_t global) {
    if (symbol == "world") return ValidateWorld(process, global);
    if (symbol == "replay_interface") return ValidateReplay(process, global);
    if (symbol == "ped_pool" || symbol == "vehicle_pool") return ValidatePool(process, global);
    if (symbol == "viewport") return ValidateViewport(process, global);
    if (symbol == "camera") return ValidateCamera(process, global);
    return {false, false, "no semantic validator registered"};
}

bool ValidatePlayerInfo(HANDLE process_handle, uintptr_t world_global, int build,
                        uintptr_t& validated_field, std::string& reason) {
    validated_field = 0;
    if (!world_global) {
        reason = "validated World global unavailable";
        return false;
    }
    const uintptr_t world = memory::dereference_pointer(process_handle, world_global);
    if (!world || !memory::is_readable(process_handle, world + 0x8, sizeof(uintptr_t))) {
        reason = "world/local-player chain unavailable";
        return false;
    }
    const uintptr_t local_player = memory::dereference_pointer(process_handle, world + 0x8);
    if (!local_player) {
        reason = "local player unavailable";
        return false;
    }
    std::set<uintptr_t> field_candidates;
    if (const auto* profile = FindBuildOffset(build, "player_info"))
        field_candidates.insert(profile->rva);
    for (const auto& entry : kBuildOffsets)
        if (entry.symbol == "player_info") field_candidates.insert(entry.rva);

    for (const uintptr_t field : field_candidates) {
        const uintptr_t player_info = memory::dereference_pointer(process_handle, local_player + field);
        if (!player_info || !memory::is_readable(process_handle, player_info, sizeof(uintptr_t))) {
            reason = "candidate " + Hex(field) + ": PlayerInfo pointer unreadable";
            continue;
        }
        int wanted_level = -1;
        std::uint32_t net_id = 0;
        if (!ReadValue(process_handle, player_info + 0x8E8, wanted_level) ||
            wanted_level < 0 || wanted_level > 6) {
            reason = "candidate " + Hex(field) + ": wanted-level field invalid";
            continue;
        }
        if (!ReadValue(process_handle, player_info + 0x88, net_id) || net_id > 0xFFFF) {
            reason = "candidate " + Hex(field) + ": Net ID field invalid";
            continue;
        }
        char name[65]{};
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(player_info + 0x100), name,
                sizeof(name) - 1, &bytes_read) || bytes_read == 0) {
            reason = "candidate " + Hex(field) + ": inline player name unreadable";
            continue;
        }
        const std::size_t length = strnlen_s(name, sizeof(name));
        if (length == 0 || length >= sizeof(name) ||
            !std::all_of(name, name + length, [](unsigned char c) {
                return c >= 0x20 && c != 0x7F;
            })) {
            reason = "candidate " + Hex(field) + ": player name is not plausible text";
            continue;
        }
        validated_field = field;
        reason = "local ped, name, Net ID and wanted level confirmed";
        return true;
    }
    if (reason.empty()) reason = "no known PlayerInfo field candidate passed semantic validation";
    return false;
}

void WriteOutputs(const std::filesystem::path& output_directory,
                  const process::module_info& module, int build, GameFamily family,
                  const std::map<std::string, std::vector<CandidateResult>>& results,
                  const std::map<std::string, SemanticResult>& semantics,
                  bool player_info_valid, uintptr_t player_info_field,
                  const std::string& player_info_reason) {
    std::filesystem::create_directories(output_directory);
    for (const auto& entry : std::filesystem::directory_iterator(output_directory))
        if (entry.is_regular_file()) std::filesystem::remove(entry.path());
    const auto output_path = output_directory /
        ("fivem_offsets_b" + std::to_string(build) + ".txt");
    std::ofstream output(output_path, std::ios::binary);
    output << "OmniGhost FiveM offsets\n"
           << "build=" << build << "\n"
           << "family=" << FamilyName(family) << "\n"
           << "module=" << module.name << "\n\n";
    for (const auto& [symbol, candidates] : results) {
        const CandidateResult* selected = nullptr;
        for (const auto& result : candidates) {
            if (result.status == "profile_confirmed") { selected = &result; break; }
        }
        if (!selected) {
            for (const auto& result : candidates) {
                if (result.status == "candidate_matched") { selected = &result; break; }
            }
        }
        if (!selected || selected->resolved.empty()) continue;
        const auto semantic = semantics.find(symbol);
        const bool has_validator = semantic != semantics.end();
        const bool profile_identity = selected->status == "profile_confirmed";


        if ((!profile_identity && (!has_validator || !semantic->second.passed)) ||
            (has_validator && !semantic->second.passed)) continue;
        output << symbol << '=' << Hex(selected->resolved.front() - module.base) << "\n";
    }
    if (player_info_valid && player_info_field)
        output << "player_info=" << Hex(player_info_field) << "\n";
    output << "\n# validation\n";
    for (const auto& [symbol, validation] : semantics)
        output << "# " << symbol << '=' << (validation.passed ? "confirmed" : "rejected")
               << " (" << validation.detail << ")\n";
    output << "# player_info=" << (player_info_valid ? "confirmed" : "rejected")
           << " (" << player_info_reason << ")\n";
}
}

int main(int argc, char** argv) {
    SetConsoleTitleA("OmniGhost FiveM Offset Dumper");
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode))
        SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    std::cout << "       \033[94mOmniGhost FiveM offset dumper\033[0m\n\n";
    if (argc > 1 && std::string(argv[1]) == "--inventory") {
        std::set<std::string> symbols;
        for (const auto& candidate : kLegacyPatternCandidates) symbols.insert(candidate.symbol);
        symbols.insert("player_info");
        std::cout << "Legacy pattern candidates: " << std::size(kLegacyPatternCandidates) << "\n"
                  << "Enhanced pattern candidates: " << kEnhancedPatternCandidates.size() << "\n"
                  << "Exported symbols: " << symbols.size() << "\n";
        for (const auto& symbol : symbols) std::cout << "  - " << symbol << "\n";
        return 0;
    }
    logger::log("A aguardar pelo FiveM...", logger::info);
    HWND window_handle = nullptr;
    while (!(window_handle = FindWindowA("grcWindow", nullptr)))
        std::this_thread::sleep_for(std::chrono::seconds(1));

    DWORD pid = 0;
    GetWindowThreadProcessId(window_handle, &pid);
    if (!pid) return 1;
    const auto module = process::get_main_module(pid);
    if (!module.base || !module.size) {
        logger::log("Nao foi possivel obter os limites do modulo principal.", logger::fail);
        return 1;
    }
    const int build = ParseBuild(module.name);
    const GameFamily family = DetectFamily(module.name, build);
    logger::log("FiveM encontrado: " + module.name + " (build " +
                (build ? std::to_string(build) : std::string("desconhecida")) +
                ", familia " + FamilyName(family) + ")", logger::success);
    if (family == GameFamily::Unknown) {
        logger::log("Familia do executavel desconhecida; nenhum perfil sera exportado.", logger::fail);
        return 2;
    }
    if (family == GameFamily::Enhanced && kEnhancedPatternCandidates.empty()) {
        logger::log("FiveM Enhanced detetado, mas ainda nao existem perfis Enhanced validados.", logger::fail);
        return 3;
    }

    HANDLE process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process_handle) return 1;
    logger::log("A criar uma captura unica do modulo...", logger::info);
    const auto module_image = memory::snapshot_module(process_handle, module.base, module.size);
    if (module_image.size() != module.size) {
        logger::log("Nao foi possivel capturar o modulo principal.", logger::fail);
        CloseHandle(process_handle);
        return 1;
    }

    std::map<std::string, std::vector<CandidateResult>> results;
    std::set<std::string> profile_confirmed_symbols;
    const auto scan_candidates = [&](const auto& inventory) {
        for (const auto& candidate : inventory) {
            if (!SupportsBuild(candidate, build)) continue;
            if (profile_confirmed_symbols.count(candidate.symbol)) continue;
            auto result = Evaluate(process_handle, module, module_image, candidate, build);
            std::string message = std::string(candidate.symbol) + " / " + candidate.id +
                ": " + result.status + " (" + std::to_string(result.matches.size()) + " match)";
            if (result.raw_targets.size() == 1 && result.raw_targets.front() >= module.base)
                message += " -> RVA " + Hex(result.raw_targets.front() - module.base);
            logger::log(message, (result.status == "candidate_matched" ||
                                 result.status == "profile_confirmed") ? logger::success :
                                 result.status == "ambiguous" ? logger::info : logger::fail);
            if (result.status == "profile_confirmed")
                profile_confirmed_symbols.insert(candidate.symbol);
            results[candidate.symbol].push_back(std::move(result));
        }
    };
    if (family == GameFamily::Legacy) scan_candidates(kLegacyPatternCandidates);
    else scan_candidates(kEnhancedPatternCandidates);

    std::map<std::string, SemanticResult> semantics;
    std::map<std::string, uintptr_t> selected_globals;
    const std::set<std::string> semantic_symbols = {
        "world", "replay_interface", "ped_pool", "vehicle_pool", "camera", "viewport"
    };
    for (const auto& symbol : semantic_symbols) {
        const auto found = results.find(symbol);
        if (found == results.end()) continue;
        const CandidateResult* selected = nullptr;
        for (const auto& result : found->second)
            if ((result.status == "profile_confirmed" || result.status == "candidate_matched") &&
                result.resolved.size() == 1) { selected = &result; break; }
        if (!selected) continue;
        selected_globals[symbol] = selected->resolved.front();
        auto validation = ValidateSymbol(process_handle, symbol, selected->resolved.front());
        logger::log(symbol + " semantic: " + validation.detail,
                    validation.passed ? logger::success : logger::info);
        semantics.emplace(symbol, std::move(validation));
    }

    std::string player_info_reason;
    uintptr_t player_info_field = 0;
    const auto world_global = selected_globals.find("world");
    const bool player_info_valid = ValidatePlayerInfo(
        process_handle,
        world_global != selected_globals.end() ? world_global->second : 0,
        build, player_info_field, player_info_reason);
    logger::log("player_info: " + player_info_reason,
                player_info_valid ? logger::success : logger::info);
    const auto output_directory = std::filesystem::current_path() / "output";
    std::filesystem::create_directories(output_directory);
    WriteOutputs(output_directory, module, build, family, results, semantics,
                 player_info_valid, player_info_field, player_info_reason);
    CloseHandle(process_handle);
    logger::log("Resultados gravados em " + output_directory.string(), logger::success);
    std::cout << "\nSo foram gravados offsets com identidade e semantica confirmadas.\n"
                 "Executa dentro de um servidor para validar Player Info, Replay e pools.\n"
                 "Prima Enter para fechar...";
    std::cin.get();
    return 0;
}
