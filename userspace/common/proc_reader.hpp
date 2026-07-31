#ifndef USERSPACE_COMMON_PROC_READER_HPP
#define USERSPACE_COMMON_PROC_READER_HPP

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include "expected.hpp"

namespace safety {

// C++ Core Guidelines R.1 (RAII), I.11 (no raw owning pointers), E.1 (safety::expected)
class ProcReader {
public:
    explicit ProcReader(std::filesystem::path path) : path_(std::move(path)) {}

    [[nodiscard]] auto read() const -> safety::expected<std::string, std::string> {
        if (!std::filesystem::exists(path_)) {
            return safety::unexpected("File does not exist: " + path_.string());
        }

        std::ifstream file(path_, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            return safety::unexpected("Failed to open file: " + path_.string());
        }

        std::string content;
        file.seekg(0, std::ios::end);
        content.reserve(static_cast<size_t>(file.tellg()));
        file.seekg(0, std::ios::beg);

        content.assign((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

        if (file.bad()) {
            return safety::unexpected("I/O error reading file: " + path_.string());
        }

        return content;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace safety

#endif // USERSPACE_COMMON_PROC_READER_HPP
