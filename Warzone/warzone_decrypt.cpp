#include "warzone_decrypt.h"
#include "warzone_game.h"
#include "../DMALibrary/Memory/Memory.h"
#include <Windows.h>
#include <intrin.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstdint>

extern Memory mem;

namespace Warzone {

bool IsCanonicalUserPtr(uintptr_t p) {
    return p > 0x10000ULL && p < 0x00007FFFFFFFFFFFULL;
}

bool PointerReadable(uintptr_t p) {
    if (!IsCanonicalUserPtr(p)) return false;
    uint8_t b = 0;
    return mem.Read(p, &b, 1);
}

bool ReadU64(uintptr_t addr, uintptr_t& out) {
    out = 0;
    return mem.Read(addr, &out, sizeof(out));
}

// ---- DMA adapter for the Blonde94 / UC decrypt primitives ----
using ReadFn = bool(*)(void* ctx, std::uint64_t addr, void* dst, std::size_t size);

bool DmaReadFn(void* /*ctx*/, std::uint64_t addr, void* dst, std::size_t size) {
    if (!addr || !dst || !size) return false;
    return mem.Read((uintptr_t)addr, dst, size);
}

template <typename T>
inline T rd(ReadFn fn, void* ctx, std::uint64_t a) {
    T v{};
    if (fn && a) fn(ctx, a, &v, sizeof(T));
    return v;
}

inline bool is_heap(std::uint64_t p) {
    return p >= 0x10000ull && p <= 0x00007FFFFFFFFFFFull;
}

// Build 0x6A6D4AEF — matches data/warzone_offsets.json + cheatoffsets Steam dump.
// Runtime offsets.* can override these if non-zero (see RunDecrypt).
namespace wz_rva {
    inline std::uint64_t client_info_enc = 0xEB72B98;
    inline std::uint64_t client_info_key = 0xD92F779;
    inline std::uint64_t client_base_enc = 0x23CDA8;
    inline std::uint64_t client_base_key = 0xD92F7AA;
    inline std::uint64_t bone_enc       = 0x12EC3488;
    inline std::uint64_t bone_key       = 0xD92F89D;
    inline std::uint64_t ref_def        = 0xEB841A0;
    inline std::uint64_t camera_pos_enc = 0x160;
    inline std::uint64_t camera_pos_key = 0x16C;
}

void SyncRvaFromOffsets() {
    if (offsets.client_info_enc) wz_rva::client_info_enc = offsets.client_info_enc;
    if (offsets.client_info_key) wz_rva::client_info_key = offsets.client_info_key;
    if (offsets.client_base_enc) wz_rva::client_base_enc = offsets.client_base_enc;
    if (offsets.client_base_key) wz_rva::client_base_key = offsets.client_base_key;
    if (offsets.bone_enc)       wz_rva::bone_enc       = offsets.bone_enc;
    if (offsets.bone_key)       wz_rva::bone_key       = offsets.bone_key;
    if (offsets.ref_def)        wz_rva::ref_def        = offsets.ref_def;
    if (offsets.camera_pos_enc) wz_rva::camera_pos_enc = offsets.camera_pos_enc;
    if (offsets.camera_pos_key) wz_rva::camera_pos_key = offsets.camera_pos_key;
}

// Throttled debug
void LogCi(uintptr_t mb, uintptr_t raw, uintptr_t result, int mode, const char* reason) {
    static uint64_t last = 0;
    const uint64_t now = GetTickCount64();
    if (last && now - last < 2500) return;
    last = now;
    std::clog << "[DEBUG][Warzone][client_info] base=0x" << std::hex << mb
              << " raw_enc=0x" << raw
              << " result=0x" << result
              << " mode=" << std::dec << mode
              << " heap=" << (is_heap(result) ? "YES" : "NO")
              << " reason=" << reason << std::endl;
}

void LogCb(uintptr_t ci, uintptr_t result, int sw, const char* reason) {
    static uint64_t last = 0;
    const uint64_t now = GetTickCount64();
    if (last && now - last < 2500) return;
    last = now;
    std::clog << "[DEBUG][Warzone][client_base] ci=0x" << std::hex << ci
              << " result=0x" << result
              << " switch=" << std::dec << sw
              << " heap=" << (is_heap(result) ? "YES" : "NO")
              << " reason=" << reason << std::endl;
}


struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Key3 { std::int32_t ref0, ref1, ref2; };
struct RefDef {
    std::int32_t x, y, width, height;
    Vec2 fov;
    char pad1[8];
    char pad2[4];
    Vec3 axis[3];
};

inline std::uint64_t get_refdef_ptr(ReadFn fn, void* ctx, std::uint64_t base) {
    if (!base) return 0;
    const std::uint64_t addr = base + wz_rva::ref_def;
    const Key3 enc = rd<Key3>(fn, ctx, addr);
    const std::uint64_t a0 = enc.ref2 ^ addr;
    const std::uint64_t a4 = enc.ref2 ^ (addr + 4);
    const std::uint32_t lower = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(enc.ref0) ^ static_cast<std::uint32_t>(a0 * (a0 + 2)));
    const std::uint32_t upper = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(enc.ref1) ^ static_cast<std::uint32_t>(a4 * (a4 + 2)));
    const std::uint64_t p = (static_cast<std::uint64_t>(upper) << 32) | lower;
    return is_heap(p) ? p : 0;
}

// ---- Live STEAM decrypts (0x6A6D4AEF) ----
// Blonde94 / UC steam 0x6A6D4AEF — MUST use rdx = ~peb (not base).
// mode: 0=~peb  1=peb  2=base  3=0  (0 is correct; others for probe only)
inline std::uint64_t decrypt_client_info_mode(ReadFn fn, void* ctx, std::uint64_t base, std::uint64_t peb, int mode)
{
    if (!base) return 0;
    const std::uint64_t mb = base;
    std::uint64_t rax = mb, rbx = mb, rcx = mb, rdx = mb, r9 = mb;
    rbx = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_info_enc);
    if (!rbx)
        return 0;
    r9 = base;
    rax = rbx;
    rax >>= 0x18;
    rcx = 0;
    rax ^= rbx;
    rcx = _rotl64(rcx, 0x10);
    rcx ^= rd<std::uint64_t>(fn, ctx, base + wz_rva::client_info_key);
    rbx = rax;
    rbx >>= 0x30;
    rcx = ~rcx;
    rbx ^= rax;
    rax = 0x233F5F4AE79533B1ull;
    rbx *= rax;
    rax = 0x4FF2ED27F19D575Dull;
    if (mode == 0) rdx = ~peb;
    else if (mode == 1) rdx = peb;
    else if (mode == 2) rdx = base;
    else rdx = 0;
    rbx -= rdx;
    rbx += rax;
    rbx ^= r9;
    // Multiplier table: *(~key + 0x19) — must be readable
    const std::uint64_t mul = rd<std::uint64_t>(fn, ctx, rcx + 0x19);
    if (!mul) return 0;
    rbx *= mul;
    return rbx;
}

inline std::uint64_t decrypt_client_info(ReadFn fn, void* ctx, std::uint64_t base, std::uint64_t peb)
{
    if (!peb) return 0;
    // Correct path first
    {
        const std::uint64_t p = decrypt_client_info_mode(fn, ctx, base, peb, 0);
        if (is_heap(p)) return p;
    }
    // Fallbacks only if ~peb failed (wrong PEB?)
    for (int mode = 1; mode < 4; ++mode) {
        const std::uint64_t p = decrypt_client_info_mode(fn, ctx, base, peb, mode);
        if (is_heap(p)) return p;
    }
    return decrypt_client_info_mode(fn, ctx, base, peb, 0);
}

// Which PEB mode produced a heap client_info (-1 = none)
inline int probe_client_info_mode(ReadFn fn, void* ctx, std::uint64_t base, std::uint64_t peb)
{
    for (int mode = 0; mode < 4; ++mode) {
        if (is_heap(decrypt_client_info_mode(fn, ctx, base, peb, mode)))
            return mode;
    }
    return -1;
}


