#include "module_loader.hpp"
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <format>
#include <iostream>
#include <unistd.h>

namespace safety {

static std::vector<ModuleLoader*> g_active_loaders;
static std::mutex g_loaders_mutex;
static volatile std::sig_atomic_t g_signal_received = 0;

ModuleLoader::~ModuleLoader() {
    unload_all();
    unregister_instance(this);
}

ModuleLoader::ModuleLoader(ModuleLoader&& other) noexcept {
    std::lock_guard lock(other.mutex_);
    loaded_modules_ = std::move(other.loaded_modules_);
    register_instance(this);
}

ModuleLoader& ModuleLoader::operator=(ModuleLoader&& other) noexcept {
    if (this != &other) {
        unload_all();
        std::lock_guard lock1(mutex_);
        std::lock_guard lock2(other.mutex_);
        loaded_modules_ = std::move(other.loaded_modules_);
    }
    return *this;
}

void ModuleLoader::register_instance(ModuleLoader* instance) {
    std::lock_guard lock(g_loaders_mutex);
    g_active_loaders.push_back(instance);
}

void ModuleLoader::unregister_instance(ModuleLoader* instance) {
    std::lock_guard lock(g_loaders_mutex);
    std::erase(g_active_loaders, instance);
}

auto ModuleLoader::load(const std::filesystem::path& module_path, const std::string& params)
    -> safety::expected<void, std::string> {
    std::lock_guard lock(mutex_);

    std::filesystem::path target_path = module_path;
    if (!std::filesystem::exists(target_path)) {
        // Fallback to local ./modules directory if absolute /lib/modules path doesn't exist
        std::filesystem::path local_alt = std::filesystem::path("modules") / module_path.filename();
        if (std::filesystem::exists(local_alt)) {
            target_path = local_alt;
        } else {
            return safety::unexpected(std::format("Module file not found: {}", module_path.string()));
        }
    }

    std::string module_name = target_path.stem().string();
    if (is_loaded(module_name)) {
        return {};
    }

    std::string cmd = params.empty() ?
        std::format("insmod {}", target_path.string()) :
        std::format("insmod {} {}", target_path.string(), params);

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        return safety::unexpected(std::format("insmod failed for {} (exit code {})", target_path.string(), ret));
    }

    loaded_modules_.push_back({module_name, target_path});
    register_instance(this);
    return {};
}

auto ModuleLoader::unload(const std::string& module_name) -> safety::expected<void, std::string> {
    std::lock_guard lock(mutex_);

    auto it = std::find_if(loaded_modules_.rbegin(), loaded_modules_.rend(),
        [&module_name](const LoadedModule& mod) { return mod.name == module_name; });

    if (it == loaded_modules_.rend()) {
        return {};
    }

    std::string cmd = std::format("rmmod {}", module_name);
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        return safety::unexpected(std::format("rmmod failed for module {} (exit code {})", module_name, ret));
    }

    loaded_modules_.erase(std::next(it).base());
    return {};
}

void ModuleLoader::unload_all() {
    std::lock_guard lock(mutex_);
    while (!loaded_modules_.empty()) {
        auto mod = loaded_modules_.back();
        std::string cmd = std::format("rmmod {}", mod.name);
        int rc = std::system(cmd.c_str());
        (void)rc;
        loaded_modules_.pop_back();
    }
}

bool ModuleLoader::is_loaded(const std::string& module_name) const noexcept {
    return std::any_of(loaded_modules_.begin(), loaded_modules_.end(),
        [&module_name](const LoadedModule& mod) { return mod.name == module_name; });
}

void ModuleLoader::setup_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = ModuleLoader::handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void ModuleLoader::handle_signal(int signal) {
    g_signal_received = signal;
}

bool ModuleLoader::was_signaled() noexcept {
    return g_signal_received != 0;
}

void ModuleLoader::cleanup_on_signal() {
    if (g_signal_received != 0) {
        int sig = g_signal_received;
        g_signal_received = 0;
        std::lock_guard lock(g_loaders_mutex);
        for (auto* loader : g_active_loaders) {
            if (loader) {
                loader->unload_all();
            }
        }
        std::_Exit(128 + sig);
    }
}

} // namespace safety
