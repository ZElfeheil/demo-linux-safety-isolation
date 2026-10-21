#include "devmem/phys_view.hpp"
#include <atomic>
#include <csignal>
#include <exception>
#include <format>
#include <iostream>
#include <string_view>

namespace {
constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int kProgramArgIndex = 0;
constexpr int kSubcommandArgIndex = 1;
constexpr int kAddressArgIndex = 2;
constexpr int kValueOrIntervalArgIndex = 3;
constexpr int kArgsWithSubcommand = 2;
constexpr int kArgsWithAddress = 3;
constexpr int kArgsWithValueOrInterval = 4;

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
    if (argc < kArgsWithAddress) {
        std::cerr << "Error: 'read' subcommand requires a physical address argument.\n";
        print_usage(argv[kProgramArgIndex]);
        return kExitFailure;
    }

    auto addr_res = safety::devmem::parse_address(argv[kAddressArgIndex]);
    if (!addr_res) {
        std::cerr << std::format("Error: {}\n", addr_res.error());
        return kExitFailure;
    }

    auto read_res = safety::devmem::read_phys_32(*addr_res);
    if (!read_res) {
        std::cerr << std::format("Read Error: {}\n", read_res.error());
        return kExitFailure;
    }

    std::cout << std::format("0x{:08X}\n", *read_res);
    return kExitSuccess;
}

int handle_write_cmd(int argc, const char* const* argv) {
    if (argc < kArgsWithValueOrInterval) {
        std::cerr << "Error: 'write' subcommand requires physical address and value arguments.\n";
        print_usage(argv[kProgramArgIndex]);
        return kExitFailure;
    }

    auto addr_res = safety::devmem::parse_address(argv[kAddressArgIndex]);
    if (!addr_res) {
        std::cerr << std::format("Error: {}\n", addr_res.error());
        return kExitFailure;
    }

    auto val_res = safety::devmem::parse_value32(argv[kValueOrIntervalArgIndex]);
    if (!val_res) {
        std::cerr << std::format("Error: {}\n", val_res.error());
        return kExitFailure;
    }

    auto write_res = safety::devmem::write_phys_32(*addr_res, *val_res);
    if (!write_res) {
        std::cerr << std::format("Write Error: {}\n", write_res.error());
        return kExitFailure;
    }

    std::cout << std::format("Wrote 0x{:08X} to 0x{:08X}\n", *val_res, *addr_res);
    return kExitSuccess;
}

int handle_watch_cmd(int argc, const char* const* argv) {
    if (argc < kArgsWithAddress) {
        std::cerr << "Error: 'watch' subcommand requires a physical address argument.\n";
        print_usage(argv[kProgramArgIndex]);
        return kExitFailure;
    }

    auto addr_res = safety::devmem::parse_address(argv[kAddressArgIndex]);
    if (!addr_res) {
        std::cerr << std::format("Error: {}\n", addr_res.error());
        return kExitFailure;
    }

    safety::devmem::WatchOptions options;
    options.phys_addr = *addr_res;
    options.interval = safety::devmem::kDefaultWatchInterval;

    if (argc >= kArgsWithValueOrInterval) {
        try {
            int ms = std::stoi(argv[kValueOrIntervalArgIndex]);
            if (ms > 0) {
                options.interval = std::chrono::milliseconds(ms);
            }
        } catch (const std::exception& e) {
            std::cerr << std::format("Warning: Invalid interval '{}', using default {}ms ({})\n",
                                     argv[kValueOrIntervalArgIndex],
                                     safety::devmem::kDefaultWatchInterval.count(), e.what());
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    auto watch_res = safety::devmem::watch_phys_32(options, g_stop_requested);
    if (!watch_res) {
        std::cerr << std::format("Watch Error: {}\n", watch_res.error());
        return kExitFailure;
    }

    return kExitSuccess;
}
} // namespace

// cppcheck-suppress constParameter
int main(int argc, char* argv[]) {
    if (argc < kArgsWithSubcommand) {
        print_usage(argv[kProgramArgIndex]);
        return kExitFailure;
    }

    const std::string_view subcommand = argv[kSubcommandArgIndex];
    if (subcommand == "-h" || subcommand == "--help" || subcommand == "help") {
        print_usage(argv[kProgramArgIndex]);
        return kExitSuccess;
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
    print_usage(argv[kProgramArgIndex]);
    return kExitFailure;
}