inline std::uint64_t decrypt_client_base(ReadFn fn, void* ctx, std::uint64_t base, std::uint64_t peb, std::uint64_t client_info)
{
    const std::uint64_t mb = base;
    std::uint64_t rax = mb, rbx = mb, rcx = mb, rdx = mb, r8 = mb, r9 = mb, r10 = mb, r11 = mb, r13 = mb, r14 = mb, r15 = mb;
    r8 = rd<std::uint64_t>(fn, ctx, client_info + wz_rva::client_base_enc);
    if(!r8)
        return r8;
    rbx = peb;         //mov rbx, gs:[rax]
    rax = rbx;         //mov rax, rbx
    rax <<= 0x23;         //shl rax, 0x23
    rax = _byteswap_uint64(rax);         //bswap rax
    rax &= 0xF;
    switch(rax) {
    case 0:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D35834]
        rax = base;         //lea rax, [0xFFFFFFFFFC405EDA]
        r8 -= rax;         //sub r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x1E;         //shr rax, 0x1E
        rax ^= r8;         //xor rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x3C;         //shr r8, 0x3C
        r8 ^= rax;         //xor r8, rax
        rax = base;         //lea rax, [0xFFFFFFFFFC405CBE]
        r8 -= rax;         //sub r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x28;         //shr rax, 0x28
        r8 ^= rax;         //xor r8, rax
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        rax = 0x9CC8E0420ADA280D;         //mov rax, 0x9CC8E0420ADA280D
        rax *= r8;         //imul rax, r8
        rax += rbx;         //add rax, rbx
        r8 = rax;         //mov r8, rax
        r8 >>= 0x11;         //shr r8, 0x11
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x22;         //shr rax, 0x22
        r8 ^= rax;         //xor r8, rax
        return r8;
    }
    case 1:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D3539D]
        r15 = base + 0x755F7BDD;         //lea r15, [0x00000000719FD7B8]
        rax = r8;         //mov rax, r8
        rax >>= 0x9;         //shr rax, 0x09
        rax ^= r8;         //xor rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x12;         //shr r8, 0x12
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x24;         //shr rax, 0x24
        r8 ^= rax;         //xor r8, rax
        r8 ^= rbx;         //xor r8, rbx
        rcx = 0;         //and rcx, 0xFFFFFFFFC0000000
        rcx = _rotl64(rcx, 0x10);         //rol rcx, 0x10
        rcx ^= r10;         //xor rcx, r10
        rcx = ~rcx;         //not rcx
        r8 *= rd<std::uint64_t>(fn, ctx, rcx + 0x9);         //imul r8, [rcx+0x09]
        rcx = base;         //lea rcx, [0xFFFFFFFFFC405841]
        rax = rbx;         //mov rax, rbx
        rax -= rcx;         //sub rax, rcx
        rax += 0xFFFFFFFF9F0CFAED;         //add rax, 0xFFFFFFFF9F0CFAED
        r8 += rax;         //add r8, rax
        rax = 0x40ED86BABDEA8F5B;         //mov rax, 0x40ED86BABDEA8F5B
        r8 *= rax;         //imul r8, rax
        rax = 0xA7798517B7F399EA;         //mov rax, 0xA7798517B7F399EA
        r8 ^= rax;         //xor r8, rax
        rax = r15;         //mov rax, r15
        rax = ~rax;         //not rax
        rax ^= rbx;         //xor rax, rbx
        r8 += rax;         //add r8, rax
        rax = 0x459093E765583ADB;         //mov rax, 0x459093E765583ADB
        r8 *= rax;         //imul r8, rax
        return r8;
    }
    case 2:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D34F4F]
        r14 = base + 0xAC81;         //lea r14, [0xFFFFFFFFFC410413]
        rax = 0xE03443781C6DB26D;         //mov rax, 0xE03443781C6DB26D
        r8 *= rax;         //imul r8, rax
        rax = 0x26676A6627BAC50C;         //mov rax, 0x26676A6627BAC50C
        r8 -= rax;         //sub r8, rax
        rax = 0x541ECC7788F37ADE;         //mov rax, 0x541ECC7788F37ADE
        r8 += rax;         //add r8, rax
        r8 += r14;         //add r8, r14
        rax = base + 0x142;         //lea rax, [0xFFFFFFFFFC405492]
        rax = ~rax;         //not rax
        rcx = rbx;         //mov rcx, rbx
        rcx = ~rcx;         //not rcx
        rcx -= rbx;         //sub rcx, rbx
        rcx += rax;         //add rcx, rax
        r8 += rcx;         //add r8, rcx
        rax = r8;         //mov rax, r8
        rax >>= 0x15;         //shr rax, 0x15
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x2A;         //shr rax, 0x2A
        r8 ^= rax;         //xor r8, rax
        rax = base;         //lea rax, [0xFFFFFFFFFC4052F1]
        r8 -= rax;         //sub r8, rax
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        return r8;
    }
    case 3:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D34A72]
        r13 = base + 0xF26D;         //lea r13, [0xFFFFFFFFFC414522]
        r8 ^= rbx;         //xor r8, rbx
        rcx = 0;         //and rcx, 0xFFFFFFFFC0000000
        rcx = _rotl64(rcx, 0x10);         //rol rcx, 0x10
        rcx ^= r10;         //xor rcx, r10
        rcx = ~rcx;         //not rcx
        r8 *= rd<std::uint64_t>(fn, ctx, rcx + 0x9);         //imul r8, [rcx+0x09]
        rax = 0x6C5618A3BE4C414;         //mov rax, 0x6C5618A3BE4C414
        r8 -= rax;         //sub r8, rax
        rax = 0xE98709096AD185CC;         //mov rax, 0xE98709096AD185CC
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0xB;         //shr rax, 0x0B
        rax ^= r8;         //xor rax, r8
        r8 = base + 0x5ED318FB;         //lea r8, [0x000000005B136A8B]
        rcx = rax;         //mov rcx, rax
        r8 = ~r8;         //not r8
        r8 *= rbx;         //imul r8, rbx
        rcx >>= 0x16;         //shr rcx, 0x16
        rcx ^= rax;         //xor rcx, rax
        rax = rcx;         //mov rax, rcx
        rax >>= 0x2C;         //shr rax, 0x2C
        r8 ^= rax;         //xor r8, rax
        r8 ^= rcx;         //xor r8, rcx
        rax = 0x22A1571E2E749CB;         //mov rax, 0x22A1571E2E749CB
        r8 *= rax;         //imul r8, rax
        rax = rbx;         //mov rax, rbx
        rax *= r13;         //imul rax, r13
        r8 += rax;         //add r8, rax
        return r8;
    }
    case 4:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D345F4]
        r15 = base + 0x72D0A311;         //lea r15, [0x000000006F10F143]
        rax = 0x54EE9012A77B3C0E;         //mov rax, 0x54EE9012A77B3C0E
        r8 ^= rax;         //xor r8, rax
        rax = base;         //lea rax, [0xFFFFFFFFFC4049E1]
        rax += 0x432D;         //add rax, 0x432D
        rax += rbx;         //add rax, rbx
        r8 += rax;         //add r8, rax
        rax = 0xBE6A84FFF3304C3D;         //mov rax, 0xBE6A84FFF3304C3D
        r8 *= rax;         //imul r8, rax
        rax = r8;         //mov rax, r8
        r8 >>= 0x12;         //shr r8, 0x12
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x24;         //shr rax, 0x24
        rax ^= r8;         //xor rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x7;         //shr r8, 0x07
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0xE;         //shr rax, 0x0E
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x1C;         //shr rax, 0x1C
        rax ^= r8;         //xor rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x38;         //shr r8, 0x38
        r8 ^= rax;         //xor r8, rax
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        rax = rbx;         //mov rax, rbx
        rax *= r15;         //imul rax, r15
        r8 -= rax;         //sub r8, rax
        rax = 0x598660DAA37ACC99;         //mov rax, 0x598660DAA37ACC99
        r8 ^= rax;         //xor r8, rax
        return r8;
    }
    case 5:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D34159]
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        rax = 0xC088FB236BE68165;         //mov rax, 0xC088FB236BE68165
        r8 *= rax;         //imul r8, rax
        rax = r8;         //mov rax, r8
        r8 >>= 0x5;         //shr r8, 0x05
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0xA;         //shr rax, 0x0A
        rax ^= r8;         //xor rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x14;         //shr r8, 0x14
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x28;         //shr rax, 0x28
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0xB;         //shr rax, 0x0B
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x16;         //shr rax, 0x16
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x2C;         //shr rax, 0x2C
        r8 ^= rax;         //xor r8, rax
        rax = 0xF87FD44152069748;         //mov rax, 0xF87FD44152069748
        r8 ^= rax;         //xor r8, rax
        rax = base;         //lea rax, [0xFFFFFFFFFC404676]
        rax += 0x1079;         //add rax, 0x1079
        rax += rbx;         //add rax, rbx
        r8 ^= rax;         //xor r8, rax
        rcx = base;         //lea rcx, [0xFFFFFFFFFC404857]
        rax = rbx;         //mov rax, rbx
        rax = ~rax;         //not rax
        rax -= rcx;         //sub rax, rcx
        rax += 0xFFFFFFFF968271AB;         //add rax, 0xFFFFFFFF968271AB
        r8 += rax;         //add r8, rax
        return r8;
    }
    case 6:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D33CDB]
        r15 = base + 0x1EE2;         //lea r15, [0xFFFFFFFFFC406400]
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        rcx = 0x30DABF93D6E4FB5;         //mov rcx, 0x30DABF93D6E4FB5
        r8 ^= rcx;         //xor r8, rcx
        rax = rbx;         //mov rax, rbx
        rax ^= r15;         //xor rax, r15
        r8 -= rax;         //sub r8, rax
        rax = 0xDB8B0AAFA542904;         //mov rax, 0xDB8B0AAFA542904
        r8 -= rbx;         //sub r8, rbx
        r8 -= rax;         //sub r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x22;         //shr rax, 0x22
        r8 ^= rax;         //xor r8, rax
        rax = 0xDF170407BBE28DB5;         //mov rax, 0xDF170407BBE28DB5
        r8 *= rax;         //imul r8, rax
        rax = r8;         //mov rax, r8
        r8 >>= 0x8;         //shr r8, 0x08
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x10;         //shr rax, 0x10
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x20;         //shr rax, 0x20
        r8 ^= rax;         //xor r8, rax
        return r8;
    }
    case 7:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D3389A]
        r15 = base + 0xC177;         //lea r15, [0xFFFFFFFFFC410254]
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        rax = 0x1C4A7DE2E2F8F68F;         //mov rax, 0x1C4A7DE2E2F8F68F
        rcx = 0x378CE09B287B2D41;         //mov rcx, 0x378CE09B287B2D41
        rcx ^= r8;         //xor rcx, r8
        rcx += rax;         //add rcx, rax
        r8 = rcx;         //mov r8, rcx
        r8 >>= 0x23;         //shr r8, 0x23
        r8 ^= rcx;         //xor r8, rcx
        rax = rbx + 0x1;         //lea rax, [rbx+0x01]
        rax *= r15;         //imul rax, r15
        rax += rbx;         //add rax, rbx
        r8 += rax;         //add r8, rax
        rax = 0xEBEA9B8B5714671D;         //mov rax, 0xEBEA9B8B5714671D
        r8 *= rax;         //imul r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0xE;         //shr rax, 0x0E
        rax ^= r8;         //xor rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x1C;         //shr r8, 0x1C
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x38;         //shr rax, 0x38
        r8 ^= rax;         //xor r8, rax
        return r8;
    }
    case 8:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D33359]
        r13 = base + 0x553;         //lea r13, [0xFFFFFFFFFC4040EA]
        r15 = base + 0x88B9;         //lea r15, [0xFFFFFFFFFC40C43F]
        rax = base;         //lea rax, [0xFFFFFFFFFC4039B0]
        r8 ^= rax;         //xor r8, rax
        rax = 0x3169FBDB3B875224;         //mov rax, 0x3169FBDB3B875224
        r8 += rax;         //add r8, rax
        rax = r15;         //mov rax, r15
        rax = ~rax;         //not rax
        rax *= rbx;         //imul rax, rbx
        r8 ^= rax;         //xor r8, rax
        r8 ^= rbx;         //xor r8, rbx
        r8 ^= r13;         //xor r8, r13
        rax = r8;         //mov rax, r8
        rax >>= 0x13;         //shr rax, 0x13
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x26;         //shr rax, 0x26
        r8 ^= rax;         //xor r8, rax
        rcx = 0;         //and rcx, 0xFFFFFFFFC0000000
        rcx = _rotl64(rcx, 0x10);         //rol rcx, 0x10
        rax = 0x49665D7F2AFA3F6B;         //mov rax, 0x49665D7F2AFA3F6B
        r8 *= rax;         //imul r8, rax
        rcx ^= r10;         //xor rcx, r10
        rax = base + 0x11D125F7;         //lea rax, [0x000000000E115EE0]
        rax = ~rax;         //not rax
        rcx = ~rcx;         //not rcx
        rax *= rbx;         //imul rax, rbx
        r8 += rax;         //add r8, rax
        r8 *= rd<std::uint64_t>(fn, ctx, rcx + 0x9);         //imul r8, [rcx+0x09]
        return r8;
    }
    case 9:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r9, [0x0000000009D32F1C]
        r11 = base + 0x7C81;         //lea r11, [0xFFFFFFFFFC40B3E0]
        rax = rbx;         //mov rax, rbx
        rax *= r11;         //imul rax, r11
        r8 -= rax;         //sub r8, rax
        rax = base;         //lea rax, [0xFFFFFFFFFC403485]
        r8 -= rax;         //sub r8, rax
        rax = rbx;         //mov rax, rbx
        rax -= base;         //sub rax, [rsp+0x70] -- didn't find trace -> use base
        rax += 0xFFFFFFFFFFFF4D38;         //add rax, 0xFFFFFFFFFFFF4D38
        r8 += rax;         //add r8, rax
        rax = 0xB294869EA09D48AA;         //mov rax, 0xB294869EA09D48AA
        r8 ^= rax;         //xor r8, rax
        rax = 0xDA6A9700AB4D27FD;         //mov rax, 0xDA6A9700AB4D27FD
        r8 *= rax;         //imul r8, rax
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r9;         //xor rax, r9
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        rax = 0x38632CDC13FD78A5;         //mov rax, 0x38632CDC13FD78A5
        r8 += rax;         //add r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x1D;         //shr rax, 0x1D
        rax ^= r8;         //xor rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x3A;         //shr r8, 0x3A
        r8 ^= rax;         //xor r8, rax
        return r8;
    }
    case 10:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D32A01]
        r8 += rbx;         //add r8, rbx
        rax = 0x36164EFD786890C1;         //mov rax, 0x36164EFD786890C1
        r8 *= rax;         //imul r8, rax
        rax = 0x6F993F33D7A49418;         //mov rax, 0x6F993F33D7A49418
        rax += r8;         //add rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x8;         //shr r8, 0x08
        r8 ^= rax;         //xor r8, rax
        rcx = r8;         //mov rcx, r8
        rcx >>= 0x10;         //shr rcx, 0x10
        rcx ^= r8;         //xor rcx, r8
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        r8 = rcx;         //mov r8, rcx
        rax ^= r10;         //xor rax, r10
        r8 >>= 0x20;         //shr r8, 0x20
        r8 ^= rcx;         //xor r8, rcx
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        rax = 0xE88B55E25B8B057C;         //mov rax, 0xE88B55E25B8B057C
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x1A;         //shr rax, 0x1A
        rax ^= r8;         //xor rax, r8
        rcx = rax;         //mov rcx, rax
        rcx >>= 0x34;         //shr rcx, 0x34
        rcx ^= rax;         //xor rcx, rax
        rax = rcx;         //mov rax, rcx
        rax >>= 0x4;         //shr rax, 0x04
        rax ^= rcx;         //xor rax, rcx
        rcx = rax;         //mov rcx, rax
        rcx >>= 0x8;         //shr rcx, 0x08
        rcx ^= rax;         //xor rcx, rax
        r8 = rcx;         //mov r8, rcx
        r8 >>= 0x10;         //shr r8, 0x10
        r8 ^= rcx;         //xor r8, rcx
        rax = r8;         //mov rax, r8
        rax >>= 0x20;         //shr rax, 0x20
        r8 ^= rax;         //xor r8, rax
        return r8;
    }
    case 11:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D325D4]
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        rax = rbx;         //mov rax, rbx
        std::uint64_t RBP_0xFFFFFFFFFFFFFFB8;
        RBP_0xFFFFFFFFFFFFFFB8 = base + 0xA1FD;         //lea rax, [0xFFFFFFFFFC40D020]
        rax *= RBP_0xFFFFFFFFFFFFFFB8;         //imul rax, [rbp-0x48]
        r8 += rax;         //add r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x26;         //shr rax, 0x26
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0xA;         //shr rax, 0x0A
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x14;         //shr rax, 0x14
        rax ^= r8;         //xor rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x28;         //shr r8, 0x28
        r8 ^= rax;         //xor r8, rax
        rax = 0xC6A8E21F37CF3675;         //mov rax, 0xC6A8E21F37CF3675
        r8 *= rax;         //imul r8, rax
        rax = base;         //lea rax, [0xFFFFFFFFFC402916]
        rax += rbx;         //add rax, rbx
        r8 -= rax;         //sub r8, rax
        rax = base;         //lea rax, [0xFFFFFFFFFC402A43]
        r8 ^= rax;         //xor r8, rax
        return r8;
    }
    case 12:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r9, [0x0000000009D3209F]
        rax = 0x5D2901AC55739352;         //mov rax, 0x5D2901AC55739352
        r8 -= rax;         //sub r8, rax
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r9;         //xor rax, r9
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        rax = base;         //lea rax, [0xFFFFFFFFFC402441]
        r8 += rax;         //add r8, rax
        rax = 0x156D71AB28FBFAFF;         //mov rax, 0x156D71AB28FBFAFF
        r8 *= rax;         //imul r8, rax
        rax = r8;         //mov rax, r8
        r8 >>= 0x27;         //shr r8, 0x27
        r8 ^= rax;         //xor r8, rax
        r8 -= rbx;         //sub r8, rbx
        rax = r8;         //mov rax, r8
        rax >>= 0x17;         //shr rax, 0x17
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x2E;         //shr rax, 0x2E
        r8 ^= rax;         //xor r8, rax
        rax = base;         //lea rax, [0xFFFFFFFFFC4024C3]
        r8 ^= rax;         //xor r8, rax
        return r8;
    }
    case 13:
    {
        r11 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r11, [0x0000000009D31B56]
        rax = r8;         //mov rax, r8
        rax >>= 0x1F;         //shr rax, 0x1F
        r8 ^= rax;         //xor r8, rax
        rcx = r8;         //mov rcx, r8
        rax = base;         //lea rax, [0xFFFFFFFFFC401FAF]
        rcx >>= 0x3E;         //shr rcx, 0x3E
        rcx ^= r8;         //xor rcx, r8
        rdx = 0;         //and rdx, 0xFFFFFFFFC0000000
        rdx = _rotl64(rdx, 0x10);         //rol rdx, 0x10
        r8 = rbx;         //mov r8, rbx
        r8 = ~r8;         //not r8
        rdx ^= r11;         //xor rdx, r11
        r8 += rcx;         //add r8, rcx
        rdx = ~rdx;         //not rdx
        r8 -= rax;         //sub r8, rax
        r8 -= 0x6929AFAC;         //sub r8, 0x6929AFAC
        r8 *= rd<std::uint64_t>(fn, ctx, rdx + 0x9);         //imul r8, [rdx+0x09]
        rax = r8;         //mov rax, r8
        rax >>= 0x18;         //shr rax, 0x18
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x30;         //shr rax, 0x30
        r8 ^= rax;         //xor r8, rax
        rax = 0x69651B1AF033619B;         //mov rax, 0x69651B1AF033619B
        r8 += rbx;         //add r8, rbx
        r8 *= rax;         //imul r8, rax
        rax = 0x29BBD1B30DFD9417;         //mov rax, 0x29BBD1B30DFD9417
        r8 *= rax;         //imul r8, rax
        rax = 0xA7B8F15C4FABBB6C;         //mov rax, 0xA7B8F15C4FABBB6C
        r8 ^= rax;         //xor r8, rax
        return r8;
    }
    case 14:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D316DA]
        r8 += rbx;         //add r8, rbx
        rax = base + 0x8D0;         //lea rax, [0xFFFFFFFFFC402661]
        rax -= rbx;         //sub rax, rbx
        r8 += rax;         //add r8, rax
        rax = 0xBC0AAA7E98B1663A;         //mov rax, 0xBC0AAA7E98B1663A
        r8 ^= rax;         //xor r8, rax
        rax = 0x54D1F9305B205B45;         //mov rax, 0x54D1F9305B205B45
        r8 *= rax;         //imul r8, rax
        rcx = r8;         //mov rcx, r8
        rcx >>= 0xA;         //shr rcx, 0x0A
        rcx ^= r8;         //xor rcx, r8
        rax = rcx;         //mov rax, rcx
        rax >>= 0x14;         //shr rax, 0x14
        rax ^= rcx;         //xor rax, rcx
        r8 = rax;         //mov r8, rax
        r8 >>= 0x28;         //shr r8, 0x28
        r8 ^= rax;         //xor r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x12;         //shr rax, 0x12
        rax ^= r8;         //xor rax, r8
        r8 = rax;         //mov r8, rax
        r8 >>= 0x24;         //shr r8, 0x24
        r8 ^= rax;         //xor r8, rax
        rax = 0xFFFFFFFFDE23E20A;         //mov rax, 0xFFFFFFFFDE23E20A
        rax -= rbx;         //sub rax, rbx
        rax -= base;         //sub rax, [rsp+0x70] -- didn't find trace -> use base
        r8 += rax;         //add r8, rax
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        r8 *= rd<std::uint64_t>(fn, ctx, rax + 0x9);         //imul r8, [rax+0x09]
        return r8;
    }
    case 15:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::client_base_key);         //mov r10, [0x0000000009D31256]
        r15 = base + 0x76BB;         //lea r15, [0xFFFFFFFFFC409154]
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        rax = rd<std::uint64_t>(fn, ctx, rax + 0x9);         //mov rax, [rax+0x09]
        std::uint64_t RBP_0xFFFFFFFFFFFFFFB0;
        RBP_0xFFFFFFFFFFFFFFB0 = 0x3A27415DA31CA989;         //mov rax, 0x3A27415DA31CA989
        rax *= RBP_0xFFFFFFFFFFFFFFB0;         //imul rax, [rbp-0x50]
        r8 *= rax;         //imul r8, rax
        rax = 0x6F6A3BE0CADE4A54;         //mov rax, 0x6F6A3BE0CADE4A54
        r8 -= rax;         //sub r8, rax
        r8 -= rbx;         //sub r8, rbx
        rcx = r8;         //mov rcx, r8
        rcx >>= 0x13;         //shr rcx, 0x13
        rcx ^= r8;         //xor rcx, r8
        r8 = rbx;         //mov r8, rbx
        r8 = ~r8;         //not r8
        rax = r15;         //mov rax, r15
        rax = ~rax;         //not rax
        r8 *= rax;         //imul r8, rax
        rax = rcx;         //mov rax, rcx
        rax >>= 0x26;         //shr rax, 0x26
        rax ^= rcx;         //xor rax, rcx
        r8 += rax;         //add r8, rax
        rax = r8;         //mov rax, r8
        rax >>= 0x28;         //shr rax, 0x28
        r8 ^= rax;         //xor r8, rax
        rax = 0x3224CE0A9BEB6A6E;         //mov rax, 0x3224CE0A9BEB6A6E
        r8 -= rax;         //sub r8, rax
        return r8;
    }
    }
    return 0;
}

