// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Core/Replay.hpp"

#include "SSVOpenHexagon/Global/Assert.hpp"
#include "SSVOpenHexagon/Utils/Concat.hpp"

#include <SSVUtils/Core/Common/LikelyUnlikely.hpp>

#include <SFML/Network/Packet.hpp>

#include <zlib.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace hg {

[[gnu::cold]] static void printTryFailure(const char* code)
{
    ::std::cerr << "Failed [de]serialization operation '" << code << "'\n";
}

#define SSVOH_TRY(...)                      \
    do                                      \
    {                                       \
        __VA_ARGS__;                        \
                                            \
        if(SSVU_UNLIKELY(!result._success)) \
        {                                   \
            printTryFailure(#__VA_ARGS__);  \
            return result;                  \
        }                                   \
    }                                       \
    while(false)

static auto make_write(serialization_result& result, std::byte*& buffer,
    const std::byte* const buffer_end)
{
    return [&result, &buffer, buffer_end](const auto& datum)
    {
        if(buffer + sizeof(datum) > buffer_end)
        {
            result._success = false;
            return;
        }

        std::memcpy(buffer, &datum, sizeof(datum));
        buffer += sizeof(datum);
        result._written_bytes += sizeof(datum);
    };
}

static auto make_read(deserialization_result& result, const std::byte*& buffer,
    const std::byte* const buffer_end)
{
    return [&result, &buffer, buffer_end](auto& target)
    {
        if(buffer + sizeof(target) > buffer_end)
        {
            result._success = false;
            return;
        }

        std::memcpy(&target, buffer, sizeof(target));
        buffer += sizeof(target);
        result._read_bytes += sizeof(target);
    };
}

void replay_data::record_input(const bool left, const bool right,
    const bool swap, const bool focus) noexcept
{
    input_bitset& ib = _inputs.emplace_back();
    ib[static_cast<unsigned int>(input_bit::left)] = left;
    ib[static_cast<unsigned int>(input_bit::right)] = right;
    ib[static_cast<unsigned int>(input_bit::swap)] = swap;
    ib[static_cast<unsigned int>(input_bit::focus)] = focus;
}

[[nodiscard]] input_bitset replay_data::at(
    const std::size_t index) const noexcept
{
    SSVOH_ASSERT(index < size());
    return _inputs[index];
}

[[nodiscard]] std::size_t replay_data::size() const noexcept
{
    return _inputs.size();
}

[[nodiscard]] bool replay_data::operator==(
    const replay_data& rhs) const noexcept
{
    return _inputs == rhs._inputs;
}

[[nodiscard]] bool replay_data::operator!=(
    const replay_data& rhs) const noexcept
{
    return !(*this == rhs);
}

[[nodiscard]] serialization_result replay_data::serialize(
    std::byte* buffer, const std::size_t buffer_size) const
{
    return serialize(buffer, buffer + buffer_size);
}

[[nodiscard]] deserialization_result replay_data::deserialize(
    const std::byte* buffer, const std::size_t buffer_size)
{
    return deserialize(buffer, buffer + buffer_size);
}

[[nodiscard]] serialization_result replay_data::serialize(
    std::byte* buffer, const std::byte* const buffer_end) const
{
    serialization_result result;
    const auto write = make_write(result, buffer, buffer_end);

    const std::size_t n_inputs = _inputs.size();
    SSVOH_TRY(write(n_inputs));

    for(const input_bitset& ib : _inputs)
    {
        const std::uint8_t ib_byte = ib.to_ulong();
        SSVOH_TRY(write(ib_byte));
    }

    return result;
}

[[nodiscard]] deserialization_result replay_data::deserialize(
    const std::byte* buffer, const std::byte* const buffer_end)
{
    deserialization_result result;
    const auto read = make_read(result, buffer, buffer_end);

    std::size_t n_inputs;
    SSVOH_TRY(read(n_inputs));

    _inputs.resize(n_inputs);

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    for(std::size_t i = 0; i < n_inputs; ++i)
    {
        std::uint8_t ib_byte;
        SSVOH_TRY(read(ib_byte));

        _inputs[i] = input_bitset{static_cast<unsigned long>(ib_byte)};
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    return result;
}

replay_player::replay_player(const replay_data& rd) noexcept
    : _replay_data{rd}, _current_index{0}
{}

[[nodiscard]] input_bitset
replay_player::get_current_and_move_forward() noexcept
{
    if(_replay_data.size() <= _current_index)
    {
        return {};
    }

    return _replay_data.at(_current_index++);
}

[[nodiscard]] bool replay_player::done() const noexcept
{
    return _current_index == _replay_data.size();
}

void replay_player::reset() noexcept
{
    _current_index = 0;
}


[[nodiscard]] bool replay_file::operator==(
    const replay_file& rhs) const noexcept
{
    return _version == rhs._version &&                   //
           _player_name == rhs._player_name &&           //
           _seed == rhs._seed &&                         //
           _data == rhs._data &&                         //
           _pack_id == rhs._pack_id &&                   //
           _level_id == rhs._level_id &&                 //
           _music_start_time == rhs._music_start_time && //
           _first_play == rhs._first_play &&             //
           _difficulty_mult == rhs._difficulty_mult &&   //
           _played_score == rhs._played_score;
}

[[nodiscard]] bool replay_file::operator!=(
    const replay_file& rhs) const noexcept
{
    return !(*this == rhs);
}

[[nodiscard]] serialization_result replay_file::serialize(
    std::byte* buffer, const std::size_t buffer_size) const
{
    return serialize(buffer, buffer + buffer_size);
}

[[nodiscard]] deserialization_result replay_file::deserialize(
    const std::byte* buffer, const std::size_t buffer_size)
{
    return deserialize(buffer, buffer + buffer_size);
}

[[nodiscard]] serialization_result replay_file::serialize(
    std::byte* buffer, const std::byte* const buffer_end) const
{
    serialization_result result;
    const auto write = make_write(result, buffer, buffer_end);

    const auto write_str = [&](const std::string& s)
    {
        SSVOH_TRY(write(static_cast<std::uint32_t>(s.size())));

        for(const char c : s)
        {
            SSVOH_TRY(write(c));
        }

        return result;
    };

    SSVOH_TRY(write(_version));
    SSVOH_TRY(write_str(_player_name));
    SSVOH_TRY(write(_seed));

    const serialization_result data_result =
        _data.serialize(buffer, buffer_end);

    if(!data_result._success)
    {
        result._success = false;
        return result;
    }

    buffer += data_result._written_bytes;
    result._written_bytes += data_result._written_bytes;

    SSVOH_TRY(write_str(_pack_id));
    SSVOH_TRY(write_str(_level_id));
    SSVOH_TRY(write(_music_start_time));
    SSVOH_TRY(write(_first_play));
    SSVOH_TRY(write(_difficulty_mult));
    SSVOH_TRY(write(_played_score));

    return result;
}

[[nodiscard]] deserialization_result replay_file::deserialize(
    const std::byte* buffer, const std::byte* const buffer_end)
{
    deserialization_result result;
    const auto read = make_read(result, buffer, buffer_end);

    const auto read_str = [&](std::string& s)
    {
        std::uint32_t s_size;
        SSVOH_TRY(read(s_size));

        s.resize(s_size);

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        for(std::uint32_t i = 0; i < s_size; ++i)
        {
            char c;
            SSVOH_TRY(read(c));

            s[i] = c;
        }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

        return result;
    };

    SSVOH_TRY(read(_version));
    SSVOH_TRY(read_str(_player_name));
    SSVOH_TRY(read(_seed));

    const deserialization_result data_result =
        _data.deserialize(buffer, buffer_end);

    if(!data_result._success)
    {
        result._success = false;
        return result;
    }

    buffer += data_result._read_bytes;
    result._read_bytes += data_result._read_bytes;

    SSVOH_TRY(read_str(_pack_id));
    SSVOH_TRY(read_str(_level_id));
    SSVOH_TRY(read(_music_start_time));
    SSVOH_TRY(read(_first_play));
    SSVOH_TRY(read(_difficulty_mult));
    SSVOH_TRY(read(_played_score));

    return result;
}

static constexpr std::size_t buf_size{2097152}; // 2MB

[[nodiscard]] static std::byte* get_static_buf()
{
    thread_local std::byte buf[buf_size];
    return buf;
}

[[nodiscard]] bool replay_file::serialize_to_file(
    const std::filesystem::path& p) const
{
    std::byte* buf = get_static_buf();

    const serialization_result sr = serialize(buf, buf_size);
    if(!static_cast<bool>(sr))
    {
        return false;
    }

    std::ofstream os(p, std::ios::binary | std::ios::out);
    os.write(reinterpret_cast<const char*>(buf), sr.written_bytes());
    os.flush();

    return static_cast<bool>(os);
}

[[nodiscard]] bool replay_file::deserialize_from_file(
    const std::filesystem::path& p)
{
    std::ifstream is(p, std::ios::binary | std::ios::in);

    is.seekg(0, std::ios::end);
    const std::size_t bytes_to_read = is.tellg();
    is.seekg(0, std::ios::beg);

    std::byte* buf = get_static_buf();

    is.read(reinterpret_cast<char*>(buf), bytes_to_read);

    if(!static_cast<bool>(is))
    {
        return false;
    }

    const deserialization_result dr = deserialize(buf, bytes_to_read);
    return static_cast<bool>(dr);
}

[[nodiscard]] bool replay_file::serialize_to_packet(sf::Packet& p) const
{
    std::byte* buf = get_static_buf();

    const serialization_result sr = serialize(buf, buf_size);
    if(!static_cast<bool>(sr))
    {
        return false;
    }

    const sf::Uint64 written_bytes = sr.written_bytes();

    p << written_bytes;
    p.append(static_cast<const void*>(buf), written_bytes);
    return true;
}

[[nodiscard]] bool replay_file::deserialize_from_packet(sf::Packet& p)
{
    static_assert(sizeof(sf::Uint8) == sizeof(std::byte));
    static_assert(alignof(sf::Uint8) == alignof(std::byte));

    sf::Uint64 bytes_to_read;
    if(!(p >> bytes_to_read))
    {
        return false;
    }

    std::byte* buf = get_static_buf();

    for(sf::Uint64 i = 0; i < bytes_to_read; ++i)
    {
        if(!(p >> reinterpret_cast<sf::Uint8&>(buf[i])))
        {
            return false;
        }
    }

    const deserialization_result dr = deserialize(buf, bytes_to_read);
    return static_cast<bool>(dr);
}


[[nodiscard]] std::string replay_file::create_filename() const
{
    return Utils::concat(_version, '_', _player_name, '_', _level_id, '_',
        _difficulty_mult, '_', _played_score, ".ohreplay");
}

[[nodiscard]] bool compressed_replay_file::serialize_to_file(
    const std::filesystem::path& p) const
{
    std::ofstream os(p, std::ios::binary | std::ios::out);
    os.write(_data.data(), _data.size());
    os.flush();

    return static_cast<bool>(os);
}

[[nodiscard]] bool compressed_replay_file::deserialize_from_file(
    const std::filesystem::path& p)
{
    std::ifstream is(p, std::ios::binary | std::ios::in);

    is.seekg(0, std::ios::end);
    const std::size_t bytes_to_read = is.tellg();
    is.seekg(0, std::ios::beg);

    _data.resize(bytes_to_read);
    is.read(_data.data(), bytes_to_read);

    return static_cast<bool>(is);
}

[[nodiscard]] bool compressed_replay_file::serialize_to_packet(
    sf::Packet& p) const
{
    p << static_cast<sf::Uint64>(_data.size());
    p.append(static_cast<const void*>(_data.data()), _data.size());
    return true;
}

[[nodiscard]] bool compressed_replay_file::deserialize_from_packet(
    sf::Packet& p)
{
    static_assert(sizeof(sf::Uint8) == sizeof(char));
    static_assert(alignof(sf::Uint8) == alignof(char));

    sf::Uint64 bytes_to_read;
    if(!(p >> bytes_to_read))
    {
        return false;
    }

    _data.resize(bytes_to_read);

    for(sf::Uint64 i = 0; i < bytes_to_read; ++i)
    {
        if(!(p >> reinterpret_cast<sf::Uint8&>(_data[i])))
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] static std::byte* get_static_compression_buf()
{
    thread_local std::byte buf[buf_size];
    return buf;
}

[[nodiscard]] std::optional<compressed_replay_file> compress_replay_file(
    const replay_file& rf)
{
    std::byte* buf = get_static_buf();

    const serialization_result sr = rf.serialize(buf, buf_size);
    if(!static_cast<bool>(sr))
    {
        return std::nullopt;
    }

    std::byte* compression_buf = get_static_compression_buf();

    uLongf in_out_dest_len = buf_size;
    const int rc = compress2(reinterpret_cast<Bytef*>(compression_buf),
        &in_out_dest_len, reinterpret_cast<const Bytef*>(buf),
        sr.written_bytes(), Z_BEST_COMPRESSION);

    if(rc != Z_OK)
    {
        std::cerr << "Failed compression of replay file, error code: '" << rc
                  << "'\n";

        return std::nullopt;
    }

    compressed_replay_file result;
    result._data.resize(in_out_dest_len);

    std::memcpy(static_cast<void*>(result._data.data()),
        static_cast<const void*>(compression_buf), result._data.size());

    return {std::move(result)};
}

[[nodiscard]] std::optional<replay_file> decompress_replay_file(
    const compressed_replay_file& crf)
{
    std::byte* buf = get_static_buf();

    uLongf in_out_dest_len = buf_size;
    const int rc = uncompress(reinterpret_cast<Bytef*>(buf), &in_out_dest_len,
        reinterpret_cast<const Bytef*>(crf._data.data()), crf._data.size());

    if(rc != Z_OK)
    {
        std::cerr << "Failed compression of replay file, error code: '" << rc
                  << "'\n";

        return std::nullopt;
    }

    replay_file result;

    const deserialization_result dr = result.deserialize(buf, in_out_dest_len);
    if(!static_cast<bool>(dr))
    {
        return std::nullopt;
    }

    return {std::move(result)};
}

} // namespace hg
