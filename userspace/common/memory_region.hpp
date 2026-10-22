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
#include "common/expected.hpp"

namespace safety {
namespace common {

/** Conservative fallback page size used when sysconf is unavailable. */
constexpr std::size_t kDefaultPageSize = 4096;

/** Access permissions requested for a physical memory mapping. */
enum class MemoryAccessMode : std::uint8_t {
    ReadOnly,
    ReadWrite
};

/** Custom deleter for std::unique_ptr managing mmap memory allocations. */
struct MmapDeleter {
    std::size_t length{0};

    /** Unmap the allocation when its owning smart pointer is destroyed. */
    void operator()(void* ptr) const noexcept {
        if (ptr != nullptr && ptr != MAP_FAILED && length > 0) {
            ::munmap(ptr, length);
        }
    }
};

/** RAII wrapper for a physical memory region mapped through /dev/mem. */
class PhysicalMemoryView {
public:
    /** Creates an empty, invalid view. */
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

    /**
     * @brief Map a physical memory range using page alignment.
     * @param phys_addr Physical start address requested by the caller.
     * @param size Number of bytes visible through the returned view.
     * @param mode Read-only or read-write mapping permissions.
     * @param dev_path Device file used for the mapping, normally `/dev/mem`.
     * @return A valid view on success, or a descriptive error string.
     */
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
        const auto page_size = static_cast<std::size_t>(page_size_long);

        const uint64_t page_base = phys_addr & ~(page_size - 1);
        const auto page_offset = static_cast<std::size_t>(phys_addr & (page_size - 1));
        const std::size_t map_length = page_offset + size;

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
        return PhysicalMemoryView(std::move(mapping), phys_addr, size, page_offset);
    }

    /**
     * @brief Return a read-only span over the mapped physical memory region.
     * @return An empty span if the view is invalid; otherwise the requested bytes.
     */
    [[nodiscard]] auto view() const noexcept -> std::span<const std::byte> {
        if (!mapping_) {
            return {};
        }
        const auto* byte_ptr = static_cast<const std::byte*>(mapping_.get()) + offset_in_page_;
        return {byte_ptr, size_};
    }

    /**
     * @brief Return a writable span over the mapped physical memory region.
     * @return An empty span if the view is invalid; otherwise writable mapped bytes.
     */
    [[nodiscard]] auto mutable_view() noexcept -> std::span<std::byte> {
        if (!mapping_) {
            return {};
        }
        auto* byte_ptr = static_cast<std::byte*>(mapping_.get()) + offset_in_page_;
        return {byte_ptr, size_};
    }

    /**
     * @brief Read a trivially copyable value at an offset within the mapping.
     * @param offset Byte offset relative to the requested physical address.
     * @return The value read, or a value-initialized object for an invalid view.
     */
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] auto read_at(std::ptrdiff_t offset = 0) const -> T {
        if (!mapping_) {
            return T{};
        }
        const auto* base_ptr = static_cast<const std::byte*>(mapping_.get()) + offset_in_page_ + offset;

        if constexpr (sizeof(T) == sizeof(uint32_t) && std::is_integral_v<T>) {
            const volatile auto* vptr = reinterpret_cast<const volatile uint32_t*>(base_ptr);
            const uint32_t val = *vptr;
            return std::bit_cast<T>(val);
        } else {
            T val{};
            std::memcpy(&val, base_ptr, sizeof(T));
            return val;
        }
    }

    /**
     * @brief Write a trivially copyable value at an offset within the mapping.
     * @param val Value to write to the mapped physical memory.
     * @param offset Byte offset relative to the requested physical address.
     */
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void write_at(const T& val, std::ptrdiff_t offset = 0) {
        if (!mapping_) {
            return;
        }
        auto* base_ptr = static_cast<std::byte*>(mapping_.get()) + offset_in_page_ + offset;

        if constexpr (sizeof(T) == sizeof(uint32_t) && std::is_integral_v<T>) {
            volatile auto* vptr = reinterpret_cast<volatile uint32_t*>(base_ptr);
            *vptr = std::bit_cast<uint32_t>(val);
        } else {
            std::memcpy(base_ptr, &val, sizeof(T));
        }
    }

    /** Returns the requested physical start address. */
    [[nodiscard]] uint64_t phys_addr() const noexcept { return phys_addr_; }
    /** Returns the requested mapping size in bytes. */
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    /** Returns the byte offset from the aligned mapping base. */
    [[nodiscard]] std::size_t offset_in_page() const noexcept { return offset_in_page_; }
    /** Reports whether the view owns a valid mapping. */
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
