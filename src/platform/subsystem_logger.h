#pragma once

#include "session_log.h"
#include <string>
#include <string_view>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace OmniGhost::Platform {

// Item 55: Separate logs by subsystem (PnP, FTDI, LeechCore, VMM, Read, Adapter)

enum class LogSubsystem : uint8_t {
    Core = 0,
    PnP = 1,
    FTDI = 2,
    LeechCore = 3,
    VMM = 4,
    DMARead = 5,
    DMAWrite = 5,
    Adapter = 6,
    Updater = 7,
    Config = 8,
    UI = 9,
    Network = 10,
    Filesystem = 11,
    DMA = 12,
    Hardware = 13,
};

struct SubsystemLogger {
    explicit SubsystemLogger(LogSubsystem subsystem) : subsystem_(subsystem) {}

    template <typename... Args>
    void Info(std::string_view message, Args&&... args) const {
        Write(Severity::Info, message, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Warning(std::string_view message, Args&&... args) const {
        Write(Severity::Warning, message, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Error(std::string_view message, Args&&... args) const {
        Write(Severity::Error, message, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Debug(std::string_view message, Args&&... args) const {
        Write(Severity::Debug, message, std::forward<Args>(args)...);
    }

    void Info(std::string_view message, std::initializer_list<SessionLog::Field> fields = {}) const {
        Write(Severity::Info, message, fields);
    }

    void Warning(std::string_view message, std::initializer_list<SessionLog::Field> fields = {}) const {
        Write(Severity::Warning, message, fields);
    }

    void Error(std::string_view message, std::initializer_list<SessionLog::Field> fields = {}) const {
        Write(Severity::Error, message, fields);
    }

    void Debug(std::string_view message, std::initializer_list<SessionLog::Field> fields = {}) const {
        Write(Severity::Debug, message, fields);
    }

private:
    LogSubsystem subsystem_;

    template <typename... Args>
    void Write(Severity severity, std::string_view message, Args&&... args) const {
        std::string formatted = FormatMessage(message, std::forward<Args>(args)...);
        SessionLog::Write(static_cast<SessionLog::Severity>(severity), subsystem_, formatted, {});
    }

    void Write(Severity severity, std::string_view message,
               std::initializer_list<SessionLog::Field> fields) const {
        SessionLog::Write(static_cast<SessionLog::Severity>(severity), subsystem_, message, fields);
    }

    template <typename... Args>
    std::string FormatMessage(std::string_view fmt, Args&&... args) const {
        // Simple formatting - could use fmtlib or std::format in C++20
        std::string result(fmt);
        size_t pos = 0;
        ((pos = result.find("{}", pos), pos != std::string::npos ?
         (result.replace(pos, 2, std::to_string(args)), pos += std::to_string(args).length()) :
         pos), ...);
        return result;
    }
};

// Global subsystem loggers
inline const SubsystemLogger LogCore{LogSubsystem::Core};
inline const SubsystemLogger LogPnP{LogSubsystem::PnP};
inline const SubsystemLogger LogFTDI{LogSubsystem::FTDI};
inline const SubsystemLogger LogLeechCore{LogSubsystem::LeechCore};
inline const SubsystemLogger LogVMM{LogSubsystem::VMM};
inline const SubsystemLogger LogDMARead{LogSubsystem::DMARead};
inline const SubsystemLogger LogDMAWrite{LogSubsystem::DMAWrite};
inline const SubsystemLogger LogAdapter{LogSubsystem::Adapter};
inline const SubsystemLogger LogUpdater{LogSubsystem::Updater};
inline const SubsystemLogger LogConfig{LogSubsystem::Config};
inline const SubsystemLogger LogUI{LogSubsystem::UI};
inline const SubsystemLogger LogNetwork{LogSubsystem::Network};
inline const SubsystemLogger LogFilesystem{LogSubsystem::Filesystem};
inline const SubsystemLogger LogDMA{LogSubsystem::DMA};
inline const SubsystemLogger LogHardware{LogSubsystem::Hardware};

// Helper to get subsystem name
[[nodiscard]] inline std::string_view SubsystemName(LogSubsystem s) noexcept {
    switch (s) {
        case LogSubsystem::Core: return "core";
        case LogSubsystem::PnP: return "pnp";
        case LogSubsystem::FTDI: return "ftdi";
        case LogSubsystem::LeechCore: return "leechcore";
        case LogSubsystem::VMM: return "vmm";
        case LogSubsystem::DMARead: return "dma_read";
        case LogSubsystem::DMAWrite: return "dma_write";
        case LogSubsystem::Adapter: return "adapter";
        case LogSubsystem::Updater: return "updater";
        case LogSubsystem::Config: return "config";
        case LogSubsystem::UI: return "ui";
        case LogSubsystem::Network: return "network";
        case LogSubsystem::Filesystem: return "filesystem";
        case LogSubsystem::DMA: return "dma";
        case LogSubsystem::Hardware: return "hardware";
        default: return "unknown";
    }
}

} // namespace OmniGhost::Platform