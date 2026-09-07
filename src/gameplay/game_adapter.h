#pragma once
#include <optional>
#include <thread>
#include <mutex>
#include <map>
#include <cstdint>
#include <chrono>
#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Gameplay::GameAdapter {

    // Game identification
    enum class GameType : int {
        Unknown = 0,
        FiveM = 1,
        CS2 = 2,
        Rust = 3,
        Warzone = 4,
        Valorant = 5,
        Fortnite = 6,
        Apex = 7,
        Custom = 99
    };

    // Game capabilities
    struct GameCapabilities {
        bool has_aim = true;
        bool has_esp = true;
        bool has_triggerbot = true;
        bool has_radar = true;
        bool has_recoil_control = true;
        bool has_sound_esp = false;
        bool has_movement_assist = true;
        bool has_vehicle_esp = false;
        bool has_inventory_esp = false;
        bool has_spectator_list = false;
        bool supports_hot_reload_offsets = true;
        bool supports_multi_monitor = true;
        bool supports_vr = false;
    };

    // Game info
    struct GameInfo {
        GameType type = GameType::Unknown;
        std::string name;
        std::string process_name;       // e.g., "cs2.exe"
        std::string window_class;       // Window class name
        std::string window_title;       // Window title pattern
        std::vector<std::string> modules; // Required modules
        std::string version;            // Game version
        std::string build_id;           // Build identifier
        GameCapabilities capabilities;
        std::string install_path;       // Installation directory
        std::string executable_path;    // Full path to executable
    };

    // Adapter interface
    class IGameAdapter {
    public:
        virtual ~IGameAdapter() = default;
        
        // Identification
        virtual GameInfo GetGameInfo() const = 0;
        virtual bool IsGameRunning() const = 0;
        virtual bool AttachToGame() = 0;
        virtual void DetachFromGame() = 0;
        
        // Offsets
        virtual bool InitializeOffsets() = 0;
        virtual bool UpdateOffsets() = 0;
        virtual uintptr_t GetOffset(const std::string& name) = 0;
        
        // Entity management
        virtual std::vector<uint64_t> GetEntityList() = 0;
        virtual bool ReadEntity(uint64_t entity, void* buffer, size_t size) = 0;
        virtual bool WriteEntity(uint64_t entity, const void* buffer, size_t size) = 0;
        
        // Player
        virtual uint64_t GetLocalPlayer() = 0;
        virtual Vec3 GetLocalPosition() = 0;
        virtual Vec3 GetLocalVelocity() = 0;
        virtual Vec3 GetLocalAngles() = 0;
        virtual int GetLocalHealth() = 0;
        virtual int GetLocalTeam() = 0;
        virtual bool IsLocalAlive() = 0;
        virtual bool IsLocalScoping() = 0;
        
        // Entity
        virtual Vec3 GetEntityPosition(uint64_t entity) = 0;
        virtual Vec3 GetEntityVelocity(uint64_t entity) = 0;
        virtual int GetEntityHealth(uint64_t entity) = 0;
        virtual int GetEntityMaxHealth(uint64_t entity) = 0;
        virtual int GetEntityTeam(uint64_t entity) = 0;
        virtual bool IsEntityAlive(uint64_t entity) = 0;
        virtual bool IsEntityVisible(uint64_t entity) = 0;
        virtual bool IsEntityDormant(uint64_t entity) = 0;
        virtual std::string GetEntityName(uint64_t entity) = 0;
        virtual std::string GetEntityModel(uint64_t entity) = 0;
        virtual int GetEntityWeapon(uint64_t entity) = 0;
        
        // Aim
        virtual bool WorldToScreen(const Vec3& world, Vec2& screen) = 0;
        virtual float GetFOVToEntity(uint64_t entity) = 0;
        virtual float GetDistanceToEntity(uint64_t entity) = 0;
        virtual void SetViewAngles(const Vec3& angles) = 0;
        virtual Vec3 GetViewAngles() = 0;
        
        // Input
        virtual void MoveMouse(float x, float y) = 0;
        virtual void Click(int button) = 0;
        virtual void KeyPress(int key, bool down) = 0;
        virtual bool IsKeyDown(int key) = 0;
        
        // Memory
        virtual bool ReadMemory(uintptr_t address, void* buffer, size_t size) = 0;
        virtual bool WriteMemory(uintptr_t address, const void* buffer, size_t size) = 0;
        virtual uintptr_t FindPattern(const std::string& module, const std::string& pattern, const std::string& mask) = 0;
        
        // Game-specific
        virtual std::vector<std::string> GetEntityClasses() = 0;
        virtual bool IsInMenu() = 0;
        virtual bool IsInGame() = 0;
        virtual float GetGameTime() = 0;
    };
    
    // Adapter factory
    class AdapterFactory {
    public:
        using CreatorFunc = std::function<std::unique_ptr<IGameAdapter>()>;
        
        static AdapterFactory& Instance();
        
        void RegisterAdapter(GameType type, CreatorFunc creator, const GameInfo& info);
        std::unique_ptr<IGameAdapter> CreateAdapter(GameType type);
        std::unique_ptr<IGameAdapter> CreateAdapter(const std::string& process_name);
        
        const GameInfo* GetGameInfo(GameType type) const;
        const GameInfo* GetGameInfo(const std::string& process_name) const;
        
        std::vector<GameType> GetRegisteredTypes() const;
        bool IsGameSupported(GameType type) const;
        bool IsGameSupported(const std::string& process_name) const;
        
    private:
        struct AdapterInfo {
            std::function<std::unique_ptr<IGameAdapter>()> creator;
            GameInfo info;
        };
        
        std::unordered_map<GameType, AdapterInfo> adapters_;
        std::unordered_map<std::string, GameType> process_to_type_;
    };
    
    // Adapter manager
    class AdapterManager {
    public:
        static AdapterManager& Instance();
        
        bool Initialize();
        void Shutdown();
        
        // Current adapter
        IGameAdapter* GetCurrentAdapter() const { return current_adapter_.get(); }
        GameType GetCurrentGame() const { return current_game_; }
        
        // Auto-detection
        GameType DetectGame();
        bool AutoAttach();
        void AutoDetach();
        
        // Manual control
        bool AttachToGame(GameType type);
        void DetachFromGame();
        
        // Game switching
        bool SwitchGame(GameType type);
        std::vector<GameType> GetAvailableGames();
        
        // Events
        std::function<void(GameType)> on_game_attached;
        std::function<void(GameType)> on_game_detached;
        std::function<void(GameType)> on_game_switched;
        
        // Game-specific data access
        template<typename T>
        T* GetGameData() {
            return static_cast<T*>(game_data_.get());
        }
        
        template<typename T>
        void SetGameData(std::unique_ptr<T> data) {
            game_data_ = std::move(data);
        }
        
    private:
        std::unique_ptr<IGameAdapter> current_adapter_;
        GameType current_game_ = GameType::Unknown;
        std::unique_ptr<void, void(*)(void*)> game_data_;
        GameType pending_game_ = GameType::Unknown;
    };
    
    // Game launcher integration
    class GameLauncher {
    public:
        struct LaunchConfig {
            GameType game = GameType::Unknown;
            std::string executable_path;
            std::string working_directory;
            std::string arguments;
            std::string steam_app_id;
            bool wait_for_attach = true;
            int attach_timeout_ms = 30000;
            bool inject_dll = false;
            std::string dll_path;
            std::map<std::string, std::string> env_vars;
        };
        
        static GameLauncher& Instance();
        
        bool LaunchGame(const LaunchConfig& config);
        bool IsGameRunning(GameType game) const;
        bool TerminateGame(GameType game);
        bool WaitForGame(GameType game, int timeout_ms = 60000);
        
        // Steam integration
        bool LaunchViaSteam(const std::string& app_id, const std::string& args = "");
        bool IsSteamRunning() const;
        
        // Process monitoring
        void AddProcessCallback(GameType game, std::function<void(bool)> callback);
        
    private:
        struct GameProcess {
            GameType game = GameType::Unknown;
            uint32_t process_id = 0;
            std::chrono::steady_clock::time_point start_time;
            std::function<void(bool)> callback;
        };
        
        std::vector<GameProcess> monitored_processes_;
        std::mutex processes_mutex_;
        std::thread monitor_thread_;
        std::atomic<bool> monitoring_ = false;
    };
    
    // Vec3 for adapter
    struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
        Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
        Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
        float Length() const { return sqrtf(x*x + y*y + z*z); }
        float Length2D() const { return sqrtf(x*x + y*y); }
    };
    
    struct Vec2 {
        float x = 0, y = 0;
        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}
    };

} // namespace Gameplay::GameAdapter