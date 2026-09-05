#pragma once

#include "result.h"
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <Windows.h>
#include <thread>

namespace OmniGhost::Platform {

// ============================================================
// IClock - abstraction for time
// ============================================================
class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual uint64_t NowMs() const noexcept = 0;
    [[nodiscard]] virtual uint64_t NowUs() const noexcept = 0;
    virtual void SleepMs(uint32_t ms) const noexcept = 0;
    virtual void SleepUs(uint32_t us) const noexcept = 0;
};

class SteadyClock final : public IClock {
public:
    [[nodiscard]] uint64_t NowMs() const noexcept override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    [[nodiscard]] uint64_t NowUs() const noexcept override {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void SleepMs(uint32_t ms) const noexcept override {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    void SleepUs(uint32_t us) const noexcept override {
        std::this_thread::sleep_for(std::chrono::microseconds(us));
    }
};

// ============================================================
// IFilesystem - abstraction for filesystem operations
// ============================================================
class IFilesystem {
public:
    virtual ~IFilesystem() = default;
    [[nodiscard]] virtual Result<bool> Exists(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Result<bool> CreateDirectory(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Result<bool> Remove(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Result<bool> RemoveAll(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Result<std::string> ReadTextFile(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Result<void> WriteTextFile(const std::filesystem::path& path, std::string_view content) const noexcept = 0;
    [[nodiscard]] virtual Result<std::vector<uint8_t>> ReadBinaryFile(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Result<void> WriteBinaryFile(const std::filesystem::path& path, std::span<const uint8_t> data) const noexcept = 0;
    [[nodiscard]] virtual Result<std::filesystem::file_time_type> LastWriteTime(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Result<uintmax_t> FileSize(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Result<std::vector<std::filesystem::path>> ListDirectory(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Result<std::filesystem::path> GetTempPath() const noexcept = 0;
    [[nodiscard]] virtual Result<std::filesystem::path> GetAppDataPath() const noexcept = 0;
    [[nodiscard]] virtual Result<std::filesystem::path> GetExePath() const noexcept = 0;
};

class StdFilesystem final : public IFilesystem {
public:
    [[nodiscard]] Result<bool> Exists(const std::filesystem::path& path) const noexcept override {
        try { return Ok(std::filesystem::exists(path)); }
        catch (const std::exception& e) { return Err<bool>(e.what()); }
    }
    [[nodiscard]] Result<bool> CreateDirectory(const std::filesystem::path& path) const noexcept override {
        try { return Ok(std::filesystem::create_directories(path)); }
        catch (const std::exception& e) { return Err<bool>(e.what()); }
    }
    [[nodiscard]] Result<bool> Remove(const std::filesystem::path& path) const noexcept override {
        try { return Ok(std::filesystem::remove(path)); }
        catch (const std::exception& e) { return Err<bool>(e.what()); }
    }
    [[nodiscard]] Result<bool> RemoveAll(const std::filesystem::path& path) const noexcept override {
        try { return Ok(std::filesystem::remove_all(path) > 0); }
        catch (const std::exception& e) { return Err<bool>(e.what()); }
    }
    [[nodiscard]] Result<std::string> ReadTextFile(const std::filesystem::path& path) const noexcept override {
        try {
            std::ifstream file(path);
            if (!file) return Err<std::string>("Failed to open file: " + path.string());
            return Ok(std::string(std::istreambuf_iterator<char>(file), {}));
        } catch (const std::exception& e) { return Err<std::string>(e.what()); }
    }
    [[nodiscard]] Result<void> WriteTextFile(const std::filesystem::path& path, std::string_view content) const noexcept override {
        try {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) return Err<void>("Failed to open file for writing: " + path.string());
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!file) return Err<void>("Failed to write file: " + path.string());
            return Ok();
        } catch (const std::exception& e) { return Err<void>(e.what()); }
    }
    [[nodiscard]] Result<std::vector<uint8_t>> ReadBinaryFile(const std::filesystem::path& path) const noexcept override {
        try {
            std::ifstream file(path, std::ios::binary);
            if (!file) return Err<std::vector<uint8_t>>("Failed to open file: " + path.string());
            file.seekg(0, std::ios::end);
            size_t size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> data(size);
            file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
            if (!file) return Err<std::vector<uint8_t>>("Failed to read file: " + path.string());
            return Ok(std::move(data));
        } catch (const std::exception& e) { return Err<std::vector<uint8_t>>(e.what()); }
    }
    [[nodiscard]] Result<void> WriteBinaryFile(const std::filesystem::path& path, std::span<const uint8_t> data) const noexcept override {
        try {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) return Err<void>("Failed to open file for writing: " + path.string());
            file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!file) return Err<void>("Failed to write file: " + path.string());
            return Ok();
        } catch (const std::exception& e) { return Err<void>(e.what()); }
    }
    [[nodiscard]] Result<std::filesystem::file_time_type> LastWriteTime(const std::filesystem::path& path) const noexcept override {
        try { return Ok(std::filesystem::last_write_time(path)); }
        catch (const std::exception& e) { return Err<std::filesystem::file_time_type>(e.what()); }
    }
    [[nodiscard]] Result<uintmax_t> FileSize(const std::filesystem::path& path) const noexcept override {
        try { return Ok(std::filesystem::file_size(path)); }
        catch (const std::exception& e) { return Err<uintmax_t>(e.what()); }
    }
    [[nodiscard]] Result<std::vector<std::filesystem::path>> ListDirectory(const std::filesystem::path& path) const noexcept override {
        try {
            std::vector<std::filesystem::path> entries;
            for (const auto& entry : std::filesystem::directory_iterator(path))
                entries.push_back(entry.path());
            return Ok(std::move(entries));
        } catch (const std::exception& e) { return Err<std::vector<std::filesystem::path>>(e.what()); }
    }
    [[nodiscard]] Result<std::filesystem::path> GetTempPath() const noexcept override {
        return Ok(std::filesystem::temp_directory_path());
    }
    [[nodiscard]] Result<std::filesystem::path> GetAppDataPath() const noexcept override {
        try {
            wchar_t* appdata = nullptr;
            if (_wdupenv_s(&appdata, nullptr, L"LOCALAPPDATA") == 0 && appdata) {
                std::filesystem::path path(appdata);
                free(appdata);
                return Ok(path / "OmniGhost");
            }
            return Err<std::filesystem::path>("LOCALAPPDATA not available");
        } catch (const std::exception& e) { return Err<std::filesystem::path>(e.what()); }
    }
    [[nodiscard]] Result<std::filesystem::path> GetExePath() const noexcept override {
        try {
            wchar_t path[MAX_PATH];
            DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
            if (len == 0 || len == MAX_PATH) return Err<std::filesystem::path>("GetModuleFileNameW failed");
            return Ok(std::filesystem::path(path));
        } catch (const std::exception& e) { return Err<std::filesystem::path>(e.what()); }
    }
};

// ============================================================
// INetwork - abstraction for network operations
// ============================================================
class INetwork {
public:
    virtual ~INetwork() = default;
    [[nodiscard]] virtual Result<std::string> Get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers = {}) const noexcept = 0;
    [[nodiscard]] virtual Result<std::string> Post(const std::string& url, std::string_view body, const std::vector<std::pair<std::string, std::string>>& headers = {}) const noexcept = 0;
    [[nodiscard]] virtual Result<std::vector<uint8_t>> Download(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers = {}) const noexcept = 0;
    [[nodiscard]] virtual bool IsOnline() const noexcept = 0;
};

// ============================================================
// IProcess - abstraction for process operations
// ============================================================
class IProcess {
public:
    virtual ~IProcess() = default;
    [[nodiscard]] virtual Result<std::vector<std::string>> ListProcesses() const noexcept = 0;
    [[nodiscard]] virtual Result<uint32_t> FindProcess(const std::string& name) const noexcept = 0;
    [[nodiscard]] virtual Result<bool> IsRunning(uint32_t pid) const noexcept = 0;
    [[nodiscard]] virtual Result<std::string> GetProcessName(uint32_t pid) const noexcept = 0;
    [[nodiscard]] virtual Result<std::filesystem::path> GetProcessExePath(uint32_t pid) const noexcept = 0;
};

// ============================================================
// IHardware - abstraction for hardware probing
// ============================================================
class IHardware {
public:
    virtual ~IHardware() = default;
    [[nodiscard]] virtual Result<bool> ProbeDmaDevice() const noexcept = 0;
    [[nodiscard]] virtual Result<bool> ProbeMakcu() const noexcept = 0;
    [[nodiscard]] virtual Result<std::string> GetDmaDeviceInfo() const noexcept = 0;
    [[nodiscard]] virtual Result<std::string> GetMakcuInfo() const noexcept = 0;
};

} // namespace OmniGhost::Platform