inline std::uint64_t decrypt_bone_base(ReadFn fn, void* ctx, std::uint64_t base, std::uint64_t peb)
{
    const std::uint64_t mb = base;
    std::uint64_t rax = mb, rcx = mb, rdx = mb, r8 = mb, r9 = mb, r10 = mb, r11 = mb, r12 = mb, r13 = mb, r15 = mb;
    rdx = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_enc);
    if(!rdx)
        return rdx;
    r8 = peb;         //mov r8, gs:[rax]
    rax = r8;         //mov rax, r8
    rax >>= 0x13;         //shr rax, 0x13
    rax &= 0xF;
    switch(rax) {
    case 0:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);         //mov r9, [0x000000000638DE60]
        rax = base + 0x8C93;         //lea rax, [0xFFFFFFFFF8A671DC]
        rax -= r8;         //sub rax, r8
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x15;         //shr rax, 0x15
        rax ^= rdx;         //xor rax, rdx
        rdx = rax;         //mov rdx, rax
        rdx >>= 0x2A;         //shr rdx, 0x2A
        rdx ^= rax;         //xor rdx, rax
        rdx += r8;         //add rdx, r8
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r9;         //xor rax, r9
        rax = ~rax;         //not rax
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);         //imul rdx, [rax+0x17]
        rax = base;         //lea rax, [0xFFFFFFFFF8A5E22C]
        rdx ^= rax;         //xor rdx, rax
        rax = 0x860534C8C01FEA7B;         //mov rax, 0x860534C8C01FEA7B
        rdx *= rax;         //imul rdx, rax
        rax = 0xEE334BF3EC572D68;         //mov rax, 0xEE334BF3EC572D68
        rdx ^= rax;         //xor rdx, rax
        return rdx;
    }
    case 1:
    {
        // Steam 0x6A6D4AEF — previous body was a stub (return rdx). Live PEB hits this case.
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);
        r15 = base + 0xDF5D;
        rax = 0;
        rax = _rotl64(rax, 0x10);
        rax ^= r10;
        rax = ~rax;
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);
        rax = 0x8A4B98169395E686ull;
        rdx ^= rax;
        rax = 0xC3957EB9F84EC5AFull;
        rdx *= rax;
        rax = rdx;
        rax >>= 0xE;
        rax ^= rdx;
        rdx = rax;
        rdx >>= 0x1C;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x38;
        rdx ^= rax;
        rax = base + 0x31CB;
        rax -= r8;
        rdx += rax;
        rax = rdx;
        rax >>= 0xD;
        rax ^= rdx;
        rcx = rax;
        rcx >>= 0x1A;
        rcx ^= rax;
        rdx = rcx;
        rdx >>= 0x34;
        rdx ^= rcx;
        rax = r15;
        rax = ~rax;
        rdx ^= rax;
        rdx ^= r8;
        return rdx;
    }
    case 2:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);         //mov r9, [0x000000000638D526]
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r9;         //xor rax, r9
        rax = ~rax;         //not rax
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);         //imul rdx, [rax+0x17]
        rax = base + 0x47C2AE1B;         //lea rax, [0x00000000406889E9]
        rax = ~rax;         //not rax
        rax ^= r8;         //xor rax, r8
        rax += r8;         //add rax, r8
        rdx += rax;         //add rdx, rax
        rax = 0x94073D91C803188D;         //mov rax, 0x94073D91C803188D
        rdx ^= rax;         //xor rdx, rax
        rax = 0x2EEA8A0831CE333B;         //mov rax, 0x2EEA8A0831CE333B
        rdx *= rax;         //imul rdx, rax
        rdx += r8;         //add rdx, r8
        rax = rdx;         //mov rax, rdx
        rax >>= 0x13;         //shr rax, 0x13
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x26;         //shr rax, 0x26
        rdx ^= rax;         //xor rdx, rax
        rax = 0xD4E2CCE5B7959CA0;         //mov rax, 0xD4E2CCE5B7959CA0
        rdx ^= rax;         //xor rdx, rax
        return rdx;
    }
    case 3:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);
        r12 = base + 0x114B;
        rax = rdx;
        rax >>= 0x13;
        rax ^= rdx;
        rdx = rax;
        rdx >>= 0x26;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x24;
        rdx ^= rax;
        rax = 0x764F15DD269101D3ull;
        rdx *= rax;
        rax = 0x34E81942B113C230ull;
        rdx -= rax;
        rax = 0x13805FC46F4FC36Aull;
        rdx += rax;
        rax = r8;
        rax -= base;
        rax += 0xFFFFFFFFFFFF85F3ull;
        rdx += rax;
        rax = 0;
        rax = _rotl64(rax, 0x10);
        rax ^= r9;
        rax = ~rax;
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);
        rdx ^= r12;
        rdx ^= r8;
        return rdx;
    }
    case 4:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);
        rax = rdx;
        rax >>= 0x11;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x22;
        rdx ^= rax;
        rax = 0x2CFB6FB2F3BAD3Cull;
        rdx -= rax;
        rax = 0;
        rax = _rotl64(rax, 0x10);
        rax ^= r9;
        rax = ~rax;
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);
        rax = 0xEED0F28134CE8447ull;
        rdx *= rax;
        rax = 0x52D4170A67BFFCB2ull;
        rdx ^= rax;
        rax = rdx + r8 * 1;
        rdx = rax;
        rdx >>= 0x16;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x2C;
        rdx ^= rax;
        rdx ^= r8;
        return rdx;
    }
    case 5:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);
        r15 = base + 0x19B7DBCB;
        r12 = base + 0x654BDD13;
        rax = r12;
        rax = ~rax;
        ++rax;
        rdx += rax;
        rax = 0x4A2AFA53025C5181ull;
        rdx += r8;
        rdx *= rax;
        rax = rdx;
        rax >>= 0x28;
        rdx ^= rax;
        rax = r8 + r15 * 1;
        rcx = base + 0xA045;
        rcx += r8;
        rcx ^= rax;
        rdx ^= rcx;
        rax = 0x574A3A5B7408079Bull;
        rdx *= rax;
        rax = 0;
        rax = _rotl64(rax, 0x10);
        rax ^= r10;
        rax = ~rax;
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);
        return rdx;
    }
    case 6:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);         //mov r9, [0x000000000638C315]
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r9;         //xor rax, r9
        rax = ~rax;         //not rax
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);         //imul rdx, [rax+0x17]
        rdx += r8;         //add rdx, r8
        rcx = base + 0x4951;         //lea rcx, [0xFFFFFFFFF8A610AE]
        rax = r8;         //mov rax, r8
        rax *= rcx;         //imul rax, rcx
        rdx += rax;         //add rdx, rax
        rax = r8 + rdx * 1;         //lea rax, [r8+rdx*1]
        r10 = base;         //lea r10, [0xFFFFFFFFF8A5C8BA]
        rcx = r10 + 0x1d37b933;         //lea rcx, [r10+0x1D37B933]
        rcx += rax;         //add rcx, rax
        rdx = rcx;         //mov rdx, rcx
        rdx >>= 0x9;         //shr rdx, 0x09
        rdx ^= rcx;         //xor rdx, rcx
        rax = rdx;         //mov rax, rdx
        rax >>= 0x12;         //shr rax, 0x12
        rax ^= rdx;         //xor rax, rdx
        rcx = rax;         //mov rcx, rax
        rcx >>= 0x24;         //shr rcx, 0x24
        rcx ^= rax;         //xor rcx, rax
        rax = 0x6C2A29044A40E4C7;         //mov rax, 0x6C2A29044A40E4C7
        rcx *= rax;         //imul rcx, rax
        rcx ^= r10;         //xor rcx, r10
        rax = rcx;         //mov rax, rcx
        rax >>= 0x3;         //shr rax, 0x03
        rax ^= rcx;         //xor rax, rcx
        rcx = rax;         //mov rcx, rax
        rcx >>= 0x6;         //shr rcx, 0x06
        rcx ^= rax;         //xor rcx, rax
        rax = rcx;         //mov rax, rcx
        rax >>= 0xC;         //shr rax, 0x0C
        rax ^= rcx;         //xor rax, rcx
        rdx = rax;         //mov rdx, rax
        rdx >>= 0x18;         //shr rdx, 0x18
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x30;         //shr rax, 0x30
        rdx ^= rax;         //xor rdx, rax
        return rdx;
    }
    case 7:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);         //mov r9, [0x000000000638BF07]
        rax = base;         //lea rax, [0xFFFFFFFFF8A5C51C]
        rdx += rax;         //add rdx, rax
        rax = 0x5F80490A38DB3901;         //mov rax, 0x5F80490A38DB3901
        rdx ^= rax;         //xor rdx, rax
        rax = 0x4EC9DC6A5902297D;         //mov rax, 0x4EC9DC6A5902297D
        rdx -= rax;         //sub rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x25;         //shr rax, 0x25
        rdx ^= rax;         //xor rdx, rax
        rcx = base + 0xCEFB;         //lea rcx, [0xFFFFFFFFF8A693A6]
        rax = r8;         //mov rax, r8
        rax ^= rcx;         //xor rax, rcx
        rdx += rax;         //add rdx, rax
        rax = 0x92B34BC27C367071;         //mov rax, 0x92B34BC27C367071
        rdx *= rax;         //imul rdx, rax
        rdx -= r8;         //sub rdx, r8
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r9;         //xor rax, r9
        rax = ~rax;         //not rax
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);         //imul rdx, [rax+0x17]
        return rdx;
    }
    case 8:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);
        r13 = base + 0x5723;
        r12 = base + 0xFAB2;
        rax = 0xE62DA6375F493113ull;
        rdx *= rax;
        rax = base;
        rdx -= rax;
        rax = 0;
        rax = _rotl64(rax, 0x10);
        rax ^= r10;
        rax = ~rax;
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);
        rdx -= r8;
        rax = rdx;
        rax >>= 0xF;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x1E;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x3C;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x3;
        rax ^= rdx;
        rdx = rax;
        rdx >>= 0x6;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0xC;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x18;
        rax ^= rdx;
        rdx = rax;
        rdx >>= 0x30;
        rdx ^= rax;
        rax = r8 + r12 * 1;
        rdx ^= rax;
        rdx ^= r13;
        rdx ^= r8;
        return rdx;
    }
    case 9:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);         //mov r10, [0x000000000638B5AA]
        r11 = base + 0x429D;         //lea r11, [0xFFFFFFFFF8A5FFA3]
        r9 = base + 0xF1EC;         //lea r9, [0xFFFFFFFFF8A6AEEB]
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r10;         //xor rax, r10
        rax = ~rax;         //not rax
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);         //imul rdx, [rax+0x17]
        rax = r8 + r9 * 1;         //lea rax, [r8+r9*1]
        rcx = r8 + r11 * 1;         //lea rcx, [r8+r11*1]
        rcx += rdx;         //add rcx, rdx
        rcx ^= rax;         //xor rcx, rax
        rax = base + 0x5304B0E6;         //lea rax, [0x000000004BAA6D41]
        rcx ^= rax;         //xor rcx, rax
        rcx ^= r8;         //xor rcx, r8
        rdx = rcx;         //mov rdx, rcx
        rdx >>= 0x22;         //shr rdx, 0x22
        rdx ^= rcx;         //xor rdx, rcx
        rax = 0xEE899EDDAF56550;         //mov rax, 0xEE899EDDAF56550
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0xE;         //shr rax, 0x0E
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x1C;         //shr rax, 0x1C
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x38;         //shr rax, 0x38
        rdx ^= rax;         //xor rdx, rax
        rax = 0x39D515C223A57391;         //mov rax, 0x39D515C223A57391
        rdx *= rax;         //imul rdx, rax
        return rdx;
    }
    case 10:
    {
        rax = rdx;         //mov rax, rdx
        rax >>= 0xF;         //shr rax, 0x0F
        rax ^= rdx;         //xor rax, rdx
        rdx = rax;         //mov rdx, rax
        rdx >>= 0x1E;         //shr rdx, 0x1E
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x3C;         //shr rax, 0x3C
        rax ^= rdx;         //xor rax, rdx
        rdx = rax;         //mov rdx, rax
        rdx >>= 0x13;         //shr rdx, 0x13
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x26;         //shr rax, 0x26
        rdx ^= rax;         //xor rdx, rax
        return rdx;
    }
    case 11:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);         //mov r9, [0x000000000638ACBD]
        rax = rdx;         //mov rax, rdx
        rax >>= 0x22;         //shr rax, 0x22
        rdx ^= rax;         //xor rdx, rax
        rax = base + 0x67B591A2;         //lea rax, [0x00000000605B437B]
        rax = ~rax;         //not rax
        rax ^= r8;         //xor rax, r8
        rax += r8;         //add rax, r8
        rdx -= rax;         //sub rdx, rax
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r9;         //xor rax, r9
        rax = ~rax;         //not rax
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);         //imul rdx, [rax+0x17]
        rdx ^= r8;         //xor rdx, r8
        rax = 0x112AEF7CBA9BEDF1;         //mov rax, 0x112AEF7CBA9BEDF1
        rdx *= rax;         //imul rdx, rax
        rax = 0x792205E77EAA6797;         //mov rax, 0x792205E77EAA6797
        rdx ^= rax;         //xor rdx, rax
        return rdx;
    }
    case 12:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);         //mov r9, [0x000000000638A90C]
        rax = 0;         //and rax, 0xFFFFFFFFC0000000
        rax = _rotl64(rax, 0x10);         //rol rax, 0x10
        rax ^= r9;         //xor rax, r9
        rax = ~rax;         //not rax
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);         //imul rdx, [rax+0x17]
        rax = 0x33BF00DD8A073650;         //mov rax, 0x33BF00DD8A073650
        rdx -= rax;         //sub rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0xA;         //shr rax, 0x0A
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x14;         //shr rax, 0x14
        rax ^= rdx;         //xor rax, rdx
        rdx = rax;         //mov rdx, rax
        rdx >>= 0x28;         //shr rdx, 0x28
        rdx ^= rax;         //xor rdx, rax
        rax = base;         //lea rax, [0xFFFFFFFFF8A5AE09]
        rdx ^= rax;         //xor rdx, rax
        rax = base + 0x70E4B3E1;         //lea rax, [0x00000000698A60D2]
        rax = ~rax;         //not rax
        rdx += rax;         //add rdx, rax
        rax = 0x37300D9E69A77B2F;         //mov rax, 0x37300D9E69A77B2F
        rdx *= rax;         //imul rdx, rax
        rdx -= r8;         //sub rdx, r8
        return rdx;
    }
    case 13:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);         //mov r9, [0x000000000638A53C]
        rax = base + 0x666C9DA0;         //lea rax, [0x000000005F1248CC]
        rax ^= r8;         //xor rax, r8
        rdx -= rax;         //sub rdx, rax
        rax = 0x124569EA4125D98;         //mov rax, 0x124569EA4125D98
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x5;         //shr rax, 0x05
        rax ^= rdx;         //xor rax, rdx
        rcx = rax;         //mov rcx, rax
        rcx >>= 0xA;         //shr rcx, 0x0A
        rcx ^= rax;         //xor rcx, rax
        rdx = rcx;         //mov rdx, rcx
        rdx >>= 0x14;         //shr rdx, 0x14
        rdx ^= rcx;         //xor rdx, rcx
        rax = rdx;         //mov rax, rdx
        rax >>= 0x28;         //shr rax, 0x28
        rax ^= rdx;         //xor rax, rdx
        rdx = rax;         //mov rdx, rax
        rdx >>= 0x1A;         //shr rdx, 0x1A
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rcx = 0;         //and rcx, 0xFFFFFFFFC0000000
        rax >>= 0x34;         //shr rax, 0x34
        rcx = _rotl64(rcx, 0x10);         //rol rcx, 0x10
        rdx ^= rax;         //xor rdx, rax
        rcx ^= r9;         //xor rcx, r9
        rcx = ~rcx;         //not rcx
        rdx *= rd<std::uint64_t>(fn, ctx, rcx + 0x17);         //imul rdx, [rcx+0x17]
        rdx ^= r8;         //xor rdx, r8
        rax = 0xD83F30F92C64DF4F;         //mov rax, 0xD83F30F92C64DF4F
        rdx ^= rax;         //xor rdx, rax
        rax = 0xB69AFD2628432A9D;         //mov rax, 0xB69AFD2628432A9D
        rdx *= rax;         //imul rdx, rax
        return rdx;
    }
    case 14:
    {
        r9 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);         //mov r9, [0x000000000638A10A]
        r10 = base + 0xD03A;         //lea r10, [0xFFFFFFFFF8A678A0]
        rax = rdx;         //mov rax, rdx
        rax >>= 0x1B;         //shr rax, 0x1B
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rcx = 0;         //and rcx, 0xFFFFFFFFC0000000
        rax >>= 0x36;         //shr rax, 0x36
        rcx = _rotl64(rcx, 0x10);         //rol rcx, 0x10
        rdx ^= rax;         //xor rdx, rax
        rcx ^= r9;         //xor rcx, r9
        rcx = ~rcx;         //not rcx
        rdx *= rd<std::uint64_t>(fn, ctx, rcx + 0x17);         //imul rdx, [rcx+0x17]
        rax = 0xDC4274449EFE767B;         //mov rax, 0xDC4274449EFE767B
        rdx ^= rax;         //xor rdx, rax
        rax = rdx;         //mov rax, rdx
        rax >>= 0x6;         //shr rax, 0x06
        rax ^= rdx;         //xor rax, rdx
        rcx = rax;         //mov rcx, rax
        rcx >>= 0xC;         //shr rcx, 0x0C
        rcx ^= rax;         //xor rcx, rax
        rdx = rcx;         //mov rdx, rcx
        rdx >>= 0x18;         //shr rdx, 0x18
        rdx ^= rcx;         //xor rdx, rcx
        rax = rdx;         //mov rax, rdx
        rax >>= 0x30;         //shr rax, 0x30
        rdx ^= rax;         //xor rdx, rax
        rax = base + 0x5113;         //lea rax, [0xFFFFFFFFF8A5F6BD]
        rax ^= r8;         //xor rax, r8
        rdx -= rax;         //sub rdx, rax
        rax = 0x4480AA60A21867F9;         //mov rax, 0x4480AA60A21867F9
        rdx *= rax;         //imul rdx, rax
        rax = r8 + r10 * 1;         //lea rax, [r8+r10*1]
        rdx += rax;         //add rdx, rax
        return rdx;
    }
    case 15:
    {
        r10 = rd<std::uint64_t>(fn, ctx, base + wz_rva::bone_key);
        r13 = base + 0x642A39AC;
        r12 = base + 0x6744783A;
        rdx += r8;
        rax = r8;
        rax = ~rax;
        rax ^= r13;
        rdx -= rax;
        rdx ^= r12;
        rdx ^= r8;
        rax = 0;
        rax = _rotl64(rax, 0x10);
        rax ^= r10;
        rax = ~rax;
        rdx *= rd<std::uint64_t>(fn, ctx, rax + 0x17);
        rax = 0x54750E0E4638841Aull;
        rdx += rax;
        rax = 0x17257FE07A931EB4ull;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x4;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x8;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x10;
        rdx ^= rax;
        rax = rdx;
        rax >>= 0x20;
        rdx ^= rax;
        rax = 0x7493CCED6314B08Bull;
        rdx *= rax;
        return rdx;
    }
    }
    return 0;
}

