// pch.h: shared header used by the DMA layer.
// Keep C++ standard-library headers before the C ABI headers from MemProcFS.
// This is important because VmmOwned uses std::unique_ptr below.

#ifndef PCH_H
#define PCH_H

// C++ standard library used directly by this header and by DMA translation units.
// These must be included before VmmOwned is declared.  The previous ordering
// declared std::unique_ptr before <memory>, which caused the first C2039/C2061
// errors and then hundreds of parser-recovery errors inside MSVC's CRT headers.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

#include <Windows.h>

// MemProcFS/LeechCore public API.
// Recent vmmdll.h versions use NTSTATUS without pulling in ntdef.h. Defining
// the documented Windows-compatible underlying type here avoids coupling all
// DMA translation units to the NT kernel headers.
#ifndef NTSTATUS
#define NTSTATUS LONG
#endif
#include <vmmdll.h>

// Resources returned through VMMDLL_* APIs are owned by MemProcFS and must
// be released with VMMDLL_MemFree. Keeping the deleter next to the canonical
// API include makes ownership explicit at every call site without changing the
// underlying allocation contract.
struct VmmMemDeleter
{
    template <typename T>
    void operator()(T* ptr) const noexcept
    {
        if (ptr)
            VMMDLL_MemFree(ptr);
    }
};

template <typename T>
using VmmOwned = std::unique_ptr<T, VmmMemDeleter>;

//#define DEBUG_INFO // quieter Release
#ifdef DEBUG_INFO
#define LOG(fmt, ...) std::printf(fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) std::wprintf(fmt, ##__VA_ARGS__)
#else
#define LOG(...) ((void)0)
#define LOGW(...) ((void)0)
#endif

#define THROW_EXCEPTION
#ifdef THROW_EXCEPTION
#define THROW(fmt, ...) throw std::runtime_error(fmt, ##__VA_ARGS__)
#endif

#endif // PCH_H
