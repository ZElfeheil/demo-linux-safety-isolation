#ifndef USERSPACE_COMMON_DEMO_CONSTANTS_HPP
#define USERSPACE_COMMON_DEMO_CONSTANTS_HPP

#include <chrono>
#include <cstdint>
#include <string_view>

namespace safety::demo {

inline constexpr std::uint32_t kSafetySentinel = 0x5AFE1234U;
inline constexpr std::uint32_t kCorruptSentinel = 0xDEADDEADU;
inline constexpr std::string_view kSafetySentinelStatus = "0x5AFE1234";
inline constexpr std::string_view kCorruptSentinelStatus = "0xDEADDEAD";
inline constexpr std::string_view kLinearBypassStatus = "0xBAD30003";

inline constexpr std::chrono::milliseconds kAttackSettleTime{100};
inline constexpr std::chrono::milliseconds kThreadAttackObservationTime{300};
inline constexpr std::chrono::milliseconds kMetadataAttackObservationTime{1000};
inline constexpr std::chrono::milliseconds kAttackStatusPollInterval{10};
inline constexpr std::chrono::milliseconds kMonitorSnapshotSettleTime{300};
inline constexpr std::chrono::milliseconds kResultDisplayTime{1500};
inline constexpr std::uint64_t kIllustrativeTlbFlushLatencyNs = 1500;

inline constexpr std::string_view kVmallocAttackCommand = "1";
inline constexpr std::string_view kLinearMapAttackCommand = "3";
inline constexpr std::string_view kEnableProtectionCommand = "protect";
inline constexpr std::string_view kRogueWriteModuleParams =
    "attack_mode=0 interval_ms=100";
inline constexpr std::string_view kRogueMetadataModuleParams =
    "attack_mode=1 interval_ms=100";
inline constexpr std::string_view kRogueStatusPath = "/proc/rogue_thread_status";
inline constexpr std::string_view kMetadataAttackCountField = "metadata_attack_count:";
inline constexpr std::string_view kProtectionEnabledStatus = "ctx_protected: 1";
inline constexpr std::string_view kVmallocReadOnlyStatus =
    "mapping_writable_vmalloc: 0";

} // namespace safety::demo

#endif // USERSPACE_COMMON_DEMO_CONSTANTS_HPP
