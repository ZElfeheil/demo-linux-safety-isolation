#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_F_HPP
#define USERSPACE_HARNESS_SCENARIOS_SCENARIO_F_HPP

#include <string_view>
#include "../../common/expected.hpp"
#include "../../common/scenario.hpp"
#include "../module_loader.hpp"

namespace safety {

class ScenarioF {
public:
    [[nodiscard]] static constexpr std::string_view name() noexcept {
        return "Scenario F: Full CTX + SMMU Isolation";
    }

    auto setup() -> safety::expected<void, std::string>;
    auto run() -> ScenarioResult;
    void teardown();

private:
    ModuleLoader loader_;
};

} // namespace safety

#endif // USERSPACE_HARNESS_SCENARIOS_SCENARIO_F_HPP
