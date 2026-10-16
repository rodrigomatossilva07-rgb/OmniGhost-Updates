#include "config_legacy.h"
#include "config_manager.h"

namespace config_manager {

std::vector<std::string> ListHistory() {
    std::vector<std::string> out;
    for (const auto& s : saved_list) {
        if (s.name.rfind("_autosave", 0) == 0)
            out.push_back(s.name);
    }
    return out;
}

}
