#include "debounce_irq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void debounce_filter_init(debounce_filter_t *filter, uint32_t debounce_time_us, uint32_t sample_interval_us, uint8_t initial_level) {
    if (!filter) return;
    memset(filter, 0, sizeof(debounce_filter_t));

    uint32_t c_max = 1;
    if (sample_interval_us > 0) {
        c_max = (uint32_t)ceil((double)debounce_time_us / (double)sample_interval_us);
    }
    if (c_max == 0) c_max = 1;

    filter->c_max = c_max;
    filter->debounce_time_us = debounce_time_us;
    filter->sample_interval_us = sample_interval_us;
    filter->state = initial_level ? 1 : 0;
    filter->prev_raw = filter->state;
    filter->counter = filter->state ? c_max : 0;
}

void debounce_filter_reset(debounce_filter_t *filter) {
    if (!filter) return;
    filter->counter = filter->state ? filter->c_max : 0;
    filter->prev_raw = filter->state;
    filter->raw_rising_edges = 0;
    filter->raw_falling_edges = 0;
    filter->debounced_rising = 0;
    filter->debounced_falling = 0;
    filter->glitches_rejected = 0;
}

uint8_t debounce_filter_step(debounce_filter_t *filter, uint8_t raw_level, gpio_edge_t *edge_out) {
    if (!filter) {
        if (edge_out) *edge_out = EDGE_NONE;
        return 0;
    }

    uint8_t x = raw_level ? 1 : 0;
    gpio_edge_t detected_edge = EDGE_NONE;

    /* Count raw transitions */
    if (x == 1 && filter->prev_raw == 0) {
        filter->raw_rising_edges++;
    } else if (x == 0 && filter->prev_raw == 1) {
        filter->raw_falling_edges++;
    }
    filter->prev_raw = x;

    /* Kuhn digital integrator counter update */
    if (x == 1) {
        if (filter->counter < filter->c_max) {
            filter->counter++;
        }
        /* Check if reversing towards 1 while state is 0 but not yet reached c_max */
        if (filter->state == 1 && filter->counter > 0 && filter->counter < filter->c_max) {
            filter->glitches_rejected++;
        }
    } else {
        if (filter->counter > 0) {
            filter->counter--;
        }
        /* Check if reversing towards 0 while state is 1 but not yet reached 0 */
        if (filter->state == 0 && filter->counter > 0 && filter->counter < filter->c_max) {
            filter->glitches_rejected++;
        }
    }

    /* Output state transitions only at saturation rails */
    if (filter->counter == filter->c_max) {
        if (filter->state == 0) {
            filter->state = 1;
            filter->debounced_rising++;
            detected_edge = EDGE_RISING;
        }
    } else if (filter->counter == 0) {
        if (filter->state == 1) {
            filter->state = 0;
            filter->debounced_falling++;
            detected_edge = EDGE_FALLING;
        }
    }

    if (edge_out) {
        *edge_out = detected_edge;
    }

    return filter->state;
}

int run_debounce_simulation(const bounce_trace_t *trace, uint32_t debounce_us, uint32_t sample_us, debounce_sim_result_t *result) {
    if (!trace || !result || trace->sample_count == 0) return -1;
    memset(result, 0, sizeof(debounce_sim_result_t));

    uint32_t eff_sample_us = (sample_us > 0) ? sample_us : trace->sample_interval_us;
    if (eff_sample_us == 0) eff_sample_us = 100;

    debounce_filter_t filter;
    debounce_filter_init(&filter, debounce_us, eff_sample_us, trace->samples[0]);

    for (uint32_t i = 0; i < trace->sample_count; i++) {
        gpio_edge_t edge = EDGE_NONE;
        debounce_filter_step(&filter, trace->samples[i], &edge);
    }

    result->total_samples = trace->sample_count;
    result->total_duration_ms = (double)(trace->sample_count * eff_sample_us) / 1000.0;
    result->raw_rising_count = filter.raw_rising_edges;
    result->raw_falling_count = filter.raw_falling_edges;
    result->debounced_rising_count = filter.debounced_rising;
    result->debounced_falling_count = filter.debounced_falling;
    result->glitches_rejected = filter.glitches_rejected;
    result->final_output_state = filter.state;

    uint64_t total_raw = filter.raw_rising_edges + filter.raw_falling_edges;
    uint64_t total_debounced = filter.debounced_rising + filter.debounced_falling;
    if (total_raw > 0) {
        if (total_raw >= total_debounced) {
            result->noise_reduction_ratio = (double)(total_raw - total_debounced) / (double)total_raw;
        } else {
            result->noise_reduction_ratio = 0.0;
        }
    } else {
        result->noise_reduction_ratio = 1.0;
    }

    return 0;
}

