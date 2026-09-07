#pragma once
#pragma warning(push)
#pragma warning(disable: 4100 4189)

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Tracy profiler integration (header-only)
// Define TRACY_ENABLE before including to enable
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.hpp>
#else
// No-op macros when Tracy not enabled
#define ZoneScoped
#define ZoneScopedN(name)
#define ZoneScopedNC(name, color)
#define FrameMark
#define FrameMarkNamed(name)
#define TracyPlot(name, value)
#define TracyPlotConfig(name, config)
#define TracyMessage(msg, size)
#define TracyMessageLiteral(msg)
#define tracy_set_thread_name(name)
#define tracy_get_thread_handle()
#define tracy_force_context_switch()
#define tracy_thread_yield()
#define tracy_shutdown()
#define tracy_profiler_connected() false
#endif


// Prevent Tracy macros from expanding method names
#ifdef FrameMark
#undef FrameMark
#endif
#ifdef FrameMarkNamed
#undef FrameMarkNamed
#endif
namespace OmniGhost::Platform {

// ============================================================
// Tracy Profiler Integration
// ============================================================

class TracyProfiler {
public:
    static TracyProfiler& Instance() noexcept {
        static TracyProfiler instance;
        return instance;
    }
    
    void Initialize(const std::string& outputPath = "") noexcept {
#ifdef TRACY_ENABLE
        // Tracy auto-initializes on first use
        // Set thread name for main thread
        tracy_set_thread_name("Main");
        
        // Start periodic frame capture if connected
        if (tracy_profiler_connected()) {
            running_.store(true);
            captureThread_ = std::thread(&TracyProfiler::CaptureLoop, this);
        }
#endif
    }
    
    void Shutdown() noexcept {
#ifdef TRACY_ENABLE
        running_.store(false);
        if (captureThread_.joinable()) {
            captureThread_.join();
        }
        tracy_shutdown();
#endif
    }
    
    // Zone macros for easy use
    static void BeginZone(const char* name) noexcept {
#ifdef TRACY_ENABLE
        // ZoneScoped uses RAII, so we need manual begin/end
        // This is a simplified wrapper
#endif
    }
    
    static void EndZone() noexcept {
#ifdef TRACY_ENABLE
#endif
    }
    
    // Plot a value over time
    static void Plot(const char* name, float value) noexcept {
#ifdef TRACY_ENABLE
        TracyPlot(name, value);
#endif
    }
    
    // Plot with configuration
    static void PlotConfig(const char* name, float min, float max, bool clamp = true) noexcept {
#ifdef TRACY_ENABLE
        TracyPlotConfig(name, tracy::PlotFormatConfig{min, max, clamp});
#endif
    }
    
    // Send message to profiler
    static void Message(std::string_view msg) noexcept {
#ifdef TRACY_ENABLE
        TracyMessage(msg.data(), msg.size());
#endif
    }
    
    // Mark frame boundary (call once per frame)
    static void FrameMark() noexcept {
#ifdef TRACY_ENABLE
        // FrameMark macro from Tracy
#endif
    }
    
    // Named frame mark
    static void FrameMarkNamed(std::string_view name) noexcept {
        (void)name;
#ifdef TRACY_ENABLE
        // FrameMarkNamed macro from Tracy
#endif
    }
    
    // Set current thread name in profiler
    static void SetThreadName(const char* name) noexcept {
#ifdef TRACY_ENABLE
        tracy_set_thread_name(name);
#endif
    }
    
    // Check if profiler is connected
    [[nodiscard]] static bool IsConnected() noexcept {
#ifdef TRACY_ENABLE
        return tracy_profiler_connected();
#else
        return false;
#endif
    }
    
    // Force context switch (for fiber/coroutine switching)
    static void ForceContextSwitch() noexcept {
#ifdef TRACY_ENABLE
        tracy_force_context_switch();
#endif
    }
    
    // Get thread handle for cross-thread zone tracking
    [[nodiscard]] static uint64_t GetThreadHandle() noexcept {
#ifdef TRACY_ENABLE
        return tracy_get_thread_handle();
#else
        return 0;
#endif
    }
    
    // Zone with color
    struct ScopedZone {
        ScopedZone(const char* name, uint32_t color = 0xFFFFFFFF) noexcept {
#ifdef TRACY_ENABLE
            ZoneScopedNC(name, color);
#endif
        }
        ~ScopedZone() noexcept = default;
    };
    
