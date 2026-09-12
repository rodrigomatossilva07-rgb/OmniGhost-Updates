#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace OmniGhost::EmbeddedOffsets {

class ByteView {
public:
    constexpr ByteView() noexcept = default;
    constexpr ByteView(const std::uint8_t* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    ByteView(const std::vector<std::uint8_t>& data) noexcept
        : data_(data.data()), size_(data.size()) {}

    template <std::size_t N>
    constexpr ByteView(const std::uint8_t (&data)[N]) noexcept : data_(data), size_(N) {}

    [[nodiscard]] constexpr const std::uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr const std::uint8_t& operator[](std::size_t index) const noexcept { return data_[index]; }
    [[nodiscard]] constexpr const std::uint8_t* begin() const noexcept { return data_; }
    [[nodiscard]] constexpr const std::uint8_t* end() const noexcept { return data_ ? data_ + size_ : data_; }

    [[nodiscard]] constexpr ByteView subview(std::size_t offset, std::size_t count) const noexcept {
        return (offset <= size_ && count <= size_ - offset)
            ? ByteView(data_ + offset, count)
            : ByteView{};
    }

private:
    const std::uint8_t* data_{};
    std::size_t size_{};
};

enum class Game : std::uint16_t {
    Fortnite = 1,
    Warzone = 2,
    CS2 = 3,
    FiveM = 5,
    Apex = 6,
};

struct Metadata {
    std::uint32_t schemaVersion{};
    std::string game;
    std::string build;
    std::string cl;
    std::string module;
    std::string source;
    std::string generatedAt;
    std::string note;
};

struct Entry {
    std::uint32_t keyHash{};
    std::uint64_t value{};
};

struct Snapshot {
    Metadata metadata;
    std::vector<Entry> entries;

    [[nodiscard]] bool TryGet(std::string_view path, std::uint64_t& value) const noexcept;
};

struct Diagnostics {
    bool resourceFound{};
    bool headerValid{};
    bool sizeValid{};
    bool decompressed{};
    bool integrityValid{};
    bool parsed{};
    std::size_t resourceSize{};
    std::size_t compressedSize{};
    std::size_t uncompressedSize{};
    std::string error;
};

[[nodiscard]] std::uint32_t HashOffsetPath(std::string_view path) noexcept;
[[nodiscard]] const char* GameName(Game game) noexcept;
[[nodiscard]] int ResourceIdForGame(Game game) noexcept;

[[nodiscard]] bool MetadataMatches(const Snapshot& snapshot,
                                   std::string_view expectedGame,
                                   std::string_view expectedBuild,
                                   std::string_view expectedCl,
                                   std::string_view expectedModule) noexcept;

// Pure parser used by the resource loader and unit tests. It never touches disk.
// ByteView keeps the implementation compatible with the main project's C++17 toolchain.
[[nodiscard]] bool ParseEmbeddedOffsetBlob(ByteView blob,
                                           Game expectedGame,
                                           Snapshot& snapshot,
                                           Diagnostics& diagnostics);

// Loads RCDATA from the current executable and parses it entirely in memory.
[[nodiscard]] bool Load(Game game, Snapshot& snapshot, Diagnostics& diagnostics);

} // namespace OmniGhost::EmbeddedOffsets