inline std::uint16_t get_bone_index(ReadFn fn, void* ctx, std::uint64_t base, std::uint32_t bone_index)
{
    const std::uint64_t mb = base;
    std::uint64_t rax = mb, rbx = mb, rcx = mb, rdx = mb, r8 = mb, r9 = mb, r10 = mb, r11 = mb, r14 = mb;
    rbx = bone_index;
    rcx = rbx * 0x13C8;
    rax = 0xCB182C584BD5193;         //mov rax, 0xCB182C584BD5193
    r11 = base;         //lea r11, [0xFFFFFFFFFC4257C1]
    rax = _umul128(rax, rcx, (std::uint64_t*)&rdx);         //mul rcx
    rax = rcx;         //mov rax, rcx
    r10 = 0xD6FB75C08B670E5B;         //mov r10, 0xD6FB75C08B670E5B
    rax -= rdx;         //sub rax, rdx
    rax >>= 0x1;         //shr rax, 0x01
    rax += rdx;         //add rax, rdx
    rax >>= 0xC;         //shr rax, 0x0C
    rax = rax * 0x1E7D;         //imul rax, rax, 0x1E7D
    rcx -= rax;         //sub rcx, rax
    rax = 0x4078E2A8FCDA18EF;         //mov rax, 0x4078E2A8FCDA18EF
    r8 = rcx * 0x1E7D;         //imul r8, rcx, 0x1E7D
    rax = _umul128(rax, r8, (std::uint64_t*)&rdx);         //mul r8
    rdx >>= 0xB;         //shr rdx, 0x0B
    rax = rdx * 0x1FC4;         //imul rax, rdx, 0x1FC4
    r8 -= rax;         //sub r8, rax
    rax = 0xF0F0F0F0F0F0F0F1;         //mov rax, 0xF0F0F0F0F0F0F0F1
    rax = _umul128(rax, r8, (std::uint64_t*)&rdx);         //mul r8
    rax = 0x624DD2F1A9FBE77;         //mov rax, 0x624DD2F1A9FBE77
    rdx >>= 0x6;         //shr rdx, 0x06
    rcx = rdx * 0x44;         //imul rcx, rdx, 0x44
    rax = _umul128(rax, r8, (std::uint64_t*)&rdx);         //mul r8
    rax = r8;         //mov rax, r8
    rax -= rdx;         //sub rax, rdx
    rax >>= 0x1;         //shr rax, 0x01
    rax += rdx;         //add rax, rdx
    rax >>= 0x6;         //shr rax, 0x06
    rcx += rax;         //add rcx, rax
    rax = rcx * 0xFA;         //imul rax, rcx, 0xFA
    rcx = r8 * 0xFC;         //imul rcx, r8, 0xFC
    rcx -= rax;         //sub rcx, rax
    rax = rd<std::uint16_t>(fn, ctx, rcx + r11 * 1 + 0xC51BD00);         //movzx eax, word ptr [rcx+r11*1+0xC51BD00]
    r8 = rax * 0x13C8;         //imul r8, rax, 0x13C8
    rax = r10;         //mov rax, r10
    rax = _umul128(rax, r8, (std::uint64_t*)&rdx);         //mul r8
    rax = r10;         //mov rax, r10
    rdx >>= 0xD;         //shr rdx, 0x0D
    rcx = rdx * 0x261B;         //imul rcx, rdx, 0x261B
    r8 -= rcx;         //sub r8, rcx
    r9 = r8 * 0x2F75;         //imul r9, r8, 0x2F75
    rax = _umul128(rax, r9, (std::uint64_t*)&rdx);         //mul r9
    rdx >>= 0xD;         //shr rdx, 0x0D
    rax = rdx * 0x261B;         //imul rax, rdx, 0x261B
    r9 -= rax;         //sub r9, rax
    rax = 0x8FB823EE08FB823F;         //mov rax, 0x8FB823EE08FB823F
    rax = _umul128(rax, r9, (std::uint64_t*)&rdx);         //mul r9
    rax = 0x579D6EE340579D6F;         //mov rax, 0x579D6EE340579D6F
    rdx >>= 0x5;         //shr rdx, 0x05
    rcx = rdx * 0x39;         //imul rcx, rdx, 0x39
    rax = _umul128(rax, r9, (std::uint64_t*)&rdx);         //mul r9
    rdx >>= 0x6;         //shr rdx, 0x06
    rcx += rdx;         //add rcx, rdx
    rax = rcx * 0x176;         //imul rax, rcx, 0x176
    rcx = r9 * 0x178;         //imul rcx, r9, 0x178
    rcx -= rax;         //sub rcx, rax
    r14 = rd<std::uint16_t>(fn, ctx, rcx + r11 * 1 + 0xC51FF70);         //movsx r14d, word ptr [rcx+r11*1+0xC51FF70]
    return static_cast<std::uint16_t>(r14);
}

