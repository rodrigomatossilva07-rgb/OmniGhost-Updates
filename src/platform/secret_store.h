#pragma once

// ============================================================================
// OmniGhost local secret store (SEC-004).
//
// Persistence for machine-local secrets that must survive restarts (session
// tokens, installation identifiers). Secrets are sealed with Windows DPAPI
// (user scope) before touching the disk, under:
//     %LOCALAPPDATA%\OmniGhost\secrets\<name>.dpapi
//
// NEVER store secrets inside config JSON, the executable or the source tree.
// Values are never logged; callers receive an ogs::SecretText whose buffer is
// zeroized on destruction. See src\project\config\strings\README.md.
// ============================================================================

#include "platform/strprotect/strprotect.h"

#include <string_view>

namespace OmniGhost::Secrets {

// Seals `plaintext` with DPAPI (user scope) and writes it under the secret
// store directory. Overwrites any previous value.
bool Store(std::string_view name, std::string_view plaintext);

// Loads and unseals a secret. Returns an empty (invalid) SecretText when the
// secret does not exist or cannot be unsealed.
ogs::SecretText Load(std::string_view name);

// Convenience: Load + empty check.
bool Exists(std::string_view name);

// Deletes the stored secret (best effort).
void Delete(std::string_view name);

} // namespace OmniGhost::Secrets
