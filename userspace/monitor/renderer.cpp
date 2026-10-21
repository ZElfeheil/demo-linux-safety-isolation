#include "monitor/renderer.hpp"
#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>

namespace safety::monitor {

namespace {

constexpr uint16_t kMinCols = 40;
constexpr uint16_t kMinRows = 12;
constexpr std::size_t kMinSideW = 18;
constexpr unsigned char kUtf8TwoByteMask = 0xE0;
constexpr unsigned char kUtf8TwoByteLead = 0xC0;
constexpr unsigned char kUtf8ThreeByteMask = 0xF0;
constexpr unsigned char kUtf8ThreeByteLead = 0xE0;
constexpr unsigned char kUtf8FourByteMask = 0xF8;
constexpr unsigned char kUtf8FourByteLead = 0xF0;
constexpr std::size_t kUtf8TwoByteLength = 2;
constexpr std::size_t kUtf8ThreeByteLength = 3;
constexpr std::size_t kUtf8FourByteLength = 4;
constexpr std::size_t kLeftPanelRatioNumerator = 2;
constexpr std::size_t kPanelRatioDenominator = 5;
constexpr std::size_t kPanelBordersWidth = 3;
constexpr std::size_t kDashboardFrameRows = 4;

enum class SnapshotRow : std::size_t {
    BeforeHeading,
    BeforeValues,
    BeforePageTable,
    BeforeProtection,
    AfterHeading,
    AfterValues,
    AfterPageTable,
    AfterProtection,
};

std::size_t visual_len(std::string_view s) noexcept {
    std::size_t len = 0;
    for (std::size_t i = 0; i < s.size(); ) {
        auto c = static_cast<unsigned char>(s[i]);
        if ((c & kUtf8TwoByteMask) == kUtf8TwoByteLead) i += kUtf8TwoByteLength;
        else if ((c & kUtf8ThreeByteMask) == kUtf8ThreeByteLead) i += kUtf8ThreeByteLength;
        else if ((c & kUtf8FourByteMask) == kUtf8FourByteLead) i += kUtf8FourByteLength;
        else i += 1;
        len += 1;
    }
    return len;
}

std::string make_hline(std::string_view fill, std::size_t count) {
    std::string res;
    res.reserve(fill.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        res.append(fill);
    }
    return res;
}

std::string pad_line(std::string_view text, std::size_t target_width) {
    std::size_t vlen = visual_len(text);
    if (vlen >= target_width) {
        // Truncate safely at character boundary if needed
        std::size_t byte_idx = 0;
        std::size_t cur_vlen = 0;
        while (byte_idx < text.size() && cur_vlen < target_width) {
            auto c = static_cast<unsigned char>(text[byte_idx]);
            if ((c & kUtf8TwoByteMask) == kUtf8TwoByteLead) byte_idx += kUtf8TwoByteLength;
            else if ((c & kUtf8ThreeByteMask) == kUtf8ThreeByteLead) byte_idx += kUtf8ThreeByteLength;
            else if ((c & kUtf8FourByteMask) == kUtf8FourByteLead) byte_idx += kUtf8FourByteLength;
            else byte_idx += 1;
            cur_vlen += 1;
        }
        return std::string(text.substr(0, byte_idx));
    }
    std::string res(text);
    res.append(target_width - vlen, ' ');
    return res;
}

} // namespace

TerminalGuard::TerminalGuard() {
    std::cout << "\033[?1049h\033[?25l" << std::flush;
}

TerminalGuard::~TerminalGuard() {
    std::cout << "\033[?25h\033[?1049l" << std::flush;
}

TerminalSize TerminalGuard::get_size() noexcept {
    struct winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return TerminalSize{ws.ws_col, ws.ws_row};
    }
    return TerminalSize{kDefaultTerminalCols, kDefaultTerminalRows};
}

void Renderer::render(std::string& out_buf, TerminalSize size) const {
    out_buf.clear();
    out_buf.append("\033[H"); // Cursor home (top-left)

    if (state_ == nullptr) return;

    // The monitor is an operational view of memory state and kernel events.
    render_normal(out_buf, size);
}

