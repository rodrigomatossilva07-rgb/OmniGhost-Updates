#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>

// Rust decrypt/encrypt formulas supplied with the 2026-10-22 dump.
// read/get_handle must be bound by the future DMA runtime before handle decryptors are used.
namespace Rust::Crypto::Build_2026_10_22 {
namespace decryptor
{
    inline auto read = std::function<std::uint64_t ( std::uint64_t )> ( );
    inline auto get_handle = std::function<std::uint64_t ( std::uint64_t )> ( );

    inline auto decrypt_clactiveitem ( std::uint64_t value ) -> std::uint64_t
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 2>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 2 );
            word ^= 0xec201afbu;
            word += 0x3d62f8bau;
            word = std::rotl ( word, 16 );
        }

        return std::bit_cast<std::uint64_t> ( words );
    }

    inline auto decrypt_client_entities ( std::uint64_t value ) -> std::uint64_t
    {
        const auto handle = decryptor::read ( value + 0x18 );

        auto words = std::bit_cast<std::array<std::uint32_t, 2>> ( handle );

        for ( auto& word : words )
        {
            word += 0xb6099923u;
            word ^= 0x165993au;
            word = std::rotl ( word, 12 );
            word ^= 0x6ea51c6u;
        }

        return decryptor::get_handle ( std::bit_cast<std::uint64_t> ( words ) );
    }

    inline auto decrypt_client_tick_interval ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 4 );
            word += 0x4e30062eu;
            word = std::rotl ( word, 29 );
            word ^= 0x74a660fau;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_entity_list ( std::uint64_t value ) -> std::uint64_t
    {
        const auto handle = decryptor::read ( value + 0x18 );

        auto words = std::bit_cast<std::array<std::uint32_t, 2>> ( handle );

        for ( auto& word : words )
        {
            word += 0x606a395cu;
            word = std::rotl ( word, 17 );
            word += 0xc7f322e4u;
        }

        return decryptor::get_handle ( std::bit_cast<std::uint64_t> ( words ) );
    }

    inline auto decrypt_eyes ( std::uint64_t value ) -> std::uint64_t
    {
        const auto handle = decryptor::read ( value + 0x18 );

        auto words = std::bit_cast<std::array<std::uint32_t, 2>> ( handle );

        for ( auto& word : words )
        {
            word ^= 0x8d1fb868u;
            word = std::rotl ( word, 13 );
            word ^= 0xbd80150du;
            word += 0x5aa25993u;
        }

        return decryptor::get_handle ( std::bit_cast<std::uint64_t> ( words ) );
    }

    inline auto decrypt_fov ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 4 );
            word += 0x4e30062eu;
            word = std::rotl ( word, 29 );
            word ^= 0x74a660fau;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_ground_angle ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word ^= 0x80eebe00u;
            word += 0xe197fc62u;
            word = std::rotl ( word, 5 );
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_ground_angle_new ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word += 0xbebb6017u;
            word = std::rotl ( word, 28 );
            word ^= 0xe0c605cau;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_ground_normal ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word ^= 0x8f0aa184u;
            word += 0x9a3b318au;
            word = std::rotl ( word, 21 );
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto decrypt_ground_normal_new ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word += 0x2b19b584u;
            word = std::rotl ( word, 23 );
            word ^= 0x4fac165cu;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto decrypt_ground_time ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 6 );
            word += 0x88eec6cau;
            word = std::rotl ( word, 8 );
            word ^= 0x7abd7cfcu;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_ground_velocity ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word += 0xa2abdb3bu;
            word = std::rotl ( word, 8 );
            word ^= 0x96ee1dcu;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto decrypt_ground_velocity_new ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 6 );
            word += 0xd1149d62u;
            word = std::rotl ( word, 9 );
            word ^= 0x531736ceu;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto decrypt_inventory ( std::uint64_t value ) -> std::uint64_t
    {
        const auto handle = decryptor::read ( value + 0x18 );

        auto words = std::bit_cast<std::array<std::uint32_t, 2>> ( handle );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 2 );
            word += 0xa0eb8d31u;
            word = std::rotl ( word, 31 );
            word += 0xecc48a04u;
        }

        return decryptor::get_handle ( std::bit_cast<std::uint64_t> ( words ) );
    }

    inline auto decrypt_jump_time ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word += 0x38a8a5c4u;
            word ^= 0xd9249408u;
            word = std::rotl ( word, 27 );
            word ^= 0xd52fd983u;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_land_time ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word += 0xf364ec76u;
            word ^= 0x362746ccu;
            word += 0x27160a3fu;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_last_tick_time ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word += 0x9fc94586u;
            word ^= 0x31f95b8du;
            word = std::rotl ( word, 19 );
            word ^= 0xf80ecf9du;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_lastsentticktime ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word += 0x1ed078ebu;
            word = std::rotl ( word, 15 );
            word += 0x757fedf8u;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_localplayer ( std::uint64_t value ) -> std::uint64_t
    {
        const auto handle = decryptor::read ( value + 0x18 );

        auto words = std::bit_cast<std::array<std::uint32_t, 2>> ( handle );

        for ( auto& word : words )
        {
            word += 0xb6099923u;
            word ^= 0x165993au;
            word = std::rotl ( word, 12 );
            word ^= 0x6ea51c6u;
        }

        return decryptor::get_handle ( std::bit_cast<std::uint64_t> ( words ) );
    }

    inline auto decrypt_max_velocity ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word += 0x3a1aff87u;
            word = std::rotl ( word, 6 );
            word += 0x80bc4882u;
            word = std::rotl ( word, 15 );
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_next_attack_time ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 18 );
            word ^= 0x5fb5ea9u;
            word = std::rotl ( word, 17 );
            word ^= 0x55dcf592u;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_next_reload_time ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 8 );
            word += 0x95051c9au;
            word ^= 0xdc9a3c45u;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_next_tick_time ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word += 0x7761570eu;
            word ^= 0xade7e2dau;
            word = std::rotl ( word, 29 );
            word += 0x8242c236u;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_previous_inherited_velocity ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word += 0xaf20442bu;
            word = std::rotl ( word, 25 );
            word ^= 0xdb70b2c5u;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto decrypt_previous_position ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 10 );
            word += 0x7c721392u;
            word ^= 0x7b14a89du;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto decrypt_previous_velocity ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word += 0x5b9e1af0u;
            word ^= 0x81ca028fu;
            word = std::rotl ( word, 3 );
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto decrypt_start_reload_time ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word += 0xdf455beeu;
            word = std::rotl ( word, 11 );
            word += 0xbea68576u;
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_time_since_deploy ( std::uint32_t value ) -> float
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 1>> ( value );

        for ( auto& word : words )
        {
            word += 0xaa78706du;
            word ^= 0xb53d54beu;
            word += 0x915d529fu;
            word = std::rotl ( word, 13 );
        }

        auto result = 0.00f;
        std::memcpy ( &result, words.data ( ), sizeof ( result ) );

        return result;
    }

    inline auto decrypt_viewoffset ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word += 0xf94b283fu;
            word ^= 0x57fdf257u;
            word = std::rotl ( word, 16 );
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto encrypt_client_tick_interval ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result ^= 0x74a660fau;
        result = std::rotl ( result, 3 );
        result += 0xb1cff9d2u;
        result = std::rotl ( result, 28 );

        return result;
    }

    inline auto encrypt_fov ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result ^= 0x74a660fau;
        result = std::rotl ( result, 3 );
        result += 0xb1cff9d2u;
        result = std::rotl ( result, 28 );

        return result;
    }

    inline auto encrypt_ground_angle ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result = std::rotl ( result, 27 );
        result += 0x1e68039eu;
        result ^= 0x80eebe00u;

        return result;
    }

    inline auto encrypt_ground_angle_new ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result ^= 0xe0c605cau;
        result = std::rotl ( result, 4 );
        result += 0x41449fe9u;

        return result;
    }

    inline auto encrypt_ground_normal ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 11 );
            word += 0x65c4ce76u;
            word ^= 0x8f0aa184u;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto encrypt_ground_normal_new ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word ^= 0x4fac165cu;
            word = std::rotl ( word, 9 );
            word += 0xd4e64a7cu;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto encrypt_ground_time ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result ^= 0x7abd7cfcu;
        result = std::rotl ( result, 24 );
        result += 0x77113936u;
        result = std::rotl ( result, 26 );

        return result;
    }

    inline auto encrypt_ground_velocity ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word ^= 0x96ee1dcu;
            word = std::rotl ( word, 24 );
            word += 0x5d5424c5u;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto encrypt_ground_velocity_new ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word ^= 0x531736ceu;
            word = std::rotl ( word, 23 );
            word += 0x2eeb629eu;
            word = std::rotl ( word, 26 );
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto encrypt_jump_time ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result ^= 0xd52fd983u;
        result = std::rotl ( result, 5 );
        result ^= 0xd9249408u;
        result += 0xc7575a3cu;

        return result;
    }

    inline auto encrypt_land_time ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result += 0xd8e9f5c1u;
        result ^= 0x362746ccu;
        result += 0xc9b138au;

        return result;
    }

    inline auto encrypt_last_tick_time ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result ^= 0xf80ecf9du;
        result = std::rotl ( result, 13 );
        result ^= 0x31f95b8du;
        result += 0x6036ba7au;

        return result;
    }

    inline auto encrypt_max_velocity ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result = std::rotl ( result, 17 );
        result += 0x7f43b77eu;
        result = std::rotl ( result, 26 );
        result += 0xc5e50079u;

        return result;
    }

    inline auto encrypt_next_attack_time ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result ^= 0x55dcf592u;
        result = std::rotl ( result, 15 );
        result ^= 0x5fb5ea9u;
        result = std::rotl ( result, 14 );

        return result;
    }

    inline auto encrypt_next_reload_time ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result ^= 0xdc9a3c45u;
        result += 0x6afae366u;
        result = std::rotl ( result, 24 );

        return result;
    }

    inline auto encrypt_next_tick_time ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result += 0x7dbd3dcau;
        result = std::rotl ( result, 3 );
        result ^= 0xade7e2dau;
        result += 0x889ea8f2u;

        return result;
    }

    inline auto encrypt_previous_inherited_velocity ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word ^= 0xdb70b2c5u;
            word = std::rotl ( word, 7 );
            word += 0x50dfbbd5u;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto encrypt_previous_position ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word ^= 0x7b14a89du;
            word += 0x838dec6eu;
            word = std::rotl ( word, 22 );
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto encrypt_previous_velocity ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 29 );
            word ^= 0x81ca028fu;
            word += 0xa461e510u;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

    inline auto encrypt_start_reload_time ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result += 0x41597a8au;
        result = std::rotl ( result, 21 );
        result += 0x20baa412u;

        return result;
    }

    inline auto encrypt_time_since_deploy ( float value ) -> std::uint32_t
    {
        auto result = std::uint32_t ( );
        std::memcpy ( &result, &value, sizeof ( result ) );

        result = std::rotl ( result, 19 );
        result += 0x6ea2ad61u;
        result ^= 0xb53d54beu;
        result += 0x55878f93u;

        return result;
    }

    inline auto encrypt_viewoffset ( std::array<std::uint32_t, 3> value ) -> std::array<std::uint32_t, 3>
    {
        auto words = std::bit_cast<std::array<std::uint32_t, 3>> ( value );

        for ( auto& word : words )
        {
            word = std::rotl ( word, 16 );
            word ^= 0x57fdf257u;
            word += 0x6b4d7c1u;
        }

        return std::bit_cast<std::array<std::uint32_t, 3>> ( words );
    }

}
}

