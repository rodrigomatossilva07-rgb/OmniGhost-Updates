#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace OmniGhost::Diagnostics {




bool CreatePackage(std::string_view summary,
                   std::filesystem::path& outputPath,
                   std::string& error);

}
