#include "games/Fivem/objects/object_explorer.h"

#include <cstdlib>
#include <chrono>
#include <iostream>
#include <string>
#include <unordered_set>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}

int main() {
    using namespace FiveM::ObjectExplorer;

    Reset();
    Require(IsVisible(0x12345678u), "a new model must be visible by default");
    SetFavorite(0xABCDEF12u, true);
    SetFavorite(0x00000001u, true);
    SetVisible(0x12345678u, false);
    SetIgnored(0x87654321u, true);
    Require(IsFavorite(0xABCDEF12u), "favorite insertion failed");
    Require(!IsVisible(0x12345678u), "hidden model is still visible");
    Require(IsIgnored(0x87654321u), "ignored insertion failed");
    Require(!IsVisible(0x87654321u), "ignored model is still visible");

    const std::string serialized = SerializeHashSet(config.favorites);
    Require(serialized == "00000001,ABCDEF12", "hash serialization is not deterministic");

    std::unordered_set<std::uint32_t> parsed;
    Require(DeserializeHashSet("0x00000001, ABCDEF12", parsed), "valid hash list rejected");
    Require(parsed.size() == 2 && parsed.contains(1u) && parsed.contains(0xABCDEF12u),
        "valid hash list parsed incorrectly");
    const auto beforeInvalid = parsed;
    Require(!DeserializeHashSet("ABCDEF12,not-a-hash", parsed), "invalid hash accepted");
    Require(parsed == beforeInvalid, "invalid input partially replaced destination");

    Require(MatchesSearch("prop_tool_box_04", 0xABCDEF12u, "TOOL_BOX"),
        "case-insensitive name search failed");
    Require(MatchesSearch("prop_tool_box_04", 0xABCDEF12u, "abcdef12"),
        "hash search failed");
    Require(MatchesSearch("prop_tool_box_04", 0xABCDEF12u, "0xABCD"),
        "prefixed partial hash search failed");
    Require(!MatchesSearch("prop_tool_box_04", 0xABCDEF12u, "vehicle"),
        "unrelated search matched");

    // Synthetic data: this validates bounded list handling only; it is not
    // presented as evidence of a real FiveM object pool.
    std::unordered_set<std::uint32_t> synthetic;
    for (std::uint32_t value = 1; value <= 4096; ++value) synthetic.insert(value);
    const auto perfStart = std::chrono::steady_clock::now();
    const std::string largeSerialized = SerializeHashSet(synthetic);
    std::unordered_set<std::uint32_t> largeParsed;
    Require(DeserializeHashSet(largeSerialized, largeParsed), "4096-item list rejected");
    Require(largeParsed == synthetic, "4096-item list round-trip mismatch");
    for (std::uint32_t value = 1; value <= 4096; ++value)
        Require(MatchesSearch("prop_synthetic_model", value, "synthetic"),
            "synthetic search failed");
    const auto perfMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - perfStart).count();
    std::cout << "Synthetic 4096-model round-trip/search: " << perfMs << " ms\n";

    Reset();
    Require(config.favorites.empty() && config.hiddenModels.empty() &&
        config.ignoredModels.empty(), "reset did not clear all lists");

    std::cout << "[FiveM Object Explorer Tests] PASS\n";
    return 0;
}