void Renderer::render_normal(std::string& out, TerminalSize size) const {
    if (state_ == nullptr) return;

    const uint16_t cols = (size.cols < kMinCols) ? kMinCols : size.cols;
    const uint16_t rows = (size.rows < kMinRows) ? kMinRows : size.rows;

    const std::size_t panel_width = cols - kPanelBordersWidth;
    const std::size_t desired_left_w =
        panel_width * kLeftPanelRatioNumerator / kPanelRatioDenominator;
    const std::size_t left_w = std::clamp(
        desired_left_w, kMinSideW, panel_width - kMinSideW);
    const std::size_t right_w = panel_width - left_w;
    const auto main_h = static_cast<std::size_t>(rows - kDashboardFrameRows);

    // Header line: ┌─ SAFETY MEMORY STATE ─┬─ KERNEL EVENT STREAM ─┐
    const std::string left_title = " SAFETY MEMORY STATE ";
    const std::string right_title = " KERNEL EVENT STREAM ";

    const std::size_t l_fill = (left_w > visual_len(left_title)) ? (left_w - visual_len(left_title)) : 0;
    const std::size_t r_fill = (right_w > visual_len(right_title)) ? (right_w - visual_len(right_title)) : 0;

    std::string top_hdr = "┌" + make_hline("─", l_fill / 2) + left_title + make_hline("─", l_fill - l_fill / 2)
                        + "┬" + make_hline("─", r_fill / 2) + right_title + make_hline("─", r_fill - r_fill / 2) + "┐\n";
    out.append(top_hdr);

    // Snapshot state
    struct SnapshotView {
        uint32_t vmalloc_value;
        uint32_t phys_value;
        uint64_t mapping_entry;
        bool writable;
        bool ctx_protected;
        bool smmu_active;
        std::string mapping_level;
        std::string status;
    };
    const auto snapshot_view = [](const ComparisonSnapshot& snapshot) {
        SnapshotView view{
            .vmalloc_value = snapshot.val_vmalloc.load(),
            .phys_value = snapshot.val_phys.load(),
            .mapping_entry = snapshot.mapping_entry.load(),
            .writable = snapshot.mapping_writable.load(),
            .ctx_protected = snapshot.ctx_protected.load(),
            .smmu_active = snapshot.smmu_active.load(),
            .mapping_level = {},
            .status = {}
        };
        std::lock_guard lock(snapshot.mx);
        view.mapping_level = snapshot.mapping_level;
        view.status = snapshot.status;
        return view;
    };

    const SnapshotView before = snapshot_view(state_->before);
    const SnapshotView after = snapshot_view(state_->after);

    std::vector<EventEntry> ev_copy;
    {
        std::lock_guard lock(state_->events_mx);
        ev_copy = state_->events;
    }

    for (std::size_t row = 0; row < main_h; ++row) {
        std::string left_text;
        switch (static_cast<SnapshotRow>(row)) {
            case SnapshotRow::BeforeHeading: left_text = " BEFORE ACTION"; break;
            case SnapshotRow::BeforeValues: left_text = std::format(" val: VM 0x{:08X} / PHYS 0x{:08X}", before.vmalloc_value, before.phys_value); break;
            case SnapshotRow::BeforePageTable: left_text = std::format(" attack PT: {} {} 0x{:016X}", before.mapping_level, before.writable ? "RW" : "RO", before.mapping_entry); break;
            case SnapshotRow::BeforeProtection: left_text = std::format(" CTX: {}  SMMU: {}  {}", before.ctx_protected ? "ON" : "OFF", before.smmu_active ? "ON" : "OFF", before.status); break;
            case SnapshotRow::AfterHeading: left_text = " AFTER ACTION"; break;
            case SnapshotRow::AfterValues: left_text = std::format(" val: VM 0x{:08X} / PHYS 0x{:08X}", after.vmalloc_value, after.phys_value); break;
            case SnapshotRow::AfterPageTable: left_text = std::format(" attack PT: {} {} 0x{:016X}", after.mapping_level, after.writable ? "RW" : "RO", after.mapping_entry); break;
            case SnapshotRow::AfterProtection: left_text = std::format(" CTX: {}  SMMU: {}  {}", after.ctx_protected ? "ON" : "OFF", after.smmu_active ? "ON" : "OFF", after.status); break;
            default: break;
        }

        std::string right_text;
        if (row < ev_copy.size()) {
            const auto& ev = ev_copy[ev_copy.size() - 1 - row];
            right_text = std::format(" [{}] {}: {}", ev.timestamp, ev.source, ev.message);
        }

        out.append("│");
        out.append(pad_line(left_text, left_w));
        out.append("│");
        out.append(pad_line(right_text, right_w));
        out.append("│\n");
    }

    // Divider line above bottom status bar: ├──────┴──────┤
    out.append("├" + make_hline("─", left_w) + "┴" + make_hline("─", right_w) + "┤\n");

    // Bottom Status Bar
    std::string scenario_bar;
    {
        std::lock_guard lock(state_->scenario_mx);
        scenario_bar = std::format(" Active: {} — {}", state_->scenario_info.id, state_->scenario_info.title);
    }
    const std::size_t total_inner_w = left_w + right_w + 1;
    out.append("│" + pad_line(scenario_bar, total_inner_w) + "│\n");

    // Bottom border: └─────────────┘
    out.append("└" + make_hline("─", total_inner_w) + "┘\n");
}

} // namespace safety::monitor
