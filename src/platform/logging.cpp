#include "logging.h"
#include "app_paths.h"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/null_sink.h>

#include <chrono>
#include <filesystem>

namespace OmniGhost::Logging {

bool LoggerManager::Initialize(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return true;
    }

    config_ = config;

    // Initialize async logger if enabled
    if (config_.asyncEnabled) {
        spdlog::init_thread_pool(config_.asyncQueueSize, config_.asyncThreadCount);
    }

    // Create sinks
    std::vector<spdlog::sink_ptr> consoleSinks;
    std::vector<spdlog::sink_ptr> fileSinks;

    // Console sink (color)
    if (config_.consoleLevel != spdlog::level::off) {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(config_.consoleLevel);
        consoleSink->set_pattern(config_.pattern);
        consoleSinks.push_back(consoleSink);
    }

    // File sink
    if (config_.fileEnabled) {
        std::filesystem::path logDir = config_.logDirectory.empty()
            ? OmniGhost::Paths::LogDirectory()
            : config_.logDirectory;

        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);

        std::filesystem::path logFile = logDir / (config_.logFilePrefix + ".log");

        spdlog::sink_ptr fileSink;
        if (config_.rotateDaily) {
            fileSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                logFile.string(), 0, 0);  // Rotate at midnight
        } else {
            fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFile.string(), config_.maxFileSize, config_.maxFiles);
        }

        fileSink->set_level(config_.fileLevel);
        fileSink->set_pattern(config_.filePattern);
        fileSinks.push_back(fileSink);
    }

    // Create core logger
    {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.insert(sinks.end(), consoleSinks.begin(), consoleSinks.end());
        sinks.insert(sinks.end(), fileSinks.begin(), fileSinks.end());

        if (config_.asyncEnabled && !sinks.empty()) {
            coreLogger_ = std::make_shared<spdlog::async_logger>(
                "core", sinks.begin(), sinks.end(),
                spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        } else if (!sinks.empty()) {
            coreLogger_ = std::make_shared<spdlog::logger>("core", sinks.begin(), sinks.end());
        } else {
            coreLogger_ = std::make_shared<spdlog::logger>("core", std::make_shared<spdlog::sinks::null_sink_mt>());
        }

        coreLogger_->set_level(spdlog::level::trace);
        coreLogger_->flush_on(config_.flushLevel);
        spdlog::register_logger(coreLogger_);
    }

    // Create UI logger
    {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.insert(sinks.end(), consoleSinks.begin(), consoleSinks.end());
        sinks.insert(sinks.end(), fileSinks.begin(), fileSinks.end());

        if (config_.asyncEnabled && !sinks.empty()) {
            uiLogger_ = std::make_shared<spdlog::async_logger>(
                "ui", sinks.begin(), sinks.end(),
                spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        } else if (!sinks.empty()) {
            uiLogger_ = std::make_shared<spdlog::logger>("ui", sinks.begin(), sinks.end());
        } else {
            uiLogger_ = std::make_shared<spdlog::logger>("ui", std::make_shared<spdlog::sinks::null_sink_mt>());
        }

        uiLogger_->set_level(spdlog::level::trace);
        uiLogger_->flush_on(config_.flushLevel);
        spdlog::register_logger(uiLogger_);
    }

    // Create DMA logger
    {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.insert(sinks.end(), consoleSinks.begin(), consoleSinks.end());
        sinks.insert(sinks.end(), fileSinks.begin(), fileSinks.end());

        if (config_.asyncEnabled && !sinks.empty()) {
            dmaLogger_ = std::make_shared<spdlog::async_logger>(
                "dma", sinks.begin(), sinks.end(),
                spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        } else if (!sinks.empty()) {
            dmaLogger_ = std::make_shared<spdlog::logger>("dma", sinks.begin(), sinks.end());
        } else {
            dmaLogger_ = std::make_shared<spdlog::logger>("dma", std::make_shared<spdlog::sinks::null_sink_mt>());
        }

        dmaLogger_->set_level(spdlog::level::trace);
        dmaLogger_->flush_on(config_.flushLevel);
        spdlog::register_logger(dmaLogger_);
    }

    // Create Security logger
    {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.insert(sinks.end(), consoleSinks.begin(), consoleSinks.end());
        sinks.insert(sinks.end(), fileSinks.begin(), fileSinks.end());

        if (config_.asyncEnabled && !sinks.empty()) {
            securityLogger_ = std::make_shared<spdlog::async_logger>(
                "security", sinks.begin(), sinks.end(),
                spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        } else if (!sinks.empty()) {
            securityLogger_ = std::make_shared<spdlog::logger>("security", sinks.begin(), sinks.end());
        } else {
            securityLogger_ = std::make_shared<spdlog::logger>("security", std::make_shared<spdlog::sinks::null_sink_mt>());
        }

        securityLogger_->set_level(spdlog::level::trace);
        securityLogger_->flush_on(config_.flushLevel);
        spdlog::register_logger(securityLogger_);
    }

    // Set up periodic flush
    if (config_.asyncEnabled) {
        spdlog::set_default_logger(coreLogger_);
        spdlog::flush_every(config_.flushInterval);
    }

    initialized_ = true;

    // Log initialization
    OGS_LOG_INFO("Logging system initialized");
    OGS_LOG_INFO("Async: {}, Console level: {}, File level: {}",
                 config_.asyncEnabled,
                 spdlog::level::to_string_view(config_.consoleLevel),
                 spdlog::level::to_string_view(config_.fileLevel));

    return true;
}

void LoggerManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    // Flush all loggers
    Flush();

    // Drop all registered loggers
    spdlog::drop_all();

    // Shutdown thread pool
    if (config_.asyncEnabled) {
        spdlog::shutdown();
    }

    coreLogger_.reset();
    uiLogger_.reset();
    dmaLogger_.reset();
    securityLogger_.reset();

    initialized_ = false;
}

std::shared_ptr<spdlog::logger> LoggerManager::GetLogger(std::string_view name) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        // Try to initialize with defaults if not initialized
        Config defaultConfig;
        defaultConfig.logDirectory = OmniGhost::Paths::LogDirectory();
        Initialize(defaultConfig);
    }

    // Check if logger already exists
    auto existing = spdlog::get(name.data());
    if (existing) {
        return existing;
    }

    // Create new logger with same sinks as core
    std::vector<spdlog::sink_ptr> sinks;
    if (coreLogger_) {
        sinks = coreLogger_->sinks();
    }

    std::shared_ptr<spdlog::logger> logger;
    if (config_.asyncEnabled && !sinks.empty()) {
        logger = std::make_shared<spdlog::async_logger>(
            std::string(name), sinks.begin(), sinks.end(),
            spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    } else if (!sinks.empty()) {
        logger = std::make_shared<spdlog::logger>(std::string(name), sinks.begin(), sinks.end());
    } else {
        logger = std::make_shared<spdlog::logger>(std::string(name), std::make_shared<spdlog::sinks::null_sink_mt>());
    }

    logger->set_level(spdlog::level::trace);
    logger->flush_on(config_.flushLevel);
    spdlog::register_logger(logger);

    return logger;
}

void LoggerManager::SetLevel(spdlog::level::level_enum level) {
    std::lock_guard<std::mutex> lock(mutex_);

    spdlog::set_level(level);

    if (coreLogger_) coreLogger_->set_level(level);
    if (uiLogger_) uiLogger_->set_level(level);
    if (dmaLogger_) dmaLogger_->set_level(level);
    if (securityLogger_) securityLogger_->set_level(level);
}

void LoggerManager::SetLevel(std::string_view name, spdlog::level::level_enum level) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto logger = spdlog::get(name.data());
    if (logger) {
        logger->set_level(level);
    }
}

void LoggerManager::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (coreLogger_) coreLogger_->flush();
    if (uiLogger_) uiLogger_->flush();
    if (dmaLogger_) dmaLogger_->flush();
    if (securityLogger_) securityLogger_->flush();

    spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) { l->flush(); });
}

} // namespace OmniGhost::Logging