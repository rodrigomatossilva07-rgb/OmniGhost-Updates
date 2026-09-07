#include "game_adapter.h"
#include <algorithm>
#include <thread>
#include <chrono>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>

namespace Gameplay::GameAdapter {

    // AdapterFactory implementation
    AdapterFactory& AdapterFactory::Instance() {
        static AdapterFactory instance;
        return instance;
    }

    void AdapterFactory::RegisterAdapter(GameType type, CreatorFunc creator, const GameInfo& info) {
        AdapterInfo adapter_info;
        adapter_info.creator = std::move(creator);
        adapter_info.info = info;
        adapters_[type] = std::move(adapter_info);
        process_to_type_[info.process_name] = type;
    }

    std::unique_ptr<IGameAdapter> AdapterFactory::CreateAdapter(GameType type) {
        auto it = adapters_.find(type);
        if (it != adapters_.end()) {
            return it->second.creator();
        }
        return nullptr;
    }

    std::unique_ptr<IGameAdapter> AdapterFactory::CreateAdapter(const std::string& process_name) {
        auto it = process_to_type_.find(process_name);
        if (it != process_to_type_.end()) {
            return CreateAdapter(it->second);
        }
        return nullptr;
    }

    const GameInfo* AdapterFactory::GetGameInfo(GameType type) const {
        auto it = adapters_.find(type);
        return it != adapters_.end() ? &it->second.info : nullptr;
    }

    const GameInfo* AdapterFactory::GetGameInfo(const std::string& process_name) const {
        auto it = process_to_type_.find(process_name);
        if (it != process_to_type_.end()) {
            auto adapter_it = adapters_.find(it->second);
            return adapter_it != adapters_.end() ? &adapter_it->second.info : nullptr;
        }
        return nullptr;
    }

    std::vector<GameType> AdapterFactory::GetRegisteredTypes() const {
        std::vector<GameType> types;
        for (const auto& [type, info] : adapters_) {
            types.push_back(type);
        }
        return types;
    }

    bool AdapterFactory::IsGameSupported(GameType type) const {
        return adapters_.find(type) != adapters_.end();
    }

    bool AdapterFactory::IsGameSupported(const std::string& process_name) const {
        return process_to_type_.find(process_name) != process_to_type_.end();
    }

    // AdapterManager implementation
    AdapterManager& AdapterManager::Instance() {
        static AdapterManager instance;
        return instance;
    }

    bool AdapterManager::Initialize() {
        return true;
    }

    void AdapterManager::Shutdown() {
        DetachFromGame();
    }

