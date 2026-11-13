// Standalone runner for the OmniGhost String Protection self-test.
// Built and executed by src\project\tools\Test-StringProtectionRuntime.ps1.

#include "platform/strprotect/strprotect.h"

#include <cstdio>

int main()
{
    if (!ogs::SelfTest()) {
        std::puts("[FAIL] ogs::SelfTest() reported failures");
        return 1;
    }
    std::puts("[OK] ogs::SelfTest() passed (RFC 8439 vectors, roundtrips, RAII wipe, concurrency)");
    return 0;
}
