#ifndef USERSPACE_MONITOR_RENDERER_HPP
#define USERSPACE_MONITOR_RENDERER_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <sys/ioctl.h>
#include <unistd.h>
#include "common/expected.hpp"

namespace safety::monitor {

enum class DashboardMode : std::uint8_t {
    Normal,
    Paused,
    Revealed
};

struct ScenarioInfo {
    std::string id{"Scenario D"};
    std::string title{"DMA Linear Map Bypass"};
    std::string setup_text{"CTX active (set_memory_ro on vmalloc alias). Driver writes via phys_to_virt()."};
    std::string question_text{"Same physical page. Different virtual address. Protected?"};
    std::vector<std::string> options{
        "A) Yes — same physical page, same protection applies",
        "B) No — set_memory_ro protected one virtual alias only",
        "C) Depends on TLB flush status"
    };
    std::string correct_option{"B"};
    std::string explanation{
        "set_memory_ro modifies ONE PTE — the vmalloc virtual alias.\n"
        "The linear map has its own PTE to the same physical page, never modified.\n"
        "One physical page, two virtual aliases, one protected."
    };
    uint64_t start_time_sec{0};
};

struct EventEntry {
    std::string timestamp{};
    std::string source{};
    std::string message{};
    bool is_warning{false};
    bool is_error{false};
};

struct DisplayState {
    std::atomic<uint32_t> val_vmalloc{0x5AFE1234};
    std::atomic<uint32_t> val_phys{0x5AFE1234};
    std::atomic<uint64_t> virt_addr{0xffff800012340000ULL};
    std::atomic<uint64_t> phys_addr{0x0000000040001000ULL};
    std::atomic<bool>     ctx_protected{false};
    std::atomic<bool>     smmu_active{false};
    std::atomic<DashboardMode> mode{DashboardMode::Normal};

    mutable std::mutex status_mx{};
    std::string mutex_owner{"none"};
    std::string status_str{"PROTECTED_RO"};

    mutable std::mutex events_mx{};
    std::vector<EventEntry> events{};
    static constexpr std::size_t max_events{100};

    mutable std::mutex scenario_mx{};
    ScenarioInfo scenario_info{};
};

struct TerminalSize {
    uint16_t cols{80};
    uint16_t rows{24};
};

class TerminalGuard {
public:
    TerminalGuard();
    ~TerminalGuard();

    TerminalGuard(const TerminalGuard&) = delete;
    TerminalGuard& operator=(const TerminalGuard&) = delete;
    TerminalGuard(TerminalGuard&&) noexcept = default;
    TerminalGuard& operator=(TerminalGuard&&) noexcept = default;

    [[nodiscard]] static auto get_size() noexcept -> TerminalSize;
};

class Renderer {
public:
    explicit Renderer(const DisplayState& state) : state_(&state) {}

    void render(std::string& out_buf, TerminalSize size) const;

private:
    void render_normal(std::string& out, TerminalSize size) const;
    void render_paused(std::string& out, TerminalSize size) const;
    void render_revealed(std::string& out, TerminalSize size) const;

    const DisplayState* state_{nullptr};
};

} // namespace safety::monitor

#endif // USERSPACE_MONITOR_RENDERER_HPP
