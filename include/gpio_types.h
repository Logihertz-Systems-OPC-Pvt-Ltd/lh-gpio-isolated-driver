#ifndef LH_GPIO_TYPES_H
#define LH_GPIO_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define LH_GPIO_MAX_RULES 10
#define LH_GPIO_MAX_NAME_LEN 64
#define LH_GPIO_MAX_DESC_LEN 256
#define LH_GPIO_MAX_CHANNELS 32
#define LH_GPIO_MAX_TRACE_SAMPLES 65536

typedef enum {
    SEV_INFO = 0,
    SEV_LOW = 1,
    SEV_MEDIUM = 4,
    SEV_HIGH = 10,
    SEV_CRITICAL = 25
} severity_t;

typedef enum {
    EDGE_NONE = 0,
    EDGE_RISING = 1,
    EDGE_FALLING = 2,
    EDGE_BOTH = 3
} gpio_edge_t;

typedef enum {
    IRQ_NORMAL = 0,
    IRQ_MASKED_STORM_LOCKOUT = 1
} irq_storm_state_t;

/* Policy Configuration Structure for 10-Rule Auditor */
typedef struct {
    char policy_name[LH_GPIO_MAX_NAME_LEN];
    uint32_t galvanic_isolation_vrms;
    bool shared_field_logic_ground;
    int32_t irq_storm_limit_hz;
    bool threaded_oneshot_irq;
    bool legacy_sysfs_gpio_enabled;
    uint32_t chardev_permissions_octal;
    bool debounce_enabled;
    uint32_t debounce_window_us;
    bool failsafe_output_state_on_fault;
    bool isolated_vcc2_uvlo_monitor;
    double optocoupler_ctr_saturation_ratio;
    bool led_aging_derating_applied;
    bool atomic_edge_counters;
    bool chardev_exclusive_open;
    double isolation_creepage_mm;
    bool tvs_field_surge_protection;
    bool strict_ioctl_validation;
    bool lock_channel_direction_in_dt;
    bool monotonic_raw_edge_timestamps;
    bool log_irq_storm_events;
} gpio_policy_t;

/* Audit Finding for individual rules */
typedef struct {
    char rule_id[16];
    char rule_name[LH_GPIO_MAX_NAME_LEN];
    char cwe_id[16];
    severity_t severity;
    uint32_t deduction;
    bool passed;
    char details[LH_GPIO_MAX_DESC_LEN];
    char recommendation[LH_GPIO_MAX_DESC_LEN];
} audit_finding_t;

/* Audit Report Summary */
typedef struct {
    char policy_name[LH_GPIO_MAX_NAME_LEN];
    uint32_t total_rules;
    uint32_t passed_rules;
    uint32_t failed_rules;
    uint32_t total_deductions;
    uint32_t posture_score;
    audit_finding_t findings[LH_GPIO_MAX_RULES];
} audit_report_t;

/* Kuhn Digital Integrator Debounce Filter State */
typedef struct {
    uint32_t c_max;              /* Maximum saturation counter = ceil(T_deb / dt) */
    uint32_t counter;            /* Current saturation counter [0, c_max] */
    uint8_t state;               /* Debounced output state (0 or 1) */
    uint8_t prev_raw;            /* Previous raw input level */
    uint32_t debounce_time_us;   /* Debounce period in microseconds */
    uint32_t sample_interval_us; /* Sampling interval in microseconds */
    uint64_t raw_rising_edges;   /* Raw input transitions */
    uint64_t raw_falling_edges;
    uint64_t debounced_rising;   /* Filtered valid transitions */
    uint64_t debounced_falling;
    uint64_t glitches_rejected;  /* Transient bounce reversals filtered out */
} debounce_filter_t;

/* Contact Bounce Simulation Trace */
typedef struct {
    uint32_t sample_count;
    uint32_t sample_interval_us;
    uint8_t samples[LH_GPIO_MAX_TRACE_SAMPLES];
    uint64_t timestamps_us[LH_GPIO_MAX_TRACE_SAMPLES];
} bounce_trace_t;

/* Simulation Result Metrics */
typedef struct {
    uint32_t total_samples;
    double total_duration_ms;
    uint64_t raw_rising_count;
    uint64_t raw_falling_count;
    uint64_t debounced_rising_count;
    uint64_t debounced_falling_count;
    uint64_t glitches_rejected;
    uint8_t final_output_state;
    double noise_reduction_ratio; /* (raw_edges - debounced_edges) / max(1, raw_edges) */
} debounce_sim_result_t;

/* Optocoupler Saturation Parameters */
typedef struct {
    double vin_v;             /* Field input voltage (e.g. 24.0 V) */
    double rlimit_ohm;        /* Input series limiting resistor (e.g. 2200 Ohm) */
    double vf_v;              /* LED forward voltage (e.g. 1.25 V) */
    double ctr_min_percent;   /* Datasheet minimum CTR % (e.g. 100%) */
    double aging_factor;      /* LED aging/thermal derating factor (e.g. 0.70) */
    double vdd_v;             /* Logic domain supply voltage (e.g. 3.3 V) */
    double vce_sat_v;         /* Phototransistor saturation voltage (e.g. 0.20 V) */
    double rpull_ohm;         /* Logic pull-up resistor (e.g. 4700 Ohm) */
    double opto_cap_pf;       /* Phototransistor output capacitance (e.g. 15 pF) */
} opto_params_t;

/* Optocoupler Saturation Calculation Results */
typedef struct {
    double if_ma;                /* Forward LED current (mA) */
    double ic_available_ma;      /* Available collector current with derating (mA) */
    double ic_required_ma;       /* Required collector current to pull down logic (mA) */
    double saturation_ratio;     /* Overdrive saturation ratio (I_c_avail / I_c_req) */
    double rlimit_power_mw;      /* Power dissipation in series resistor (mW) */
    double est_propagation_us;   /* Estimated propagation delay (us) */
    bool is_saturated;           /* Saturation ratio >= 1.50 */
    bool rlimit_power_safe;      /* True if <= 250mW (standard 1/4W resistor) */
    char safety_verdict[LH_GPIO_MAX_NAME_LEN];
} opto_result_t;

/* IRQ Storm Limiter State */
typedef struct {
    uint32_t max_rate_hz;
    uint32_t cooldown_ms;
    uint32_t window_ms;
    uint64_t window_start_us;
    uint32_t irq_count_in_window;
    irq_storm_state_t state;
    uint64_t lockout_until_us;
    uint64_t storm_lockout_events;
    uint64_t total_irqs_processed;
    uint64_t total_irqs_dropped;
} irq_storm_limiter_t;

/* Device Tree Overlay Options */
typedef struct {
    char node_name[LH_GPIO_MAX_NAME_LEN];
    char compatible[LH_GPIO_MAX_NAME_LEN];
    uint32_t channel_count;
    uint32_t isolation_vrms;
    uint32_t debounce_delay_us;
    uint32_t max_irq_rate_hz;
    uint32_t cooldown_ms;
    char failsafe_state[16];
    uint32_t chardev_mode_octal;
} dtoverlay_opts_t;

#endif /* LH_GPIO_TYPES_H */
