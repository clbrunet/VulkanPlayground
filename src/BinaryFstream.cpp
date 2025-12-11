#include <BinaryFstream.hpp>

BinaryFstream::BinaryFstream(std::filesystem::path const& path, std::fstream::openmode const mode) :
    std::fstream(path, mode | std::fstream::binary | std::ios_base::in | std::ios_base::out) {
}

BinaryFstream::BinaryFstream(BinaryFstream&& other) noexcept : std::fstream(std::move(other)) {
}

BinaryFstream& BinaryFstream::operator=(BinaryFstream&& other) noexcept {
    std::fstream::operator=(std::move(other));
    return *this;
}

void BinaryFstream::open(std::filesystem::path const& path, std::fstream::openmode const mode) {
    std::fstream::open(path, mode | std::fstream::binary | std::ios_base::in | std::ios_base::out);
}
