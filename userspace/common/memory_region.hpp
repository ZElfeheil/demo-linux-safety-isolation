#ifndef SAFETY_COMMON_MEMORY_REGION_HPP
#define SAFETY_COMMON_MEMORY_REGION_HPP

#include <bit>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>
#include "expected.hpp"

namespace safety {
namespace common {

constexpr std::size_t kDefaultPageSize = 4096;

enum class MemoryAccessMode : std::uint8_t {
    ReadOnly,
    ReadWrite
};

/// Custom deleter for std::unique_ptr managing mmap memory allocations
struct MmapDeleter {
    std::size_t length{0};

    void operator()(void* ptr) const noexcept {
        if (ptr != nullptr && ptr != MAP_FAILED && length > 0) {
            ::munmap(ptr, length);
        }
    }
};

/// RAII wrapper for physical memory regions mapped via /dev/mem
// C++ Core Guidelines I.11 (no raw owning pointer), I.13 (no raw array transfer), ES.49 (bit_cast)
class PhysicalMemoryView {
public:
    PhysicalMemoryView() = default;

    PhysicalMemoryView(std::unique_ptr<void, MmapDeleter> mapping,
                       uint64_t phys_addr,
                       std::size_t size,
                       std::size_t offset_in_page)
        : mapping_(std::move(mapping)),
          phys_addr_(phys_addr),
          size_(size),
          offset_in_page_(offset_in_page) {}

    ~PhysicalMemoryView() = default;

    PhysicalMemoryView(PhysicalMemoryView&&) noexcept = default;
    PhysicalMemoryView& operator=(PhysicalMemoryView&&) noexcept = default;

    PhysicalMemoryView(const PhysicalMemoryView&) = delete;
    PhysicalMemoryView& operator=(const PhysicalMemoryView&) = delete;

    /// Maps a physical memory range using page alignment
    [[nodiscard]] static auto map(uint64_t phys_addr,
                                  std::size_t size = kDefaultPageSize,
                                  MemoryAccessMode mode = MemoryAccessMode::ReadOnly,
                                  const char* dev_path = "/dev/mem") -> safety::expected<PhysicalMemoryView, std::string> {
        if (size == 0) {
            return safety::unexpected(std::string("Cannot map physical memory region of 0 size."));
        }

        long page_size_long = ::sysconf(_SC_PAGESIZE);
        if (page_size_long <= 0) {
            page_size_long = static_cast<long>(kDefaultPageSize);
        }
        const std::size_t page_size = static_cast<std::size_t>(page_size_long);

        const uint64_t page_base = phys_addr & ~(page_size - 1);
        const std::size_t offset_in_page = static_cast<std::size_t>(phys_addr & (page_size - 1));
        const std::size_t map_length = offset_in_page + size;

        int open_flags = (mode == MemoryAccessMode::ReadOnly) ? O_RDONLY : O_RDWR;
        open_flags |= O_SYNC;

        int fd = ::open(dev_path, open_flags);
        if (fd < 0) {
            return safety::unexpected("Failed to open " + std::string(dev_path) + ": " + std::string(::strerror(errno)));
        }

        int prot = (mode == MemoryAccessMode::ReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE);
        void* raw_ptr = ::mmap(nullptr, map_length, prot, MAP_SHARED, fd, static_cast<off_t>(page_base));

        ::close(fd);

        if (raw_ptr == MAP_FAILED) {
            return safety::unexpected("mmap failed for phys_addr 0x" + std::to_string(phys_addr) + ": " + std::string(::strerror(errno)));
        }

        std::unique_ptr<void, MmapDeleter> mapping(raw_ptr, MmapDeleter{map_length});
        return PhysicalMemoryView(std::move(mapping), phys_addr, size, offset_in_page);
    }

    /// Returns a const span view of the mapped physical memory region (I.13)
    [[nodiscard]] auto view() const noexcept -> std::span<const std::byte> {
        if (!mapping_) {
            return {};
        }
        const auto* byte_ptr = static_cast<const std::byte*>(mapping_.get()) + offset_in_page_;
        return {byte_ptr, size_};
    }

    /// Returns a mutable span view of the mapped physical memory region
    [[nodiscard]] auto mutable_view() noexcept -> std::span<std::byte> {
        if (!mapping_) {
            return {};
        }
        auto* byte_ptr = static_cast<std::byte*>(mapping_.get()) + offset_in_page_;
        return {byte_ptr, size_};
    }

    /// Reads a typed value at the specified offset within the mapped region using std::bit_cast (ES.49)
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] auto read_at(std::ptrdiff_t offset = 0) const -> T {
        if (!mapping_) {
            return T{};
        }
        const auto* base_ptr = static_cast<const std::byte*>(mapping_.get()) + offset_in_page_ + offset;

        if constexpr (sizeof(T) == 4 && std::is_integral_v<T>) {
            const volatile auto* vptr = reinterpret_cast<const volatile uint32_t*>(base_ptr);
            const uint32_t val = *vptr;
            return std::bit_cast<T>(val);
        } else {
            T val{};
            std::memcpy(&val, base_ptr, sizeof(T));
            return val;
        }
    }

    /// Writes a typed value at the specified offset within the mapped region using std::bit_cast (ES.49)
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void write_at(const T& val, std::ptrdiff_t offset = 0) {
        if (!mapping_) {
            return;
        }
        auto* base_ptr = static_cast<std::byte*>(mapping_.get()) + offset_in_page_ + offset;

        if constexpr (sizeof(T) == 4 && std::is_integral_v<T>) {
            volatile auto* vptr = reinterpret_cast<volatile uint32_t*>(base_ptr);
            *vptr = std::bit_cast<uint32_t>(val);
        } else {
            std::memcpy(base_ptr, &val, sizeof(T));
        }
    }

    [[nodiscard]] uint64_t phys_addr() const noexcept { return phys_addr_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t offset_in_page() const noexcept { return offset_in_page_; }
    [[nodiscard]] bool is_valid() const noexcept { return mapping_ != nullptr; }

private:
    std::unique_ptr<void, MmapDeleter> mapping_{nullptr, MmapDeleter{0}};
    uint64_t phys_addr_{0};
    std::size_t size_{0};
    std::size_t offset_in_page_{0};
};

} // namespace common

using PhysicalMemoryView = common::PhysicalMemoryView;
using MemoryAccessMode = common::MemoryAccessMode;
using MmapDeleter = common::MmapDeleter;

} // namespace safety

#endif // SAFETY_COMMON_MEMORY_REGION_HPP
