#include "monitor/renderer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cassert>

using namespace safety::monitor;

std::vector<std::string> split_lines(const std::string& str) {
    std::vector<std::string> lines;
    std::stringstream ss(str);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::size_t visual_len(std::string_view s) noexcept {
    std::size_t len = 0;
    for (std::size_t i = 0; i < s.size(); ) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else i += 1;
        len += 1;
    }
    return len;
}

void test_geometry(const Renderer& renderer, DisplayState& state, uint16_t cols, uint16_t rows, DashboardMode mode, const std::string& mode_name) {
    state.mode.store(mode);
    std::string out;
    renderer.render(out, TerminalSize{cols, rows});

    auto lines = split_lines(out);
    std::cout << "--- Testing mode: " << mode_name << " | Terminal size: " << cols << "x" << rows << " ---" << std::endl;
    std::cout << "Total lines output: " << lines.size() << " (Requested height: " << rows << ")" << std::endl;

    bool height_overflow = lines.size() > rows;
    if (height_overflow) {
        std::cout << "  [FAIL/BUG] HEIGHT OVERFLOW! Output " << lines.size() << " lines, but terminal height is " << rows << "! (Will cause scrolling & flicker)" << std::endl;
    }

    std::size_t line_idx = 0;
    bool length_mismatch = false;
    std::vector<std::size_t> vlens;

    for (const auto& l : lines) {
        std::string_view lview = l;
        if (lview.starts_with("\033[H")) lview.remove_prefix(3);
        std::size_t v = visual_len(lview);
        vlens.push_back(v);
    }

    if (!vlens.empty()) {
        std::size_t first_len = vlens[0];
        for (std::size_t i = 0; i < vlens.size(); ++i) {
            if (vlens[i] != first_len) {
                length_mismatch = true;
                std::cout << "  [FAIL/BUG] LINE LENGTH MISMATCH! Line " << i << " has visual len " << vlens[i] << " (Line 0 has " << first_len << ")" << std::endl;
            }
        }
    }

    if (!lines.empty()) {
        std::cout << "  Sample Line 0: " << lines[0] << " [vlen=" << vlens[0] << "]" << std::endl;
        if (lines.size() > 1) {
            std::cout << "  Sample Line 1: " << lines[1] << " [vlen=" << vlens[1] << "]" << std::endl;
        }
    }
}

int main() {
    DisplayState state;
    Renderer renderer(state);

    std::cout << "================ TERMINAL GEOMETRY STRESS TEST ================" << std::endl;

    // Test 1: Normal mode under various geometries
    test_geometry(renderer, state, 80, 24, DashboardMode::Normal, "Normal (80x24)");
    test_geometry(renderer, state, 40, 12, DashboardMode::Normal, "Normal (40x12)");
    test_geometry(renderer, state, 45, 12, DashboardMode::Normal, "Normal (45x12)");
    test_geometry(renderer, state, 20, 10, DashboardMode::Normal, "Normal (20x10 - Small)");
    test_geometry(renderer, state, 200, 60, DashboardMode::Normal, "Normal (200x60 - Large)");
    test_geometry(renderer, state, 0, 0, DashboardMode::Normal, "Normal (0x0 - Minimal Edge)");

    // Test 2: Paused mode under various geometries
    test_geometry(renderer, state, 80, 24, DashboardMode::Paused, "Paused (80x24)");
    test_geometry(renderer, state, 40, 12, DashboardMode::Paused, "Paused (40x12)");
    test_geometry(renderer, state, 20, 10, DashboardMode::Paused, "Paused (20x10 - Small)");

    // Test 3: Revealed mode under various geometries
    test_geometry(renderer, state, 80, 24, DashboardMode::Revealed, "Revealed (80x24)");
    test_geometry(renderer, state, 40, 12, DashboardMode::Revealed, "Revealed (40x12)");
    test_geometry(renderer, state, 20, 10, DashboardMode::Revealed, "Revealed (20x10 - Small)");

    return 0;
}
