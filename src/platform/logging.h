#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/fmt/ostr.h>

#include <memory>
#include <string>
#include <string_view>
#include <mutex>
#include <chrono>
#include <filesystem>

#include "app_paths.h"

namespace OmniGhost::Logging {





struct Config {

    spdlog::level::level_enum consoleLevel = spdlog::level::info;
    spdlog::level::level_enum fileLevel = spdlog::level::debug;


    bool asyncEnabled = true;
    size_t asyncQueueSize = 8192;
    size_t asyncThreadCount = 1;


    bool fileEnabled = true;
    std::filesystem::path logDirectory;
    std::string logFilePrefix = "omnighost";
    size_t maxFileSize = 10 * 1024 * 1024;
    size_t maxFiles = 5;
    bool rotateDaily = false;


    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v";
    std::string filePattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v";


    spdlog::level::level_enum flushLevel = spdlog::level::warn;
    std::chrono::seconds flushInterval = std::chrono::seconds(5);


    bool debugOutputEnabled = false;
};





class LoggerManager {
public:
    static LoggerManager& Instance() noexcept {
        static LoggerManager instance;
        return instance;
    }


    bool Initialize(const Config& config = {});


    void Shutdown();


    [[nodiscard]] std::shared_ptr<spdlog::logger> GetLogger(std::string_view name);


    [[nodiscard]] std::shared_ptr<spdlog::logger> CoreLogger() noexcept {
        return GetLogger("core");
    }


    [[nodiscard]] std::shared_ptr<spdlog::logger> UILogger() noexcept {
        return GetLogger("ui");
    }


    [[nodiscard]] std::shared_ptr<spdlog::logger> DMALogger() noexcept {
        return GetLogger("dma");
    }


    [[nodiscard]] std::shared_ptr<spdlog::logger> SecurityLogger() noexcept {
        return GetLogger("security");
    }


    [[nodiscard]] std::shared_ptr<spdlog::logger> GameLogger(std::string_view gameName) noexcept {
        return GetLogger(std::string("game.") + std::string(gameName));
    }


    void SetLevel(spdlog::level::level_enum level);


    void SetLevel(std::string_view name, spdlog::level::level_enum level);


    void Flush();


    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    LoggerManager();
    ~LoggerManager() { Shutdown(); }
    LoggerManager(const LoggerManager&) = delete;
    LoggerManager& operator=(const LoggerManager&) = delete;

    bool InitializeLocked(const Config& config);
    void FlushLocked();

    bool initialized_ = false;
    Config config_;
    std::mutex mutex_;
    std::shared_ptr<spdlog::logger> coreLogger_;
    std::shared_ptr<spdlog::logger> uiLogger_;
    std::shared_ptr<spdlog::logger> dmaLogger_;
    std::shared_ptr<spdlog::logger> securityLogger_;
};






#define OGS_LOG_TRACE(...)   OGS_LOG_IMPL(trace, "core", __VA_ARGS__)
#define OGS_LOG_DEBUG(...)   OGS_LOG_IMPL(debug, "core", __VA_ARGS__)
#define OGS_LOG_INFO(...)    OGS_LOG_IMPL(info, "core", __VA_ARGS__)
#define OGS_LOG_WARN(...)    OGS_LOG_IMPL(warn, "core", __VA_ARGS__)
#define OGS_LOG_ERROR(...)   OGS_LOG_IMPL(error, "core", __VA_ARGS__)
#define OGS_LOG_CRITICAL(...) OGS_LOG_IMPL(critical, "core", __VA_ARGS__)


#define OGS_UI_LOG_TRACE(...)  OGS_LOG_IMPL(trace, "ui", __VA_ARGS__)
#define OGS_UI_LOG_DEBUG(...)  OGS_LOG_IMPL(debug, "ui", __VA_ARGS__)
#define OGS_UI_LOG_INFO(...)   OGS_LOG_IMPL(info, "ui", __VA_ARGS__)
#define OGS_UI_LOG_WARN(...)   OGS_LOG_IMPL(warn, "ui", __VA_ARGS__)
#define OGS_UI_LOG_ERROR(...)  OGS_LOG_IMPL(error, "ui", __VA_ARGS__)
#define OGS_UI_LOG_CRITICAL(...) OGS_LOG_IMPL(critical, "ui", __VA_ARGS__)


#define OGS_DMA_LOG_TRACE(...)  OGS_LOG_IMPL(trace, "dma", __VA_ARGS__)
#define OGS_DMA_LOG_DEBUG(...)  OGS_LOG_IMPL(debug, "dma", __VA_ARGS__)
#define OGS_DMA_LOG_INFO(...)   OGS_LOG_IMPL(info, "dma", __VA_ARGS__)
#define OGS_DMA_LOG_WARN(...)   OGS_LOG_IMPL(warn, "dma", __VA_ARGS__)
#define OGS_DMA_LOG_ERROR(...)  OGS_LOG_IMPL(error, "dma", __VA_ARGS__)
#define OGS_DMA_LOG_CRITICAL(...) OGS_LOG_IMPL(critical, "dma", __VA_ARGS__)


#define OGS_SEC_LOG_TRACE(...)  OGS_LOG_IMPL(trace, "security", __VA_ARGS__)
#define OGS_SEC_LOG_DEBUG(...)  OGS_LOG_IMPL(debug, "security", __VA_ARGS__)
#define OGS_SEC_LOG_INFO(...)   OGS_LOG_IMPL(info, "security", __VA_ARGS__)
#define OGS_SEC_LOG_WARN(...)   OGS_LOG_IMPL(warn, "security", __VA_ARGS__)
#define OGS_SEC_LOG_ERROR(...)  OGS_LOG_IMPL(error, "security", __VA_ARGS__)
#define OGS_SEC_LOG_CRITICAL(...) OGS_LOG_IMPL(critical, "security", __VA_ARGS__)


#define OGS_GAME_LOG(game, level, ...) \
    do { \
        auto logger = OmniGhost::Logging::LoggerManager::Instance().GameLogger(game); \
        if (logger) logger->level(__VA_ARGS__); \
    } while (0)


#define OGS_LOG_IMPL(level, logger_name, ...) \
    do { \
        auto logger = OmniGhost::Logging::LoggerManager::Instance().GetLogger(logger_name); \
        if (logger) logger->level(__VA_ARGS__); \
    } while (0)






inline bool InitializeDefaultLogging() {
    Config config;
    config.logDirectory = OmniGhost::Paths::Logs();
    config.consoleLevel = spdlog::level::info;
    config.fileLevel = spdlog::level::debug;
    config.asyncEnabled = true;
    config.fileEnabled = true;
    config.rotateDaily = true;
    return LoggerManager::Instance().Initialize(config);
}


inline void ShutdownLogging() {
    LoggerManager::Instance().Shutdown();
}

}
