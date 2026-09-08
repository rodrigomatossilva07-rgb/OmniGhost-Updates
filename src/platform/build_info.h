#pragma once

#if __has_include("omni_build_metadata.h")
#include "omni_build_metadata.h"
#else
namespace OmniGhost::BuildInfo {
inline constexpr char AppVersion[] = "unknown";
inline constexpr char BuildId[] = "local-unknown";
inline constexpr char CommitId[] = "unknown";
inline constexpr char SourceProvenance[] = "git";
inline constexpr char BuildUtc[] = "unknown";
inline constexpr char ToolchainVersion[] = "unknown";
inline constexpr char WindowsSdkVersion[] = "unknown";
inline constexpr char CompilerVersion[] = "unknown";
inline constexpr char Architecture[] = "x64";
inline constexpr char Configuration[] = "unknown";
inline constexpr char ReleaseChannel[] = "unknown";
inline constexpr char MemProcFSVersion[] = "unknown";
inline constexpr char LeechCoreVersion[] = "unknown";
inline constexpr bool Reproducible = false;
}
#endif
