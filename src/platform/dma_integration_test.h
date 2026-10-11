#pragma once
#include <cstdint>

namespace OmniGhost::Platform {

// DMA integration test entry point
// Called when --integration-test flag is passed
// Returns: 0 = success, non-zero = error code
int RunDmaIntegrationTest(int argc, wchar_t** argv);

} // namespace OmniGhost::Platform