// Camera origin: same Key3-style per-float xor as refdef (Usermode DecryptCameraOrigin).
// Fallback in callers is the raw float3 at camera_pos (+0x1F4).
inline Vec3 decrypt_camera_origin(ReadFn fn, void* ctx, std::uint64_t camera_base)
{
    Vec3 origin{};
    if (!fn || !camera_base) return origin;
    std::uint32_t enc[3]{};
    if (!fn(ctx, camera_base + wz_rva::camera_pos_enc, enc, sizeof(enc)))
        return origin;
    const std::uint32_t key = rd<std::uint32_t>(fn, ctx, camera_base + wz_rva::camera_pos_key);
    float* c = &origin.x;
    for (int j = 0; j < 3; ++j) {
        const std::uint32_t t = static_cast<std::uint32_t>(
            camera_base + wz_rva::camera_pos_enc + 4u * static_cast<std::uint32_t>(j)) ^ key;
        const std::uint32_t d = enc[j] ^ (t * (t + 2u));
        std::memcpy(&c[j], &d, sizeof(float));
    }
    return origin;
}

uintptr_t ResolvePebAddress() {
    auto info = mem.GetProcessInformation();
    if (info.win.vaPEB && IsCanonicalUserPtr((uintptr_t)info.win.vaPEB))
        return (uintptr_t)info.win.vaPEB;
    return 0;
}

