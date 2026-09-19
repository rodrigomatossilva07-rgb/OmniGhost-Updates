#pragma once
#include <cstdint>
#include <cstring>

namespace CS2_Weapons {

// Common CS2 weapon definition indexes (item definition index).
inline const char* NameFromDefIndex(int def) {
    switch (def) {
    case 1: return "Desert Eagle";
    case 2: return "Dual Berettas";
    case 3: return "Five-SeveN";
    case 4: return "Glock-18";
    case 7: return "AK-47";
    case 8: return "AUG";
    case 9: return "AWP";
    case 10: return "FAMAS";
    case 11: return "G3SG1";
    case 13: return "Galil AR";
    case 14: return "M249";
    case 16: return "M4A4";
    case 17: return "MAC-10";
    case 19: return "P90";
    case 23: return "MP5-SD";
    case 24: return "UMP-45";
    case 25: return "XM1014";
    case 26: return "PP-Bizon";
    case 27: return "MAG-7";
    case 28: return "Negev";
    case 29: return "Sawed-Off";
    case 30: return "Tec-9";
    case 31: return "Zeus x27";
    case 32: return "P2000";
    case 33: return "MP7";
    case 34: return "MP9";
    case 35: return "Nova";
    case 36: return "P250";
    case 38: return "SCAR-20";
    case 39: return "SG 553";
    case 40: return "SSG 08";
    case 41: return "Knife";
    case 42: return "Knife";
    case 43: return "Flashbang";
    case 44: return "HE Grenade";
    case 45: return "Smoke Grenade";
    case 46: return "Molotov";
    case 47: return "Decoy";
    case 48: return "Incendiary";
    case 49: return "C4";
    case 59: return "Knife";
    case 60: return "M4A1-S";
    case 61: return "USP-S";
    case 63: return "CZ75-Auto";
    case 64: return "R8 Revolver";
    case 500: return "Bayonet";
    case 503: return "Classic Knife";
    case 505: return "Flip Knife";
    case 506: return "Gut Knife";
    case 507: return "Karambit";
    case 508: return "M9 Bayonet";
    case 509: return "Huntsman";
    case 512: return "Falchion";
    case 514: return "Bowie";
    case 515: return "Butterfly";
    case 516: return "Shadow Daggers";
    case 517: return "Paracord";
    case 518: return "Survival";
    case 519: return "Ursus";
    case 520: return "Navaja";
    case 521: return "Nomad";
    case 522: return "Stiletto";
    case 523: return "Talon";
    case 525: return "Skeleton";
    case 526: return "Kukri";
    default: return nullptr;
    }
}

inline bool IsSniper(int def) {
    return def == 9 || def == 11 || def == 38 || def == 40;
}

inline bool IsGrenade(int def) {
    return def >= 43 && def <= 48;
}

inline bool IsRifle(int def) {
    switch (def) {
    case 7: case 8: case 10: case 13: case 16: case 39: case 60: return true;
    default: return false;
    }
}

inline bool IsPistol(int def) {
    switch (def) {
    case 1: case 2: case 3: case 4: case 30: case 32: case 36: case 61: case 63: case 64: return true;
    default: return false;
    }
}

inline const char* SafeName(int def) {
    if (const char* n = NameFromDefIndex(def)) return n;
    return "Arma";
}

} // namespace CS2_Weapons
