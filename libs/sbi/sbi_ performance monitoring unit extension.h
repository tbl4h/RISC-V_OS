#ifndef SBI_PERFORMANCE_MONITORING_UNIT_EXTENSION_H
#define SBI_PERFORMANCE_MONITORING_UNIT_EXTENSION_H
#include "sbi.h"

// : Get number of counters
static inline struct sbiret sbi_pmu_num_counters(void)
{
    return sbi_ecall(
        SBI_EXT_PMU,
        SBI_EXT_PMU_NUM_COUNTERS,
        0, 0, 0, 0, 0, 0);
}

// : Get counter information
static inline struct sbiret sbi_pmu_counter_get_info(unsigned long counter_idx)
{
    return sbi_ecall(
        SBI_EXT_PMU,
        SBI_EXT_PMU_COUNTER_GET_INFO,
        counter_idx, 0, 0, 0, 0, 0);
}

//  Find and configure a matching

static inline struct sbiret sbi_pmu_counter_config_matching(unsigned long counter_idx_base,
                                                            unsigned long counter_idx_mask,
                                                            unsigned long config_flags,
                                                            unsigned long event_idx,
                                                            uint64_t event_data)
{
    return sbi_ecall(
        SBI_EXT_PMU,
        SBI_EXT_PMU_COUNTER_CFG_MATCH,
        counter_idx_base, counter_idx_mask, config_flags, event_idx, event_data, 0);
}

// : Start counters
static inline struct sbiret sbi_pmu_counter_start(unsigned long counter_idx_base,
                                                  unsigned long counter_idx_mask,
                                                  unsigned long start_flags,
                                                  uint64_t initial_value)
{
    return sbi_ecall(
        SBI_EXT_PMU,
        SBI_EXT_PMU_COUNTER_START,
        counter_idx_base, counter_idx_mask, start_flags, initial_value, 0, 0);
}

//  Stop a set of counters
static inline struct sbiret sbi_pmu_counter_stop(unsigned long counter_idx_base,
                                                 unsigned long counter_idx_mask,
                                                 unsigned long stop_flags)
{
    return sbi_ecall(
        SBI_EXT_PMU,
        SBI_EXT_PMU_COUNTER_STOP,
        counter_idx_base, counter_idx_mask, stop_flags, 0, 0, 0);
}

// Read a counter value
static inline struct sbiret sbi_pmu_counter_fw_read(unsigned long counter_idx){
    return sbi_ecall(
        SBI_EXT_PMU,
        SBI_EXT_PMU_COUNTER_FW_READ,
        counter_idx, 0, 0, 0, 0, 0);
}

#endif