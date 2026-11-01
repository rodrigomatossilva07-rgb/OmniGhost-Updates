



#ifndef PCH_H
#define PCH_H





#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

#include <Windows.h>





#ifndef NTSTATUS
#define NTSTATUS LONG
#endif
#pragma warning(push)
#pragma warning(disable: 4200 4201)
#include <vmmdll.h>
#include <leechcore.h>
#pragma warning(pop)





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

#endif
