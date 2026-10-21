/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DEMO_CONSTANTS_H
#define _DEMO_CONSTANTS_H

#define DEMO_PROC_CONTROL_MODE          0666
#define DEMO_PROC_READ_ONLY_MODE        0444
#define DEMO_MODULE_PARAM_MODE          0644
#define DEMO_COMMAND_BUFFER_SIZE        16
#define DEMO_RESULT_BUFFER_SIZE         32
#define DEMO_DEVICE_NAME_SIZE           32
#define DEMO_ACTION_NAME_SIZE           16
#define DEMO_LOG_CAPACITY               128
#define DEMO_MUTEX_OWNER_FLAGS_MASK     0x07UL
#define DEMO_ROGUE_STATUS_PROC_NAME     "rogue_thread_status"

#define SAFETY_SENTINEL                 0x5AFE1234U
#define CORRUPT_SENTINEL                0xDEADDEADU

#define BAD_DRIVER_VMALLOC_VALUE        0xBAD10001U
#define BAD_DRIVER_MUTEX_BYPASS_VALUE   0xBAD20002U
#define BAD_DRIVER_LINEAR_ALIAS_VALUE   0xBAD30003U

#define MUTEX_THREAD_A_PERIOD_MS        500U
#define MUTEX_THREAD_B_PERIOD_MS        1000U
#define MUTEX_HOLD_DURATION_MS          50U
#define ROGUE_WRITE_INTERVAL_MS         300U

#define SMMU_DEMO_STREAM_ID             0x01U

#define DEMO_PROTECT_COMMAND            "protect"
#define DEMO_UNPROTECT_COMMAND          "unprotect"
#define DEMO_ENABLE_COMMAND             "1"
#define DEMO_DISABLE_COMMAND            "0"

enum bad_driver_attack_mode {
	BAD_DRIVER_ATTACK_VMALLOC = 1,
	BAD_DRIVER_ATTACK_MUTEX_BYPASS = 2,
	BAD_DRIVER_ATTACK_LINEAR_ALIAS = 3,
};

enum rogue_attack_mode {
	ROGUE_ATTACK_UNSYNCHRONIZED_WRITE = 0,
	ROGUE_ATTACK_MUTEX_METADATA = 1,
};

#endif /* _DEMO_CONSTANTS_H */
