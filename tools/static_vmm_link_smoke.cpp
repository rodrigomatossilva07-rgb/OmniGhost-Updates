// PRIVATE DEVELOPMENT BUILD - NOT FOR DISTRIBUTION
// Link-only smoke test for the GPLv3 LeechCore and AGPLv3 MemProcFS/VMM
// source-integration prototype. Upstream copyright and license files remain in
// third_party/upstream and must accompany any future source-based work.
#include "vmmdll.h"

extern "C" BOOL LcStaticInitialize();
extern "C" VOID LcStaticShutdown();

int main()
{
    // Deliberately do not open hardware in the build smoke test. Referencing an
    // API forces the linker to resolve the static VMM -> LeechCore graph.
    if (!LcStaticInitialize() || !VMMDLL_StaticInitialize())
        return 2;
    VMMDLL_Close(nullptr);
    LcStaticShutdown();
    return 0;
}
