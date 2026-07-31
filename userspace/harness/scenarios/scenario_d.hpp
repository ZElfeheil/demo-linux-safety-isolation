#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_D_HPP
#define USERSPACE_HARNESS_SCENARIOS_SCENARIO_D_HPP

#include <string_view>
#include "../../common/expected.hpp"
#include "../../common/scenario.hpp"
#include "../module_loader.hpp"

namespace safety {

class ScenarioD {
public:
    [[nodiscard]] std::string_view name() const noexcept {
        return "Scenario D: DMA Linear Map Bypass";
    }

    auto setup() -> safety::expected<void, std::string>;
    auto run() -> ScenarioResult;
    void teardown();

private:
    ModuleLoader loader_;
};

} // namespace safety

#endif // USERSPACE_HARNESS_SCENARIOS_SCENARIO_D_HPP