int generate_synthetic_bounce_trace(uint32_t total_duration_us, uint32_t bounce_duration_us, uint32_t sample_us, bounce_trace_t *trace) {
    if (!trace || sample_us == 0 || total_duration_us < bounce_duration_us) return -1;
    memset(trace, 0, sizeof(bounce_trace_t));

    uint32_t total_samples = total_duration_us / sample_us;
    if (total_samples > LH_GPIO_MAX_TRACE_SAMPLES) {
        total_samples = LH_GPIO_MAX_TRACE_SAMPLES;
    }
    trace->sample_count = total_samples;
    trace->sample_interval_us = sample_us;

    /* Transition occurs around 30% into the trace */
    uint32_t transition_start_idx = (uint32_t)(total_samples * 0.30);
    uint32_t bounce_samples = bounce_duration_us / sample_us;
    uint32_t transition_end_idx = transition_start_idx + bounce_samples;
    if (transition_end_idx >= total_samples) {
        transition_end_idx = total_samples - 1;
    }

    for (uint32_t i = 0; i < total_samples; i++) {
        trace->timestamps_us[i] = (uint64_t)i * sample_us;
        if (i < transition_start_idx) {
            trace->samples[i] = 0;
        } else if (i < transition_end_idx) {
            /* Realistic chattering bounce: alternates rapidly with intermittent pulses */
            uint32_t offset = i - transition_start_idx;
            trace->samples[i] = ((offset % 3 == 0) || (offset % 5 == 0)) ? 1 : 0;
        } else {
            trace->samples[i] = 1;
        }
    }

    return 0;
}

void irq_storm_init(irq_storm_limiter_t *limiter, uint32_t max_rate_hz, uint32_t cooldown_ms, uint32_t window_ms) {
    if (!limiter) return;
    memset(limiter, 0, sizeof(irq_storm_limiter_t));
    limiter->max_rate_hz = (max_rate_hz > 0) ? max_rate_hz : 500;
    limiter->cooldown_ms = (cooldown_ms > 0) ? cooldown_ms : 1000;
    limiter->window_ms = (window_ms > 0) ? window_ms : 1000;
    limiter->state = IRQ_NORMAL;
}

bool irq_storm_process_event(irq_storm_limiter_t *limiter, uint64_t timestamp_us, bool *is_locked_out) {
    if (!limiter) {
        if (is_locked_out) *is_locked_out = false;
        return true;
    }

    /* Check if currently in lockout */
    if (limiter->state == IRQ_MASKED_STORM_LOCKOUT) {
        if (timestamp_us < limiter->lockout_until_us) {
            /* Still in cooldown lockout: drop event */
            limiter->total_irqs_dropped++;
            if (is_locked_out) *is_locked_out = true;
            return false;
        }
        /* Cooldown expired: re-arm channel */
        limiter->state = IRQ_NORMAL;
        limiter->window_start_us = timestamp_us;
        limiter->irq_count_in_window = 1;
        limiter->total_irqs_processed++;
        if (is_locked_out) *is_locked_out = false;
        return true;
    }

    /* Normal mode: check sliding window */
    uint64_t window_us = (uint64_t)limiter->window_ms * 1000ULL;
    if (limiter->window_start_us == 0 || timestamp_us >= limiter->window_start_us + window_us) {
        limiter->window_start_us = timestamp_us;
        limiter->irq_count_in_window = 1;
        limiter->total_irqs_processed++;
        if (is_locked_out) *is_locked_out = false;
        return true;
    }

    limiter->irq_count_in_window++;
    uint32_t max_allowed_in_win = (uint32_t)(((uint64_t)limiter->max_rate_hz * limiter->window_ms) / 1000ULL);
    if (max_allowed_in_win == 0) max_allowed_in_win = 1;

    if (limiter->irq_count_in_window > max_allowed_in_win) {
        /* IRQ storm detected! Trigger lockout */
        limiter->state = IRQ_MASKED_STORM_LOCKOUT;
        limiter->lockout_until_us = timestamp_us + (uint64_t)limiter->cooldown_ms * 1000ULL;
        limiter->storm_lockout_events++;
        limiter->total_irqs_dropped++;
        if (is_locked_out) *is_locked_out = true;
        return false;
    }

    limiter->total_irqs_processed++;
    if (is_locked_out) *is_locked_out = false;
    return true;
}

