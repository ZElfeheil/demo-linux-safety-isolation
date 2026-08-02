#include "phys_view.hpp"
#include <atomic>
#include <csignal>
#include <exception>
#include <format>
#include <iostream>
#include <string_view>

namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> g_stop_requested{false};

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_stop_requested.store(true);
    }
}

void print_usage(std::string_view prog_name) {
    std::cout << std::format(
        "ARM64 Physical Memory Inspector (`devmem`)\n"
        "Usage: {} <subcommand> [args...]\n\n"
        "Subcommands:\n"
        "  read  <phys_addr>            Read 32-bit hex value from physical memory address\n"
        "  write <phys_addr> <value>   Write 32-bit hex value to physical memory address\n"
        "  watch <phys_addr> [ms]      Poll physical memory address every 100ms (or specified ms),\n"
        "                              highlighting live changes\n\n"
        "Examples:\n"
        "  {} read 0x40001000\n"
        "  {} write 0x40001000 0xDEADDEAD\n"
        "  {} watch 0x40001000\n",
        prog_name, prog_name, prog_name, prog_name);
}

int handle_read_cmd(int argc, const char* const* argv) {
    if (argc < 3) {
        std::cerr << "Error: 'read' subcommand requires a physical address argument.\n";
        print_usage(argv[0]);
        return 1;
    }

    auto addr_res = safety::devmem::parse_address(argv[2]);
    if (!addr_res) {
        std::cerr << std::format("Error: {}\n", addr_res.error());
        return 1;
    }

    auto read_res = safety::devmem::read_phys_32(*addr_res);
    if (!read_res) {
        std::cerr << std::format("Read Error: {}\n", read_res.error());
        return 1;
    }

    std::cout << std::format("0x{:08X}\n", *read_res);
    return 0;
}

int handle_write_cmd(int argc, const char* const* argv) {
    if (argc < 4) {
        std::cerr << "Error: 'write' subcommand requires physical address and value arguments.\n";
        print_usage(argv[0]);
        return 1;
    }

    auto addr_res = safety::devmem::parse_address(argv[2]);
    if (!addr_res) {
        std::cerr << std::format("Error: {}\n", addr_res.error());
        return 1;
    }

    auto val_res = safety::devmem::parse_value32(argv[3]);
    if (!val_res) {
        std::cerr << std::format("Error: {}\n", val_res.error());
        return 1;
    }

    auto write_res = safety::devmem::write_phys_32(*addr_res, *val_res);
    if (!write_res) {
        std::cerr << std::format("Write Error: {}\n", write_res.error());
        return 1;
    }

    std::cout << std::format("Wrote 0x{:08X} to 0x{:08X}\n", *val_res, *addr_res);
    return 0;
}

int handle_watch_cmd(int argc, const char* const* argv) {
    if (argc < 3) {
        std::cerr << "Error: 'watch' subcommand requires a physical address argument.\n";
        print_usage(argv[0]);
        return 1;
    }

    auto addr_res = safety::devmem::parse_address(argv[2]);
    if (!addr_res) {
        std::cerr << std::format("Error: {}\n", addr_res.error());
        return 1;
    }

    safety::devmem::WatchOptions options;
    options.phys_addr = *addr_res;
    options.interval = std::chrono::milliseconds(100);

    if (argc >= 4) {
        try {
            int ms = std::stoi(argv[3]);
            if (ms > 0) {
                options.interval = std::chrono::milliseconds(ms);
            }
        } catch (const std::exception& e) {
            std::cerr << std::format("Warning: Invalid interval '{}', using default 100ms ({})\n", argv[3], e.what());
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    auto watch_res = safety::devmem::watch_phys_32(options, g_stop_requested);
    if (!watch_res) {
        std::cerr << std::format("Watch Error: {}\n", watch_res.error());
        return 1;
    }

    return 0;
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string_view subcommand = argv[1];
    if (subcommand == "-h" || subcommand == "--help" || subcommand == "help") {
        print_usage(argv[0]);
        return 0;
    }

    const char* const* const_argv = argv;

    if (subcommand == "read") {
        return handle_read_cmd(argc, const_argv);
    }

    if (subcommand == "write") {
        return handle_write_cmd(argc, const_argv);
    }

    if (subcommand == "watch") {
        return handle_watch_cmd(argc, const_argv);
    }

    std::cerr << std::format("Error: Unknown subcommand '{}'\n", subcommand);
    print_usage(argv[0]);
    return 1;
}
