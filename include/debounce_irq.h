#ifndef LH_DEBOUNCE_IRQ_H
#define LH_DEBOUNCE_IRQ_H

#include "gpio_types.h"
#include <stddef.h>

/* Kuhn Digital Integrator Filter API */
void debounce_filter_init(debounce_filter_t *filter, uint32_t debounce_time_us, uint32_t sample_interval_us, uint8_t initial_level);
void debounce_filter_reset(debounce_filter_t *filter);
uint8_t debounce_filter_step(debounce_filter_t *filter, uint8_t raw_level, gpio_edge_t *edge_out);

/* Simulation API */
int run_debounce_simulation(const bounce_trace_t *trace, uint32_t debounce_us, uint32_t sample_us, debounce_sim_result_t *result);
int generate_synthetic_bounce_trace(uint32_t total_duration_us, uint32_t bounce_duration_us, uint32_t sample_us, bounce_trace_t *trace);

/* IRQ Storm Limiter API */
void irq_storm_init(irq_storm_limiter_t *limiter, uint32_t max_rate_hz, uint32_t cooldown_ms, uint32_t window_ms);
bool irq_storm_process_event(irq_storm_limiter_t *limiter, uint64_t timestamp_us, bool *is_locked_out);

/* Optocoupler Saturation Calculator API */
int calc_optocoupler_saturation(const opto_params_t *params, opto_result_t *result);

/* Device-Tree Overlay Synthesizer API */
int generate_dtoverlay(const dtoverlay_opts_t *opts, char *out_buf, size_t max_len);

#endif /* LH_DEBOUNCE_IRQ_H */
