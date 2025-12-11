#pragma once

#include <fstream>
#include <filesystem>
#include <array>
#include <vector>
#include <ranges>

template<typename Type>
struct BinaryFstreamIO;

class BinaryFstream : public std::fstream {
public:
    BinaryFstream(std::filesystem::path const& path, std::fstream::openmode mode
        = std::fstream::binary | std::fstream::in | std::fstream::out);
    BinaryFstream(BinaryFstream&& other) noexcept;
    BinaryFstream(BinaryFstream const& other) = delete;

    BinaryFstream& operator=(BinaryFstream&& other) noexcept;
    BinaryFstream& operator=(BinaryFstream const& other) = delete;

    void open(std::filesystem::path const& path, std::fstream::openmode mode
        = std::fstream::binary | std::ios_base::in | std::ios_base::out);

    using std::fstream::read;

    template<typename Type>
    Type read() {
        return BinaryFstreamIO<Type>::read(*this);
    }

    template<typename Type, std::size_t Length>
    std::array<Type, Length> read_array() {
        std::array<Type, Length> array;
        for (auto& elem : array) {
            elem = read<Type>();
        }
        return array;
    }

    template<typename Type>
    std::vector<Type> read_vector(size_t const count) {
        auto vector = std::vector<Type>();
        vector.reserve(count);
        for (auto i = 0u; i < count; ++i) {
            vector.emplace_back(read<Type>());
        }
        return vector;
    }

    using std::fstream::write;

    template<typename Type>
    void write(Type const& value) {
        return BinaryFstreamIO<Type>::write(*this, value);
    }

    template<typename Type, std::size_t Length>
    void write_array(std::array<Type, Length> const& array) {
        for (auto const& elem : array) {
            write(elem);
        }
    }

    template<std::ranges::range Range>
    void write_range(Range const& range) {
        for (auto const& elem : range) {
            write(elem);
        }
    }
};

template<typename T>
concept arithmetic = std::is_arithmetic_v<T>;

template<arithmetic ArithmeticType>
struct BinaryFstreamIO<ArithmeticType> {
    static_assert(std::endian::native == std::endian::little, "Little endian is assumed");

    static ArithmeticType read(BinaryFstream& fstream) {
        ArithmeticType value;
        fstream.read(reinterpret_cast<char*>(&value), sizeof(value));
        return value;
    }

    static void write(BinaryFstream& fstream, ArithmeticType const value) {
        fstream.write(reinterpret_cast<char const*>(&value), sizeof(value));
    }
};
