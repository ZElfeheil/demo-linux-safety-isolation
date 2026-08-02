#ifndef USERSPACE_HARNESS_MODULE_LOADER_HPP
#define USERSPACE_HARNESS_MODULE_LOADER_HPP

#include <csignal>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include "common/expected.hpp"

namespace safety {

class ModuleLoader {
public:
    ModuleLoader() = default;
    ~ModuleLoader();

    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

    ModuleLoader(ModuleLoader&& other) noexcept;
    ModuleLoader& operator=(ModuleLoader&& other) noexcept;

    auto load(const std::filesystem::path& module_path, const std::string& params = "")
        -> safety::expected<void, std::string>;

    auto unload(const std::string& module_name) -> safety::expected<void, std::string>;

    void unload_all();

    [[nodiscard]] bool is_loaded(const std::string& module_name) const noexcept;

    static void setup_signal_handlers();
    static bool was_signaled() noexcept;
    static void cleanup_on_signal();

private:
    struct LoadedModule {
        std::string name;
        std::filesystem::path path;
    };

    std::vector<LoadedModule> loaded_modules_;
    mutable std::mutex mutex_;

    static void register_instance(ModuleLoader* instance);
    static void unregister_instance(ModuleLoader* instance);
    static void handle_signal(int signal);
};

} // namespace safety

#endif // USERSPACE_HARNESS_MODULE_LOADER_HPP