bool RunDecrypt(uintptr_t module_base, DecryptState& out) {
    out = {};
    if (!module_base) {
        std::snprintf(out.detail, sizeof(out.detail), "CLIENT_INFO reason=CONFIG_MISSING (no module_base)");
        return false;
    }

    SyncRvaFromOffsets();
    out.peb = ResolvePebAddress();
    if (!out.peb) {
        std::snprintf(out.detail, sizeof(out.detail), "CLIENT_INFO reason=PEB_MISSING");
        // still try name_array below
    }

    ReadFn fn = &DmaReadFn;
    void* ctx = nullptr;

    // ---- name_array always (candidates / names) ----
    const uintptr_t na_rva = offsets.name_array ? offsets.name_array : 0xED1C710;
    uintptr_t na = 0;
    if (ReadU64(module_base + na_rva, na) && IsCanonicalUserPtr(na) && PointerReadable(na)) {
        out.name_array_base = na;
        out.name_array_ok = true;
    }

    // ---- client_info PEB-mixed (mode 0 = ~peb is correct for this build) ----
    if (out.peb) {
        uintptr_t raw_enc = 0;
        const bool raw_read_ok = ReadU64(module_base + (uintptr_t)wz_rva::client_info_enc, raw_enc);

        // raw==0 is the most common post-patch / lobby signal: either the encrypted
        // pointer is not populated yet (menu/lobby) or client_info_enc RVA moved.
        if (!raw_read_ok) {
            LogCi(module_base, 0, 0, -1, "READ_FAIL");
#if defined(OMNIGHOST_VERBOSE_OFFSET_DIAGNOSTICS)
            std::snprintf(out.detail, sizeof(out.detail),
                "CLIENT_INFO FAIL reason=READ_FAIL rva=0x%llX (DMA nao leu o enc)",
                (unsigned long long)wz_rva::client_info_enc);
#else
            std::snprintf(out.detail, sizeof(out.detail),
                "CLIENT_INFO FAIL reason=READ_FAIL (DMA nao leu o enc)");
#endif
        } else if (raw_enc == 0) {
            LogCi(module_base, 0, 0, -1, "NULL_ENC");
#if defined(OMNIGHOST_VERBOSE_OFFSET_DIAGNOSTICS)
            std::snprintf(out.detail, sizeof(out.detail),
                "CLIENT_INFO FAIL reason=NULL_ENC rva=0x%llX (lobby OU offsets desatualizados apos patch)",
                (unsigned long long)wz_rva::client_info_enc);
#else
            std::snprintf(out.detail, sizeof(out.detail),
                "CLIENT_INFO FAIL reason=NULL_ENC (lobby OU offsets desatualizados apos patch)");
#endif
        } else {
            const int mode = probe_client_info_mode(fn, ctx, module_base, out.peb);
            out.client_info_mode = mode;
            uintptr_t ci = 0;
            if (mode >= 0)
                ci = decrypt_client_info_mode(fn, ctx, module_base, out.peb, mode);
            else
                ci = decrypt_client_info(fn, ctx, module_base, out.peb);

            if (is_heap(ci) && PointerReadable(ci)) {
                out.client_info = (uintptr_t)ci;
                out.client_info_ok = true;
                LogCi(module_base, raw_enc, ci, mode, "DECRYPT_OK");
                std::snprintf(out.detail, sizeof(out.detail),
                    "CLIENT_INFO READY ci=0x%llX mode=%d peb=0x%llX",
                    (unsigned long long)ci, mode, (unsigned long long)out.peb);
            } else {
                LogCi(module_base, raw_enc, ci, mode, mode < 0 ? "ALL_MODES_FAIL" : "NOT_READABLE");
                std::snprintf(out.detail, sizeof(out.detail),
                    "CLIENT_INFO FAIL reason=DECRYPT mode=%d raw=0x%llX result=0x%llX peb=0x%llX",
                    mode, (unsigned long long)raw_enc, (unsigned long long)ci, (unsigned long long)out.peb);
            }
        }
    } else {
        std::snprintf(out.detail, sizeof(out.detail), "CLIENT_INFO FAIL reason=PEB_MISSING");
    }

    // ---- client_base (only after client_info) ----
    if (out.client_info_ok) {
        // PEB switch selector (same formula as decrypt body)
        std::uint64_t sw = out.peb;
        sw <<= 0x23;
        sw = _byteswap_uint64(sw);
        sw &= 0xF;

        const uintptr_t cb = (uintptr_t)decrypt_client_base(fn, ctx, module_base, out.peb, out.client_info);
        if (is_heap(cb) && PointerReadable(cb)) {
            out.client_base = cb;
            out.client_base_ok = true;
            LogCb(out.client_info, cb, (int)sw, "DECRYPT_OK");
            std::snprintf(out.detail, sizeof(out.detail),
                "CLIENT_BASE READY cb=0x%llX ci=0x%llX sw=%d",
                (unsigned long long)cb, (unsigned long long)out.client_info, (int)sw);
        } else {
            LogCb(out.client_info, cb, (int)sw, is_heap(cb) ? "NOT_READABLE" : "NOT_HEAP");
            std::snprintf(out.detail, sizeof(out.detail),
                "CLIENT_BASE FAIL result=0x%llX sw=%d ci=0x%llX",
                (unsigned long long)cb, (int)sw, (unsigned long long)out.client_info);
        }
    }

    // ---- local_index from client_info ----
    if (out.client_info_ok) {
        const uintptr_t li_off = offsets.local_index_off ? offsets.local_index_off : 0xB03F0;
        const uintptr_t li_pos = offsets.local_index_pos ? offsets.local_index_pos : 0x418;
        // Two common layouts: direct int at client_info+off, or ptr then +pos
        int idx = -1;
        uintptr_t maybe_ptr = 0;
        if (ReadU64(out.client_info + li_off, maybe_ptr) && IsCanonicalUserPtr(maybe_ptr)) {
            int v = 0;
            if (mem.Read(maybe_ptr + li_pos, &v, sizeof(v)) && v >= 0 && v < 200)
                idx = v;
        }
        if (idx < 0) {
            int v = 0;
            if (mem.Read(out.client_info + li_off, &v, sizeof(v)) && v >= 0 && v < 200)
                idx = v;
        }
        out.local_index = idx;
    }

    // ---- optional bone_base ----
    if (out.peb) {
        const uintptr_t bb = (uintptr_t)decrypt_bone_base(fn, ctx, module_base, out.peb);
        if (is_heap(bb) && PointerReadable(bb)) {
            out.bone_base = bb;
            out.bone_ok = true;
        }
    }

    if (out.client_base_ok) return true;
    if (out.name_array_ok) return true; // candidates only
    return false;
}

