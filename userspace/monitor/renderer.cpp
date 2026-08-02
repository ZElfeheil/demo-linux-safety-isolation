#include "renderer.hpp"
#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>

namespace safety::monitor {

namespace {

constexpr uint16_t kDefaultCols = 80;
constexpr uint16_t kDefaultRows = 24;
constexpr uint16_t kMinCols = 40;
constexpr uint16_t kMinRows = 12;
constexpr std::size_t kMinSideW = 18;
constexpr std::size_t kFallbackCardW = 38;

std::size_t visual_len(std::string_view s) noexcept {
    std::size_t len = 0;
    for (std::size_t i = 0; i < s.size(); ) {
        auto c = static_cast<unsigned char>(s[i]);
        if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
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
            if ((c & 0xE0) == 0xC0) byte_idx += 2;
            else if ((c & 0xF0) == 0xE0) byte_idx += 3;
            else if ((c & 0xF8) == 0xF0) byte_idx += 4;
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
    return TerminalSize{kDefaultCols, kDefaultRows};
}

void Renderer::render(std::string& out_buf, TerminalSize size) const {
    out_buf.clear();
    out_buf.append("\033[H"); // Cursor home (top-left)

    if (state_ == nullptr) return;

    switch (state_->mode.load()) {
        case DashboardMode::Normal:
            render_normal(out_buf, size);
            break;
        case DashboardMode::Paused:
            render_paused(out_buf, size);
            break;
        case DashboardMode::Revealed:
            render_revealed(out_buf, size);
            break;
    }
}

void Renderer::render_normal(std::string& out, TerminalSize size) const {
    if (state_ == nullptr) return;

    const uint16_t cols = (size.cols < kMinCols) ? kMinCols : size.cols;
    const uint16_t rows = (size.rows < kMinRows) ? kMinRows : size.rows;

    const std::size_t left_w = (cols / 2 > 2) ? ((cols / 2) - 1) : kMinSideW;
    const std::size_t right_w = (cols > left_w + 3) ? (cols - left_w - 3) : kMinSideW;
    const auto main_h = static_cast<std::size_t>(rows - 4);

    // Header line: ┌─ SAFETY MEMORY STATE ─┬─ KERNEL EVENT STREAM ─┐
    const std::string left_title = " SAFETY MEMORY STATE ";
    const std::string right_title = " KERNEL EVENT STREAM ";

    const std::size_t l_fill = (left_w > visual_len(left_title)) ? (left_w - visual_len(left_title)) : 0;
    const std::size_t r_fill = (right_w > visual_len(right_title)) ? (right_w - visual_len(right_title)) : 0;

    std::string top_hdr = "┌" + make_hline("─", l_fill / 2) + left_title + make_hline("─", l_fill - l_fill / 2)
                        + "┬" + make_hline("─", r_fill / 2) + right_title + make_hline("─", r_fill - r_fill / 2) + "┐\n";
    out.append(top_hdr);

    // Snapshot state
    const uint32_t vm_val = state_->val_vmalloc.load();
    const uint32_t ph_val = state_->val_phys.load();
    const uint64_t vaddr = state_->virt_addr.load();
    const uint64_t paddr = state_->phys_addr.load();
    const bool ctx_on = state_->ctx_protected.load();
    const bool smmu_on = state_->smmu_active.load();

    std::string m_owner;
    std::string status;
    {
        std::lock_guard lock(state_->status_mx);
        m_owner = state_->mutex_owner;
        status = state_->status_str;
    }

    const bool is_corrupt = (vm_val != ph_val) || (status == "CORRUPTED");

    std::vector<EventEntry> ev_copy;
    {
        std::lock_guard lock(state_->events_mx);
        ev_copy = state_->events;
    }

    for (std::size_t row = 0; row < main_h; ++row) {
        std::string left_text;
        if (row == 0) left_text = std::format(" virt  0x{:016x}", vaddr);
        else if (row == 1) left_text = std::format(" phys  0x{:016x}", paddr);
        else if (row == 3) left_text = std::format(" via vmalloc: 0x{:08X}  {}", vm_val, (vm_val == 0x5AFE1234 ? "[OK]" : "[FAIL]"));
        else if (row == 4) left_text = std::format(" via phys:    0x{:08X}  {}", ph_val, (ph_val == 0x5AFE1234 ? "[OK]" : "[FAIL]"));
        else if (row == 6) left_text = std::format(" CTX protect: {}", ctx_on ? "ON" : "OFF");
        else if (row == 7) left_text = std::format(" SMMU:        {}", smmu_on ? "ON" : "OFF");
        else if (row == 8) left_text = std::format(" Mutex Owner: {}", m_owner);
        else if (row == 9) left_text = std::format(" Status:      {}", is_corrupt ? "CORRUPTED" : status);

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

void Renderer::render_paused(std::string& out, TerminalSize size) const {
    if (state_ == nullptr) return;

    ScenarioInfo s_info;
    {
        std::lock_guard lock(state_->scenario_mx);
        s_info = state_->scenario_info;
    }

    const std::size_t w = (size.cols > 4) ? static_cast<std::size_t>(size.cols - 2) : kFallbackCardW;
    const std::string header_title = std::format(" ⏸  {} — {} ", s_info.id, s_info.title);
    const std::size_t fill = (w > visual_len(header_title)) ? (w - visual_len(header_title)) : 0;

    out.append("┌" + make_hline("─", fill / 2) + header_title + make_hline("─", fill - fill / 2) + "┐\n");
    out.append("│" + pad_line(" SETUP:", w) + "│\n");
    out.append("│" + pad_line("   " + s_info.setup_text, w) + "│\n");
    out.append("│" + pad_line("", w) + "│\n");
    out.append("│" + pad_line(" ❓ QUESTION: " + s_info.question_text, w) + "│\n");
    out.append("│" + pad_line("", w) + "│\n");

    for (const auto& opt : s_info.options) {
        out.append("│" + pad_line("    " + opt, w) + "│\n");
    }

    out.append("│" + pad_line("", w) + "│\n");

    const std::string banner = "[ AWAITING PRESENTER ]";
    const std::size_t b_fill = (w > visual_len(banner)) ? (w - visual_len(banner)) : 0;
    out.append("│" + make_hline(" ", b_fill / 2) + banner + make_hline(" ", b_fill - b_fill / 2) + "│\n");

    out.append("└" + make_hline("─", w) + "┘\n");
}

void Renderer::render_revealed(std::string& out, TerminalSize size) const {
    if (state_ == nullptr) return;

    ScenarioInfo s_info;
    {
        std::lock_guard lock(state_->scenario_mx);
        s_info = state_->scenario_info;
    }

    const std::size_t w = (size.cols > 4) ? static_cast<std::size_t>(size.cols - 2) : kFallbackCardW;
    const std::string header_title = std::format(" ✓  {} — REVEALED   Answer: {} ✓ ", s_info.id, s_info.correct_option);
    const std::size_t fill = (w > visual_len(header_title)) ? (w - visual_len(header_title)) : 0;

    out.append("┌" + make_hline("─", fill / 2) + header_title + make_hline("─", fill - fill / 2) + "┐\n");

    const uint32_t vm_val = state_->val_vmalloc.load();
    const uint32_t ph_val = state_->val_phys.load();

    const std::string state_summary = std::format("  MEMORY DIVERGENCE: via vmalloc=0x{:08X} | via phys=0x{:08X}", vm_val, ph_val);
    out.append("│" + pad_line(state_summary, w) + "│\n");
    out.append("├" + make_hline("─", w) + "┤\n");
    out.append("│" + pad_line(" EXPLANATION:", w) + "│\n");

    // Split multi-line explanation by newline
    std::size_t pos = 0;
    const std::string exp = s_info.explanation;
    while (pos < exp.size()) {
        const std::size_t next = exp.find('\n', pos);
        const std::string line_str = (next == std::string::npos) ? exp.substr(pos) : exp.substr(pos, next - pos);
        out.append("│" + pad_line("   " + line_str, w) + "│\n");
        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }

    out.append("└" + make_hline("─", w) + "┘\n");
}

} // namespace safety::monitor
