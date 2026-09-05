#include "platform/session_log.h"

namespace OmniGhost::SessionLog {
void Write(Severity, Subsystem, std::string_view, std::initializer_list<Field>) {}
}
