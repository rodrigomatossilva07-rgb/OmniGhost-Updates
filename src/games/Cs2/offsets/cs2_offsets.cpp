#include "../cs2_game.h"

namespace CS2 {

bool Offsets::Validate(std::string* reason) const noexcept {
    const auto fail = [&](const char* text) {
        if (reason)
            *reason = text;
        return false;
    };
    const auto module_offset = [](uintptr_t value) {
        return value != 0 && value < 0x40000000ull;
    };
    const auto field_offset = [](uintptr_t value) {
        return value != 0 && value < 0x100000ull;
    };

    if (!module_offset(dwEntityList)) return fail("dwEntityList invalido");
    if (!module_offset(dwLocalPlayerPawn)) return fail("dwLocalPlayerPawn invalido");
    if (!module_offset(dwViewMatrix)) return fail("dwViewMatrix invalido");
    if (!field_offset(m_iHealth)) return fail("m_iHealth invalido");
    if (!field_offset(m_iTeamNum)) return fail("m_iTeamNum invalido");
    if (!field_offset(m_pGameSceneNode)) return fail("m_pGameSceneNode invalido");
    if (!field_offset(m_hPlayerPawn)) return fail("m_hPlayerPawn invalido");
    if (!field_offset(m_vecAbsOrigin)) return fail("m_vecAbsOrigin invalido");
    if (!field_offset(m_modelState)) return fail("m_modelState invalido");
    if (!field_offset(BoneArray)) return fail("BoneArray invalido");
    if (reason)
        reason->clear();
    return true;
}

} // namespace CS2
