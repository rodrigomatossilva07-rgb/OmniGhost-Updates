#pragma once

#include "app_paths.h"
#include "expected.h"
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <Windows.h>
#include <thread>

namespace OmniGhost::Platform {




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




class IFilesystem {
public:
    virtual ~IFilesystem() = default;
    [[nodiscard]] virtual Expected<bool> Exists(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Expected<bool> CreateDirectory(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Expected<bool> Remove(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Expected<bool> RemoveAll(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Expected<std::string> ReadTextFile(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual VoidExpected WriteTextFile(const std::filesystem::path& path, std::string_view content) const noexcept = 0;
    [[nodiscard]] virtual Expected<std::vector<uint8_t>> ReadBinaryFile(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual VoidExpected WriteBinaryFile(const std::filesystem::path& path, std::span<const uint8_t> data) const noexcept = 0;
    [[nodiscard]] virtual Expected<std::filesystem::file_time_type> LastWriteTime(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Expected<uintmax_t> FileSize(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Expected<std::vector<std::filesystem::path>> ListDirectory(const std::filesystem::path& path) const noexcept = 0;
    [[nodiscard]] virtual Expected<std::filesystem::path> GetTempPath() const noexcept = 0;
    [[nodiscard]] virtual Expected<std::filesystem::path> GetAppDataPath() const noexcept = 0;
    [[nodiscard]] virtual Expected<std::filesystem::path> GetExePath() const noexcept = 0;
};

class StdFilesystem final : public IFilesystem {
public:
    [[nodiscard]] Expected<bool> Exists(const std::filesystem::path& path) const noexcept override {
        try { return Success(std::filesystem::exists(path)); }
        catch (const std::exception& e) { return Failure<bool>(e.what()); }
    }
    [[nodiscard]] Expected<bool> CreateDirectory(const std::filesystem::path& path) const noexcept override {
        try { return Success(std::filesystem::create_directories(path)); }
        catch (const std::exception& e) { return Failure<bool>(e.what()); }
    }
    [[nodiscard]] Expected<bool> Remove(const std::filesystem::path& path) const noexcept override {
        try { return Success(std::filesystem::remove(path)); }
        catch (const std::exception& e) { return Failure<bool>(e.what()); }
    }
    [[nodiscard]] Expected<bool> RemoveAll(const std::filesystem::path& path) const noexcept override {
        try { return Success(std::filesystem::remove_all(path) > 0); }
        catch (const std::exception& e) { return Failure<bool>(e.what()); }
    }
    [[nodiscard]] Expected<std::string> ReadTextFile(const std::filesystem::path& path) const noexcept override {
        try {
            std::ifstream file(path);
            if (!file) return Failure<std::string>("Failed to open file: " + path.string());
            return Success(std::string(std::istreambuf_iterator<char>(file), {}));
        } catch (const std::exception& e) { return Failure<std::string>(e.what()); }
    }
    [[nodiscard]] VoidExpected WriteTextFile(const std::filesystem::path& path, std::string_view content) const noexcept override {
        try {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) return Failure<void>("Failed to open file for writing: " + path.string());
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!file) return Failure<void>("Failed to write file: " + path.string());
            return Success();
        } catch (const std::exception& e) { return Failure<void>(e.what()); }
    }
    [[nodiscard]] Expected<std::vector<uint8_t>> ReadBinaryFile(const std::filesystem::path& path) const noexcept override {
        try {
            std::ifstream file(path, std::ios::binary);
            if (!file) return Failure<std::vector<uint8_t>>("Failed to open file: " + path.string());
            file.seekg(0, std::ios::end);
            size_t size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> data(size);
            file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
            if (!file) return Failure<std::vector<uint8_t>>("Failed to read file: " + path.string());
            return Success(std::move(data));
        } catch (const std::exception& e) { return Failure<std::vector<uint8_t>>(e.what()); }
    }
    [[nodiscard]] VoidExpected WriteBinaryFile(const std::filesystem::path& path, std::span<const uint8_t> data) const noexcept override {
        try {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) return Failure<void>("Failed to open file for writing: " + path.string());
            file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!file) return Failure<void>("Failed to write file: " + path.string());
            return Success();
        } catch (const std::exception& e) { return Failure<void>(e.what()); }
    }
    [[nodiscard]] Expected<std::filesystem::file_time_type> LastWriteTime(const std::filesystem::path& path) const noexcept override {
        try { return Success(std::filesystem::last_write_time(path)); }
        catch (const std::exception& e) { return Failure<std::filesystem::file_time_type>(e.what()); }
    }
    [[nodiscard]] Expected<uintmax_t> FileSize(const std::filesystem::path& path) const noexcept override {
        try { return Success(std::filesystem::file_size(path)); }
        catch (const std::exception& e) { return Failure<uintmax_t>(e.what()); }
    }
    [[nodiscard]] Expected<std::vector<std::filesystem::path>> ListDirectory(const std::filesystem::path& path) const noexcept override {
        try {
            std::vector<std::filesystem::path> entries;
            for (const auto& entry : std::filesystem::directory_iterator(path))
                entries.push_back(entry.path());
            return Success(std::move(entries));
        } catch (const std::exception& e) { return Failure<std::vector<std::filesystem::path>>(e.what()); }
    }
    [[nodiscard]] Expected<std::filesystem::path> GetTempPath() const noexcept override {
        return Success(std::filesystem::temp_directory_path());
    }
    [[nodiscard]] Expected<std::filesystem::path> GetAppDataPath() const noexcept override {
        try {
            wchar_t* appdata = nullptr;
            if (_wdupenv_s(&appdata, nullptr, L"LOCALAPPDATA") == 0 && appdata) {
                std::filesystem::path path(appdata);
                free(appdata);
                return Success(path / "OmniGhost");
            }
            return Failure<std::filesystem::path>("LOCALAPPDATA not available");
        } catch (const std::exception& e) { return Failure<std::filesystem::path>(e.what()); }
    }
    [[nodiscard]] Expected<std::filesystem::path> GetExePath() const noexcept override {
        try {


            return Success(OmniGhost::Paths::Executable());
        } catch (const std::exception& e) { return Failure<std::filesystem::path>(e.what()); }
    }
};




class INetwork {
public:
    virtual ~INetwork() = default;
    [[nodiscard]] virtual Expected<std::string> Get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers = {}) const noexcept = 0;
    [[nodiscard]] virtual Expected<std::string> Post(const std::string& url, std::string_view body, const std::vector<std::pair<std::string, std::string>>& headers = {}) const noexcept = 0;
    [[nodiscard]] virtual Expected<std::vector<uint8_t>> Download(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers = {}) const noexcept = 0;
    [[nodiscard]] virtual bool IsOnline() const noexcept = 0;
};




class IProcess {
public:
    virtual ~IProcess() = default;
    [[nodiscard]] virtual Expected<std::vector<std::string>> ListProcesses() const noexcept = 0;
    [[nodiscard]] virtual Expected<uint32_t> FindProcess(const std::string& name) const noexcept = 0;
    [[nodiscard]] virtual Expected<bool> IsRunning(uint32_t pid) const noexcept = 0;
    [[nodiscard]] virtual Expected<std::string> GetProcessName(uint32_t pid) const noexcept = 0;
    [[nodiscard]] virtual Expected<std::filesystem::path> GetProcessExePath(uint32_t pid) const noexcept = 0;
};




class IHardware {
public:
    virtual ~IHardware() = default;
    [[nodiscard]] virtual Expected<bool> ProbeDmaDevice() const noexcept = 0;
    [[nodiscard]] virtual Expected<bool> ProbeMakcu() const noexcept = 0;
    [[nodiscard]] virtual Expected<std::string> GetDmaDeviceInfo() const noexcept = 0;
    [[nodiscard]] virtual Expected<std::string> GetMakcuInfo() const noexcept = 0;
};

}