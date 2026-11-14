#pragma once














#include "platform/strprotect/strprotect.h"

#include <string_view>

namespace OmniGhost::Secrets {



bool Store(std::string_view name, std::string_view plaintext);



ogs::SecretText Load(std::string_view name);


bool Exists(std::string_view name);


void Delete(std::string_view name);

}