    GameType AdapterManager::DetectGame() {
        // Enumerate processes and check for known games
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return GameType::Unknown;
        
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        
        if (Process32FirstW(snapshot, &entry)) {
            do {
                std::wstring wname(entry.szExeFile);
                std::string name(wname.begin(), wname.end());
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                
                // Check against known games
                if (name == "cs2.exe") {
                    CloseHandle(snapshot);
                    return GameType::CS2;
                } else if (name == "fivem.exe" || name == "fivem_gta.exe") {
                    CloseHandle(snapshot);
                    return GameType::FiveM;
                } else if (name == "rustclient.exe" || name == "rust.exe") {
                    CloseHandle(snapshot);
                    return GameType::Rust;
                } else if (name == "warzone.exe" || name == "modernwarfare.exe") {
                    CloseHandle(snapshot);
                    return GameType::Warzone;
                } else if (name == "valorant.exe" || name == "valorant-win64-shipping.exe") {
                    CloseHandle(snapshot);
                    return GameType::Valorant;
                } else if (name == "fortniteclient-win64-shipping.exe") {
                    CloseHandle(snapshot);
                    return GameType::Fortnite;
                } else if (name == "r5apex.exe") {
                    CloseHandle(snapshot);
                    return GameType::Apex;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        
        CloseHandle(snapshot);
        return GameType::Unknown;
    }

    bool AdapterManager::AutoAttach() {
        GameType detected = DetectGame();
        if (detected == GameType::Unknown) return false;
        return AttachToGame(detected);
    }

    void AdapterManager::AutoDetach() {
        DetachFromGame();
    }

    bool AdapterManager::AttachToGame(GameType type) {
        if (current_adapter_) {
            DetachFromGame();
        }
        
        auto adapter = AdapterFactory::Instance().CreateAdapter(type);
        if (!adapter) return false;
        
        if (!adapter->AttachToGame()) {
            return false;
        }
        
        current_adapter_ = std::move(adapter);
        current_game_ = type;
        pending_game_ = GameType::Unknown;
        
        if (on_game_attached) {
            on_game_attached(current_game_);
        }
        
        return true;
    }

    void AdapterManager::DetachFromGame() {
        if (current_adapter_) {
            GameType old_game = current_game_;
            current_adapter_->DetachFromGame();
            current_adapter_.reset();
            current_game_ = GameType::Unknown;
            
            if (on_game_detached) {
                on_game_detached(old_game);
            }
        }
    }

    bool AdapterManager::SwitchGame(GameType type) {
        if (current_game_ == type) return true;
        
        pending_game_ = type;
        DetachFromGame();
        return AttachToGame(type);
    }

    std::vector<GameType> AdapterManager::GetAvailableGames() {
        std::vector<GameType> available;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return {};
        
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        
        if (Process32FirstW(snapshot, &entry)) {
            do {
                std::wstring wname(entry.szExeFile);
                std::string name(wname.begin(), wname.end());
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                
                if (name == "cs2.exe") available.push_back(GameType::CS2);
                else if (name == "fivem.exe" || name == "fivem_gta.exe") available.push_back(GameType::FiveM);
                else if (name == "rustclient.exe" || name == "rust.exe") available.push_back(GameType::Rust);
                else if (name == "warzone.exe" || name == "modernwarfare.exe") available.push_back(GameType::Warzone);
                else if (name == "valorant.exe" || name == "valorant-win64-shipping.exe") available.push_back(GameType::Valorant);
                else if (name == "fortniteclient-win64-shipping.exe") available.push_back(GameType::Fortnite);
                else if (name == "r5apex.exe") available.push_back(GameType::Apex);
            } while (Process32NextW(snapshot, &entry));
        }
        
        CloseHandle(snapshot);
        
        // Remove duplicates
        std::sort(available.begin(), available.end());
        available.erase(std::unique(available.begin(), available.end()), available.end());
        
        return available;
    }

    // GameLauncher implementation
    GameLauncher& GameLauncher::Instance() {
        static GameLauncher instance;
        return instance;
    }

    bool GameLauncher::LaunchGame(const LaunchConfig& config) {
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_UNICODE;
        sei.lpVerb = L"open";
        
        std::wstring wexe(config.executable_path.begin(), config.executable_path.end());
        std::wstring wdir(config.working_directory.begin(), config.working_directory.end());
        std::wstring wargs(config.arguments.begin(), config.arguments.end());
        
        sei.lpFile = wexe.c_str();
        sei.lpDirectory = wdir.empty() ? nullptr : wdir.c_str();
        sei.lpParameters = wargs.empty() ? nullptr : wargs.c_str();
        sei.nShow = SW_SHOW;
        
        if (!ShellExecuteExW(&sei)) {
            return false;
        }
        
        if (config.wait_for_attach) {
            // Wait for process to be ready
            auto start = std::chrono::steady_clock::now();
            while (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count() < config.attach_timeout_ms) {
                
                if (IsGameRunning(config.game)) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            return false;
        }
        
        return true;
    }

    bool GameLauncher::IsGameRunning(GameType game) const {
        const char* process_names[] = {
            [GameType::CS2] = "cs2.exe",
            [GameType::FiveM] = "fivem.exe",
            [GameType::Rust] = "rustclient.exe",
            [GameType::Warzone] = "warzone.exe",
            [GameType::Valorant] = "valorant.exe",
            [GameType::Fortnite] = "fortniteclient-win64-shipping.exe",
            [GameType::Apex] = "r5apex.exe"
        };
        
        const char* target = nullptr;
        switch (game) {
            case GameType::CS2: target = "cs2.exe"; break;
            case GameType::FiveM: target = "fivem.exe"; break;
            case GameType::Rust: target = "rustclient.exe"; break;
            case GameType::Warzone: target = "warzone.exe"; break;
            case GameType::Valorant: target = "valorant.exe"; break;
            case GameType::Fortnite: target = "fortniteclient-win64-shipping.exe"; break;
            case GameType::Apex: target = "r5apex.exe"; break;
            default: return false;
        }
        
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        
        if (Process32FirstW(snapshot, &entry)) {
            do {
                std::wstring wname(entry.szExeFile);
                std::string name(wname.begin(), wname.end());
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == target) {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        
        CloseHandle(snapshot);
        return found;
    }

    bool GameLauncher::TerminateGame(GameType game) {
        const char* target = nullptr;
        switch (game) {
            case GameType::CS2: target = "cs2.exe"; break;
            case GameType::FiveM: target = "fivem.exe"; break;
            case GameType::Rust: target = "rustclient.exe"; break;
            case GameType::Warzone: target = "warzone.exe"; break;
            case GameType::Valorant: target = "valorant.exe"; break;
            case GameType::Fortnite: target = "fortniteclient-win64-shipping.exe"; break;
            case GameType::Apex: target = "r5apex.exe"; break;
            default: return false;
        }
        
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool success = false;
        
        if (Process32FirstW(snapshot, &entry)) {
            do {
                std::wstring wname(entry.szExeFile);
                std::string name(wname.begin(), wname.end());
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == target) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                    if (hProcess) {
                        TerminateProcess(hProcess, 0);
                        CloseHandle(hProcess);
                        success = true;
                    }
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        
        CloseHandle(snapshot);
        return success;
    }

    bool GameLauncher::WaitForGame(GameType game, int timeout_ms) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count() < timeout_ms) {
            
            if (IsGameRunning(game)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        return false;
    }

    bool GameLauncher::LaunchViaSteam(const std::string& app_id, const std::string& args) {
        std::string steam_url = "steam://run/" + app_id;
        if (!args.empty()) {
            steam_url += "//" + args;
        }
        
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"open";
        
        std::wstring wurl(steam_url.begin(), steam_url.end());
        sei.lpFile = wurl.c_str();
        sei.nShow = SW_SHOW;
        
        return ShellExecuteExW(&sei);
    }

    bool GameLauncher::IsSteamRunning() const {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        
        if (Process32FirstW(snapshot, &entry)) {
            do {
                std::wstring wname(entry.szExeFile);
                std::string name(wname.begin(), wname.end());
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "steam.exe") {
                    found = true;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        
        CloseHandle(snapshot);
        return found;
    }

    void GameLauncher::AddProcessCallback(GameType game, std::function<void(bool)> callback) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        GameProcess gp;
        gp.game = game;
        gp.callback = callback;
        monitored_processes_.push_back(gp);
    }

} // namespace Gameplay::GameAdapter