    // Zone with explicit name
    struct ScopedZoneN {
        ScopedZoneN(const char* name) noexcept {
#ifdef TRACY_ENABLE
            ZoneScopedN(name);
#endif
        }
        ~ScopedZoneN() noexcept = default;
    };

private:
    void CaptureLoop() noexcept {
        while (running_.load()) {
            // Tracy handles its own capture loop
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    
    std::atomic<bool> running_{false};
    std::thread captureThread_;
};

// Convenience macros
#define PROFILER_ZONE TracyProfiler::ScopedZone
#define PROFILER_ZONE_N(name) TracyProfiler::ScopedZoneN(name)
#define PROFILER_FRAME_MARK TracyProfiler::FrameMark
#define PROFILER_FRAME_MARK_NAMED(name) TracyProfiler::FrameMarkNamed(name)
#define PROFILER_PLOT(name, value) TracyProfiler::Plot(name, value)
#define PROFILER_PLOT_CONFIG(name, min, max, clamp) TracyProfiler::PlotConfig(name, min, max, clamp)
#define PROFILER_MESSAGE(msg) TracyProfiler::Message(msg)
#define PROFILER_THREAD_NAME(name) TracyProfiler::SetThreadName(name)
#define PROFILER_CONNECTED TracyProfiler::IsConnected

// Conditional profiling macros (no-op when disabled)
#ifdef TRACY_ENABLE
    #define PROFILER_IF_ENABLED(code) code
#else
    #define PROFILER_IF_ENABLED(code)
#endif

// ============================================================
// Intel VTune / ITT API Integration (optional)
// ============================================================

#ifdef INTEL_VTUNE_ENABLE
#include <ittnotify.h>

class VtuneProfiler {
public:
    static VtuneProfiler& Instance() noexcept {
        static VtuneProfiler instance;
        return instance;
    }
    
    void Initialize() noexcept {
        // VTune initializes automatically
        __itt_initialize();
    }
    
    void Shutdown() noexcept {
        // Nothing special needed
    }
    
    // Domain for organizing traces
    static __itt_domain* GetDomain() noexcept {
        static __itt_domain* domain = __itt_domain_create("OmniGhost");
        return domain;
    }
    
    // Task macros
    struct ScopedTask {
        __itt_task_handle handle;
        ScopedTask(const char* name) noexcept {
            handle = __itt_task_begin(GetDomain(), __itt_null, __itt_null, __itt_string_handle_create(name));
        }
        ~ScopedTask() noexcept {
            __itt_task_end(GetDomain());
        }
    };
    
    // Counter
    static void SetCounter(const char* name, int value) noexcept {
        __itt_counter_set(GetDomain(), __itt_counter_create(name), value);
    }
    
    static void IncrementCounter(const char* name, int delta = 1) noexcept {
        __itt_counter_increment(GetDomain(), __itt_counter_create(name), delta);
    }
    
    // Metadata
    static void SetThreadName(const char* name) noexcept {
        __itt_thread_set_name(name);
    }
    
    static void MarkFrame() noexcept {
        __itt_frame_end_v3(GetDomain(), nullptr);
    }
};

#define VTUNE_TASK(name) VtuneProfiler::ScopedTask _vtune_task(name)
#define VTUNE_COUNTER(name, value) VtuneProfiler::SetCounter(name, value)
#define VTUNE_INC_COUNTER(name, delta) VtuneProfiler::IncrementCounter(name, delta)
#define VTUNE_THREAD_NAME(name) VtuneProfiler::SetThreadName(name)
#define VTUNE_FRAME_MARK VtuneProfiler::MarkFrame

#else
#define VTUNE_TASK(name)
#define VTUNE_COUNTER(name, value)
#define VTUNE_INC_COUNTER(name, delta)
#define VTUNE_THREAD_NAME(name)
#define VTUNE_FRAME_MARK
#endif

// ============================================================
// Unified Profiling Interface
// ============================================================

namespace Profiler {
    
    inline void Initialize() noexcept {
        TracyProfiler::Instance().Initialize();
#ifdef INTEL_VTUNE_ENABLE
        VtuneProfiler::Instance().Initialize();
#endif
    }
    
    inline void Shutdown() noexcept {
        TracyProfiler::Instance().Shutdown();
    }
    
    inline void SetThreadName(const char* name) noexcept {
        TracyProfiler::SetThreadName(name);
#ifdef INTEL_VTUNE_ENABLE
        VtuneProfiler::SetThreadName(name);
#endif
    }
    
    inline void FrameMark() noexcept {
        TracyProfiler::FrameMark();
#ifdef INTEL_VTUNE_ENABLE
        VtuneProfiler::MarkFrame();
#endif
    }
    
    inline void FrameMarkNamed(std::string_view name) noexcept {
        TracyProfiler::FrameMarkNamed(name);
    }
    
    inline void Plot(std::string_view name, float value) noexcept {
        TracyProfiler::Plot(name.data(), value);
    }
    
    inline void PlotConfig(std::string_view name, float min, float max, bool clamp = true) noexcept {
        TracyProfiler::PlotConfig(name.data(), min, max, clamp);
    }
    
    inline void Message(std::string_view msg) noexcept {
        TracyProfiler::Message(msg);
    }
    
    inline bool IsConnected() noexcept {
        return TracyProfiler::IsConnected();
    }
    
    // Scoped zone
    class Zone {
    public:
        explicit Zone(const char* name, uint32_t color = 0xFFFFFFFF) noexcept
            : zone_(name, color) {}
        ~Zone() noexcept = default;
    private:
        TracyProfiler::ScopedZone zone_;
    };
    
    class ZoneN {
    public:
        explicit ZoneN(const char* name) noexcept
            : zone_(name) {}
        ~ZoneN() noexcept = default;
    private:
        TracyProfiler::ScopedZoneN zone_;
    };
}

} // namespace OmniGhost::Platform
#pragma warning(pop)
