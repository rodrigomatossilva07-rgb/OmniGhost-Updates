#pragma once
#include <cstdint>
#include <d3d11.h>

namespace CS2_WeaponIcons {

// Load PNG icons from Cs2/data/weapons/ (or data/weapons next to exe).
// Safe to call multiple times; no-ops if already loaded.
void EnsureLoaded(ID3D11Device* device);

// Returns SRV for the weapon def index, or nullptr if missing.
ID3D11ShaderResourceView* Get(int weapon_def);

// Pixel size of the underlying texture (0 if missing).
void GetSize(int weapon_def, int& out_w, int& out_h);

// Filename stem for a def index (e.g. "weapon_ak47"), or nullptr.
const char* FileStem(int weapon_def);

void Shutdown();

} // namespace CS2_WeaponIcons