bool ReadPlayerSlot(const DecryptState& st, uintptr_t /*module_base*/, int index,
                    float out_pos[3], float out_head[3], float out_chest[3],
                    int& out_team, float& out_health, char* out_name, size_t name_cap,
                    bool& out_valid, bool& out_alive) {
    out_pos[0] = out_pos[1] = out_pos[2] = 0.f;
    out_head[0] = out_head[1] = out_head[2] = 0.f;
    out_chest[0] = out_chest[1] = out_chest[2] = 0.f;
    out_team = 0;
    out_health = 0.f;
    out_valid = false;
    out_alive = false;
    if (out_name && name_cap) out_name[0] = '\0';
    if (index < 0 || index > 155) return false;
    if (!st.client_base_ok || !st.client_base) return false;

    const uintptr_t psize = offsets.player_size ? offsets.player_size : 0x2808;
    const uintptr_t pvalid = offsets.player_valid ? offsets.player_valid : 0x13FC;
    const uintptr_t ppos = offsets.player_pos ? offsets.player_pos : 0xEB0;
    const uintptr_t pteam = offsets.player_team ? offsets.player_team : 0xB0;
    const uintptr_t phealth = offsets.player_health ? offsets.player_health : 0x600;

    uintptr_t player = st.client_base + (uintptr_t)index * psize;
    if (!PointerReadable(player)) return false;

    int valid = 0;
    mem.Read(player + pvalid, &valid, sizeof(valid));
    if (!valid) return false;

    float pos[3]{};
    if (!mem.Read(player + ppos, pos, sizeof(pos))) return false;
    if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) return false;
    if (std::fabs(pos[0]) < 1.f && std::fabs(pos[1]) < 1.f) return false;
    if (std::fabs(pos[0]) > 1e6f || std::fabs(pos[1]) > 1e6f) return false;

    out_pos[0] = pos[0]; out_pos[1] = pos[1]; out_pos[2] = pos[2];
    // Stance-aware head (same table as warzone_game enum path)
    int stance = 0;
    const uintptr_t pstance = offsets.player_stance ? offsets.player_stance : 0x19D0;
    mem.Read(player + pstance, &stance, sizeof(stance));
    if (stance < 0 || stance > 8) stance = 0;
    if (stance > 2) stance = 2;
    float head_dz = 70.f, chest_dz = 40.f;
    if (stance == 1) { head_dz = 42.f; chest_dz = 28.f; }
    else if (stance == 2) { head_dz = 18.f; chest_dz = 12.f; }
    out_head[0] = pos[0]; out_head[1] = pos[1]; out_head[2] = pos[2] + head_dz;
    out_chest[0] = pos[0]; out_chest[1] = pos[1]; out_chest[2] = pos[2] + chest_dz;
    out_valid = true;

    int team = 0;
    mem.Read(player + pteam, &team, sizeof(team));
    out_team = team;

    int hp = 0;
    if (mem.Read(player + phealth, &hp, sizeof(hp)) && hp > 0 && hp <= 200) {
        out_health = (float)hp;
        out_alive = true;
    } else {
        float fhp = 0.f;
        if (mem.Read(player + phealth, &fhp, sizeof(fhp)) && fhp > 0.f && fhp <= 200.f) {
            out_health = fhp;
            out_alive = true;
        }
    }

    if (out_name && name_cap > 1 && st.name_array_ok && st.name_array_base) {
        const uintptr_t nsize = offsets.name_entry_size ? offsets.name_entry_size : 0xD0;
        const uintptr_t npos = offsets.name_array_pos ? offsets.name_array_pos : 0x3038;
        const uintptr_t nname = offsets.name_entry_name ? offsets.name_entry_name : 0x10;
        char buf[64]{};
        mem.Read(st.name_array_base + npos + (uintptr_t)index * nsize + nname, buf, 31);
        buf[31] = 0;
        for (int i = 0; i < 31 && buf[i]; ++i) {
            if ((unsigned char)buf[i] < 32 || (unsigned char)buf[i] > 126) { buf[i] = 0; break; }
        }
        if (buf[0]) std::strncpy(out_name, buf, name_cap - 1);
    }
    return true;
}

} // namespace Warzone
