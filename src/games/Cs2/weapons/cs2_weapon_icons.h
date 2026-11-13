#pragma once
#include <cstdint>
#include <d3d11.h>

namespace CS2_WeaponIcons {



void EnsureLoaded(ID3D11Device* device);


ID3D11ShaderResourceView* Get(int weapon_def);


void GetSize(int weapon_def, int& out_w, int& out_h);


const char* FileStem(int weapon_def);

void Shutdown();

}
