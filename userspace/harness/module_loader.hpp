#ifndef USERSPACE_HARNESS_MODULE_LOADER_HPP
#define USERSPACE_HARNESS_MODULE_LOADER_HPP

#include <csignal>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include "common/expected.hpp"

namespace safety {

/** RAII manager for loading and unloading the demo's kernel modules. */
class ModuleLoader {
public:
    ModuleLoader() = default;
    ~ModuleLoader();

    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

    ModuleLoader(ModuleLoader&& other) noexcept;
    ModuleLoader& operator=(ModuleLoader&& other) noexcept;

    /**
     * @brief Load a kernel module and record it for later cleanup.
     * @param module_path Path to the `.ko` file.
     * @param params Optional module parameter string passed to the kernel.
     * @return Success, or the loader error produced by the operating system.
     */
    auto load(const std::filesystem::path& module_path, const std::string& params = "")
        -> safety::expected<void, std::string>;

    /**
     * @brief Unload a previously loaded module by name.
     * @param module_name Kernel module name without the `.ko` suffix.
     * @return Success, or an error if the module is not loaded or cannot unload.
     */
    auto unload(const std::string& module_name) -> safety::expected<void, std::string>;

    /** Unloads all modules owned by this loader in reverse order. */
    void unload_all();

    /**
     * @brief Check whether a module is currently tracked by this loader.
     * @param module_name Kernel module name to search for.
     * @return true when the module is loaded by this instance; false otherwise.
     */
    [[nodiscard]] bool is_loaded(const std::string& module_name) const noexcept;

    /** Installs process-level signal handling for safe scenario cleanup. */
    static void setup_signal_handlers();
    /** Reports whether a termination signal has been observed. */
    static bool was_signaled() noexcept;
    /** Releases modules registered with the signal-cleanup mechanism. */
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