int calc_optocoupler_saturation(const opto_params_t *params, opto_result_t *result) {
    if (!params || !result) return -1;
    memset(result, 0, sizeof(opto_result_t));

    if (params->rlimit_ohm <= 0.0 || params->rpull_ohm <= 0.0 || params->vin_v <= params->vf_v) {
        snprintf(result->safety_verdict, sizeof(result->safety_verdict), "INVALID_PARAMETERS");
        return -1;
    }

    /* 1. Forward current I_F */
    double if_a = (params->vin_v - params->vf_v) / params->rlimit_ohm;
    result->if_ma = if_a * 1000.0;

    /* 2. Available collector current I_C_available with aging derating */
    double ctr_effective = (params->ctr_min_percent / 100.0) * params->aging_factor;
    double ic_avail_a = if_a * ctr_effective;
    result->ic_available_ma = ic_avail_a * 1000.0;

    /* 3. Required collector current to guarantee logic low output */
    double v_pull_drop = params->vdd_v - params->vce_sat_v;
    if (v_pull_drop < 0.0) v_pull_drop = 0.0;
    double ic_req_a = v_pull_drop / params->rpull_ohm;
    result->ic_required_ma = ic_req_a * 1000.0;

    /* 4. Overdrive saturation ratio gamma_sat */
    if (ic_req_a > 0.0) {
        result->saturation_ratio = ic_avail_a / ic_req_a;
    } else {
        result->saturation_ratio = 999.0;
    }

    /* 5. Series resistor power dissipation */
    double p_rlimit_w = if_a * if_a * params->rlimit_ohm;
    result->rlimit_power_mw = p_rlimit_w * 1000.0;

    /* 6. Estimated propagation delay based on RC pull-up */
    /* t_pd approx 2.2 * R_pull * C_opto (seconds) */
    double c_opto_f = params->opto_cap_pf * 1e-12;
    double t_pd_sec = 2.2 * params->rpull_ohm * c_opto_f;
    result->est_propagation_us = t_pd_sec * 1e6;

    /* 7. Safety margin checks */
    result->is_saturated = (result->saturation_ratio >= 1.50);
    result->rlimit_power_safe = (result->rlimit_power_mw <= 250.0); /* 1/4W resistor safe limit */

    if (result->is_saturated && result->rlimit_power_safe) {
        snprintf(result->safety_verdict, sizeof(result->safety_verdict), "SATURATED_SAFE");
    } else if (!result->is_saturated && result->rlimit_power_safe) {
        snprintf(result->safety_verdict, sizeof(result->safety_verdict), "MARGINAL_CTR_STARVATION");
    } else if (result->is_saturated && !result->rlimit_power_safe) {
        snprintf(result->safety_verdict, sizeof(result->safety_verdict), "RESISTOR_THERMAL_OVERLOAD");
    } else {
        snprintf(result->safety_verdict, sizeof(result->safety_verdict), "UNSAFE_UNDERDRIVE_AND_OVERHEAT");
    }

    return 0;
}

int generate_dtoverlay(const dtoverlay_opts_t *opts, char *out_buf, size_t max_len) {
    if (!opts || !out_buf || max_len == 0) return -1;

    const char *node_name = (opts->node_name[0] != '\0') ? opts->node_name : "lh_gpio_isolated";
    const char *compatible = (opts->compatible[0] != '\0') ? opts->compatible : "logihertz,gpio-isolated-v1";
    uint32_t ch_count = (opts->channel_count > 0) ? opts->channel_count : 8;
    uint32_t iso_vrms = (opts->isolation_vrms > 0) ? opts->isolation_vrms : 5000;
    uint32_t deb_us = (opts->debounce_delay_us > 0) ? opts->debounce_delay_us : 5000;
    uint32_t max_irq = (opts->max_irq_rate_hz > 0) ? opts->max_irq_rate_hz : 500;
    uint32_t cooldown = (opts->cooldown_ms > 0) ? opts->cooldown_ms : 1000;
    const char *failsafe = (opts->failsafe_state[0] != '\0') ? opts->failsafe_state : "low";
    uint32_t mode = (opts->chardev_mode_octal > 0) ? opts->chardev_mode_octal : 0600;

    int written = snprintf(out_buf, max_len,
        "// SPDX-License-Identifier: Apache-2.0\n"
        "/*\n"
        " * Device Tree Overlay for Logihertz Isolated Industrial GPIO Driver\n"
        " * Target: NVIDIA Jetson Orin (Tegra234) & Generic Embedded Linux\n"
        " * Generated by lh-gpio-iso gen-dtoverlay\n"
        " */\n\n"
        "/dts-v1/;\n"
        "/plugin/;\n\n"
        "/ {\n"
        "    overlay-name = \"Logihertz Isolated Industrial GPIO Overlay\";\n"
        "    compatible = \"nvidia,tegra234\", \"nvidia,jetson-orin\";\n\n"
        "    fragment@0 {\n"
        "        target-path = \"/\";\n"
        "        __overlay__ {\n"
        "            %s {\n"
        "                compatible = \"%s\";\n"
        "                status = \"okay\";\n\n"
        "                /* Industrial Isolation & Signal Characteristics */\n"
        "                logihertz,isolation-rating-vrms = <%u>;\n"
        "                logihertz,channel-count = <%u>;\n"
        "                logihertz,debounce-delay-us = <%u>;\n"
        "                logihertz,failsafe-state = \"%s\";\n"
        "                logihertz,isolated-vcc2-uvlo-active = <1>;\n\n"
        "                /* Interrupt-Storm & Denial-of-Service Protection */\n"
        "                logihertz,max-irq-rate-hz = <%u>;\n"
        "                logihertz,cooldown-period-ms = <%u>;\n"
        "                logihertz,storm-mask-lockout = <1>;\n\n"
        "                /* Access Control & Kernel Chardev Node */\n"
        "                logihertz,chardev-permissions = <0%o>;\n"
        "                logihertz,lock-channel-direction = <1>;\n"
        "                logihertz,disable-sysfs-export = <1>;\n"
        "            };\n"
        "        };\n"
        "    };\n"
        "};\n",
        node_name, compatible, iso_vrms, ch_count, deb_us, failsafe,
        max_irq, cooldown, mode
    );

    return (written > 0 && (size_t)written < max_len) ? 0 : -1;
}
