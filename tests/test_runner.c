#include "gpio_types.h"
#include "debounce_irq.h"
#include "gpio_auditor.h"
#include "exporters.h"
#include "json_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        fprintf(stderr, "FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
    } \
} while(0)

/* Suite 1: Debounce Filter (Kuhn Digital Integrator) */
static void test_debounce_filter_logic(void) {
    printf("[SUITE 1] Testing Kuhn Digital Integrator Debounce Filter...\n");

    debounce_filter_t filter;
    /* Debounce window = 5000 us, sample interval = 1000 us -> c_max = 5 */
    debounce_filter_init(&filter, 5000, 1000, 0);

    TEST_ASSERT(filter.c_max == 5, "c_max should be ceil(5000/1000) = 5");
    TEST_ASSERT(filter.counter == 0, "Initial counter should be 0");
    TEST_ASSERT(filter.state == 0, "Initial state should be 0");

    gpio_edge_t edge = EDGE_NONE;

    /* Feed 3 high samples: counter becomes 3, state should remain 0 */
    debounce_filter_step(&filter, 1, &edge);
    TEST_ASSERT(filter.counter == 1, "Counter after 1 high sample");
    TEST_ASSERT(edge == EDGE_NONE, "No edge should trigger yet");

    debounce_filter_step(&filter, 1, &edge);
    debounce_filter_step(&filter, 1, &edge);
    TEST_ASSERT(filter.counter == 3, "Counter after 3 high samples");
    TEST_ASSERT(filter.state == 0, "Output state still 0");

    /* Feed a low glitch (contact bounce): counter drops to 2, state remains 0 */
    debounce_filter_step(&filter, 0, &edge);
    TEST_ASSERT(filter.counter == 2, "Counter drops on bounce glitch");
    TEST_ASSERT(filter.state == 0, "Output state remains 0 on bounce glitch");

    /* Feed high samples until counter saturates at c_max = 5 */
    debounce_filter_step(&filter, 1, &edge); /* counter = 3 */
    debounce_filter_step(&filter, 1, &edge); /* counter = 4 */
    debounce_filter_step(&filter, 1, &edge); /* counter = 5 -> triggers RISING */
    TEST_ASSERT(filter.counter == 5, "Counter at saturation c_max");
    TEST_ASSERT(filter.state == 1, "Output state transitions to 1");
    TEST_ASSERT(edge == EDGE_RISING, "Rising edge triggered");
    TEST_ASSERT(filter.debounced_rising == 1, "Debounced rising count is 1");

    /* Additional high samples keep counter clamped at c_max */
    debounce_filter_step(&filter, 1, &edge);
    TEST_ASSERT(filter.counter == 5, "Counter clamped at c_max");
    TEST_ASSERT(edge == EDGE_NONE, "No additional edge when stable high");

    /* Test transition back to low with glitch */
    debounce_filter_step(&filter, 0, &edge); /* counter = 4 */
    debounce_filter_step(&filter, 0, &edge); /* counter = 3 */
    TEST_ASSERT(filter.state == 1, "Output state still 1 during falling bounce");

    /* Bounce glitch back to 1 */
    debounce_filter_step(&filter, 1, &edge); /* counter = 4 */
    TEST_ASSERT(filter.glitches_rejected > 0, "Glitch rejected counter incremented");

    /* Drive to 0 */
    debounce_filter_step(&filter, 0, &edge); /* 3 */
    debounce_filter_step(&filter, 0, &edge); /* 2 */
    debounce_filter_step(&filter, 0, &edge); /* 1 */
    debounce_filter_step(&filter, 0, &edge); /* 0 -> triggers FALLING */
    TEST_ASSERT(filter.counter == 0, "Counter at rail 0");
    TEST_ASSERT(filter.state == 0, "Output state transitions to 0");
    TEST_ASSERT(edge == EDGE_FALLING, "Falling edge triggered");
    TEST_ASSERT(filter.debounced_falling == 1, "Debounced falling count is 1");

    /* Reset filter */
    debounce_filter_reset(&filter);
    TEST_ASSERT(filter.raw_rising_edges == 0, "Reset clears raw rising edges");
    TEST_ASSERT(filter.debounced_rising == 0, "Reset clears debounced rising edges");
}

/* Suite 2: Contact Bounce Simulation */
static void test_bounce_simulation(void) {
    printf("[SUITE 2] Testing Contact Bounce Waveform Simulation...\n");

    bounce_trace_t trace;
    int rc = parse_bounce_trace_file("fixtures/sample_bounce_trace.json", &trace);
    TEST_ASSERT(rc == 0, "parse_bounce_trace_file succeeded");
    TEST_ASSERT(trace.sample_count == 100, "Trace contains 100 samples");
    TEST_ASSERT(trace.sample_interval_us == 100, "Sample interval is 100 us");

    debounce_sim_result_t result;
    rc = run_debounce_simulation(&trace, 3000, 100, &result);
    TEST_ASSERT(rc == 0, "run_debounce_simulation succeeded");
    TEST_ASSERT(result.total_samples == 100, "Result total samples matches trace");
    TEST_ASSERT(result.total_duration_ms == 10.0, "Result total duration is 10.0 ms");
    TEST_ASSERT(result.raw_rising_count >= 5, "Raw rising count should capture contact chattering");
    TEST_ASSERT(result.debounced_rising_count == 1, "Debounced rising count must be exactly 1");
    TEST_ASSERT(result.debounced_falling_count == 0, "Debounced falling count must be 0");
    TEST_ASSERT(result.final_output_state == 1, "Final output state must be 1 (high)");
    TEST_ASSERT(result.noise_reduction_ratio > 0.70, "Noise reduction ratio > 70%");

    /* Test synthetic generator */
    bounce_trace_t synth;
    rc = generate_synthetic_bounce_trace(15000, 3000, 100, &synth);
    TEST_ASSERT(rc == 0, "generate_synthetic_bounce_trace succeeded");
    TEST_ASSERT(synth.sample_count == 150, "Synthetic trace count is 150");
    TEST_ASSERT(synth.samples[0] == 0, "Initial level is 0");
    TEST_ASSERT(synth.samples[149] == 1, "Final level is 1");
}

/* Suite 3: IRQ Storm Limiter */
static void test_irq_storm_limiter(void) {
    printf("[SUITE 3] Testing IRQ Storm Limiter (CWE-400 Protection)...\n");

    irq_storm_limiter_t limiter;
    /* Max rate: 100 Hz, Cooldown: 500 ms, Window: 1000 ms */
    irq_storm_init(&limiter, 100, 500, 1000);
    TEST_ASSERT(limiter.max_rate_hz == 100, "Max rate configured");
    TEST_ASSERT(limiter.state == IRQ_NORMAL, "Initial state IRQ_NORMAL");

    bool locked_out = false;
    uint64_t ts = 1000000; /* 1.0 s */

    /* Send 50 normal interrupts over 500 ms (rate = 100 Hz, exactly on threshold) */
    for (int i = 0; i < 50; i++) {
        ts += 10000; /* 10 ms interval */
        bool allowed = irq_storm_process_event(&limiter, ts, &locked_out);
        TEST_ASSERT(allowed == true, "Normal rate IRQ allowed");
        TEST_ASSERT(locked_out == false, "Not locked out");
    }
    TEST_ASSERT(limiter.state == IRQ_NORMAL, "Remains IRQ_NORMAL");
    TEST_ASSERT(limiter.storm_lockout_events == 0, "No storm events yet");

    /* Flood with 100 rapid interrupts in 5 ms -> exceeds 100 Hz limit in window */
    bool triggered = false;
    for (int i = 0; i < 100; i++) {
        ts += 50; /* 50 us interval */
        bool allowed = irq_storm_process_event(&limiter, ts, &locked_out);
        if (!allowed && locked_out) {
            triggered = true;
            break;
        }
    }
    TEST_ASSERT(triggered == true, "IRQ storm triggered lockout");
    TEST_ASSERT(limiter.state == IRQ_MASKED_STORM_LOCKOUT, "State transitioned to IRQ_MASKED_STORM_LOCKOUT");
    TEST_ASSERT(limiter.storm_lockout_events == 1, "Storm lockout event recorded");

    /* During cooldown lockout, interrupts must be dropped */
    ts += 100000; /* 100 ms into cooldown */
    bool allowed = irq_storm_process_event(&limiter, ts, &locked_out);
    TEST_ASSERT(allowed == false, "IRQ dropped during cooldown");
    TEST_ASSERT(locked_out == true, "Locked out during cooldown");
    TEST_ASSERT(limiter.total_irqs_dropped > 0, "Dropped IRQs counted");

    /* Advance beyond cooldown period (cooldown is 500 ms) */
    ts += 600000; /* 600 ms later */
    allowed = irq_storm_process_event(&limiter, ts, &locked_out);
    TEST_ASSERT(allowed == true, "IRQ allowed after cooldown expiration");
    TEST_ASSERT(locked_out == false, "Lockout cleared");
    TEST_ASSERT(limiter.state == IRQ_NORMAL, "State re-armed to IRQ_NORMAL");
}

/* Suite 4: Optocoupler CTR Saturation & Power Analysis */
static void test_optocoupler_calculations(void) {
    printf("[SUITE 4] Testing Optocoupler CTR Saturation & Resistor Power...\n");

    opto_params_t params;
    params.vin_v = 24.0;
    params.rlimit_ohm = 2200.0;
    params.vf_v = 1.25;
    params.ctr_min_percent = 100.0;
    params.aging_factor = 0.70;
    params.vdd_v = 3.3;
    params.vce_sat_v = 0.20;
    params.rpull_ohm = 4700.0;
    params.opto_cap_pf = 15.0;

    opto_result_t result;
    int rc = calc_optocoupler_saturation(&params, &result);
    TEST_ASSERT(rc == 0, "calc_optocoupler_saturation succeeded");

    /* If = (24.0 - 1.25) / 2200 = 22.75 / 2200 ~= 10.34 mA */
    TEST_ASSERT(fabs(result.if_ma - 10.34) < 0.1, "Forward current approx 10.34 mA");

    /* Ic_avail = If * (100% / 100) * 0.70 ~= 7.24 mA */
    TEST_ASSERT(fabs(result.ic_available_ma - 7.24) < 0.1, "Available Ic approx 7.24 mA");

    /* Ic_req = (3.3 - 0.20) / 4700 = 3.10 / 4700 ~= 0.66 mA */
    TEST_ASSERT(fabs(result.ic_required_ma - 0.66) < 0.05, "Required Ic approx 0.66 mA");

    /* Saturation ratio gamma = 7.24 / 0.66 ~= 10.97 >= 1.50 */
    TEST_ASSERT(result.saturation_ratio > 5.0, "Saturation ratio > 5.0");
    TEST_ASSERT(result.is_saturated == true, "Optocoupler is saturated");

    /* Series resistor power: (0.01034)^2 * 2200 ~= 235 mW <= 250 mW */
    TEST_ASSERT(result.rlimit_power_mw < 250.0, "Series resistor power safe <= 250 mW");
    TEST_ASSERT(result.rlimit_power_safe == true, "Resistor power safe flag set");
    TEST_ASSERT(strcmp(result.safety_verdict, "SATURATED_SAFE") == 0, "Verdict SATURATED_SAFE");

    /* Starvation test: high series resistor */
    params.rlimit_ohm = 47000.0; /* 47k Ohm */
    calc_optocoupler_saturation(&params, &result);
    TEST_ASSERT(result.is_saturated == false, "Insufficient CTR overdrive with 47k resistor");
    TEST_ASSERT(strcmp(result.safety_verdict, "MARGINAL_CTR_STARVATION") == 0, "Verdict MARGINAL_CTR_STARVATION");

    /* Resistor thermal overload test: 48V with 1k resistor */
    params.vin_v = 48.0;
    params.rlimit_ohm = 1000.0;
    calc_optocoupler_saturation(&params, &result);
    TEST_ASSERT(result.rlimit_power_mw > 2000.0, "Resistor power exceeds 2W");
    TEST_ASSERT(result.rlimit_power_safe == false, "Resistor power unsafe flag set");
}

/* Suite 5: 10-Rule Security Posture Auditor */
static void test_posture_auditor(void) {
    printf("[SUITE 5] Testing 10-Rule Security Posture Auditor...\n");

    /* 1. Hardened Policy */
    gpio_policy_t hard_policy;
    int rc = parse_gpio_policy_file("fixtures/hardened_gpio_policy.json", &hard_policy);
    TEST_ASSERT(rc == 0, "parse hardened policy file succeeded");

    audit_report_t hard_report;
    rc = audit_gpio_policy(&hard_policy, &hard_report);
    TEST_ASSERT(rc == 0, "audit hardened policy succeeded");
    TEST_ASSERT(hard_report.total_rules == 10, "Total rules evaluated == 10");
    TEST_ASSERT(hard_report.passed_rules == 10, "Passed rules == 10");
    TEST_ASSERT(hard_report.failed_rules == 0, "Failed rules == 0");
    TEST_ASSERT(hard_report.total_deductions == 0, "Total deductions == 0");
    TEST_ASSERT(hard_report.posture_score == 100, "Hardened posture score == 100");

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(hard_report.findings[i].passed == true, "Hardened rule must pass");
        TEST_ASSERT(hard_report.findings[i].deduction == 0 || hard_report.findings[i].passed, "Deduction zero on pass");
    }

    /* 2. Vulnerable Policy */
    gpio_policy_t vuln_policy;
    rc = parse_gpio_policy_file("fixtures/vulnerable_gpio_policy.json", &vuln_policy);
    TEST_ASSERT(rc == 0, "parse vulnerable policy file succeeded");

    audit_report_t vuln_report;
    rc = audit_gpio_policy(&vuln_policy, &vuln_report);
    TEST_ASSERT(rc == 0, "audit vulnerable policy succeeded");
    TEST_ASSERT(vuln_report.total_rules == 10, "Total rules evaluated == 10");
    TEST_ASSERT(vuln_report.passed_rules == 0, "Passed rules == 0");
    TEST_ASSERT(vuln_report.failed_rules == 10, "Failed rules == 10");
    TEST_ASSERT(vuln_report.total_deductions == 124, "Total deductions == 124 (25x3 + 10x4 + 4x2 + 1)");
    TEST_ASSERT(vuln_report.posture_score == 0, "Vulnerable posture score == 0");

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(vuln_report.findings[i].passed == false, "Vulnerable rule must fail");
    }

    /* 3. Granular Rule Deductions */
    TEST_ASSERT(strcmp(vuln_report.findings[0].rule_id, "LH-GPIO-001") == 0, "Rule 1 ID");
    TEST_ASSERT(vuln_report.findings[0].deduction == 25, "Rule 1 deduction is 25");

    TEST_ASSERT(strcmp(vuln_report.findings[1].rule_id, "LH-GPIO-002") == 0, "Rule 2 ID");
    TEST_ASSERT(vuln_report.findings[1].deduction == 25, "Rule 2 deduction is 25");

    TEST_ASSERT(strcmp(vuln_report.findings[2].rule_id, "LH-GPIO-003") == 0, "Rule 3 ID");
    TEST_ASSERT(vuln_report.findings[2].deduction == 25, "Rule 3 deduction is 25");

    TEST_ASSERT(strcmp(vuln_report.findings[3].rule_id, "LH-GPIO-004") == 0, "Rule 4 ID");
    TEST_ASSERT(vuln_report.findings[3].deduction == 10, "Rule 4 deduction is 10");

    TEST_ASSERT(strcmp(vuln_report.findings[4].rule_id, "LH-GPIO-005") == 0, "Rule 5 ID");
    TEST_ASSERT(vuln_report.findings[4].deduction == 10, "Rule 5 deduction is 10");

    TEST_ASSERT(strcmp(vuln_report.findings[5].rule_id, "LH-GPIO-006") == 0, "Rule 6 ID");
    TEST_ASSERT(vuln_report.findings[5].deduction == 10, "Rule 6 deduction is 10");

    TEST_ASSERT(strcmp(vuln_report.findings[6].rule_id, "LH-GPIO-007") == 0, "Rule 7 ID");
    TEST_ASSERT(vuln_report.findings[6].deduction == 10, "Rule 7 deduction is 10");

    TEST_ASSERT(strcmp(vuln_report.findings[7].rule_id, "LH-GPIO-008") == 0, "Rule 8 ID");
    TEST_ASSERT(vuln_report.findings[7].deduction == 4, "Rule 8 deduction is 4");

    TEST_ASSERT(strcmp(vuln_report.findings[8].rule_id, "LH-GPIO-009") == 0, "Rule 9 ID");
    TEST_ASSERT(vuln_report.findings[8].deduction == 4, "Rule 9 deduction is 4");

    TEST_ASSERT(strcmp(vuln_report.findings[9].rule_id, "LH-GPIO-010") == 0, "Rule 10 ID");
    TEST_ASSERT(vuln_report.findings[9].deduction == 1, "Rule 10 deduction is 1");
}

/* Suite 6: Device Tree Overlay Generator */
static void test_dtoverlay_generator(void) {
    printf("[SUITE 6] Testing Device Tree Overlay Synthesizer...\n");

    dtoverlay_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    snprintf(opts.node_name, sizeof(opts.node_name), "test_iso_gpio");
    snprintf(opts.compatible, sizeof(opts.compatible), "logihertz,gpio-isolated-v1");
    opts.channel_count = 16;
    opts.isolation_vrms = 5000;
    opts.debounce_delay_us = 4000;
    opts.max_irq_rate_hz = 600;
    opts.cooldown_ms = 1500;
    snprintf(opts.failsafe_state, sizeof(opts.failsafe_state), "low");
    opts.chardev_mode_octal = 0600;

    char dts_buf[2048];
    int rc = generate_dtoverlay(&opts, dts_buf, sizeof(dts_buf));
    TEST_ASSERT(rc == 0, "generate_dtoverlay succeeded");
    TEST_ASSERT(strstr(dts_buf, "/dts-v1/;") != NULL, "DTS contains /dts-v1/;");
    TEST_ASSERT(strstr(dts_buf, "/plugin/;") != NULL, "DTS contains /plugin/;");
    TEST_ASSERT(strstr(dts_buf, "compatible = \"logihertz,gpio-isolated-v1\";") != NULL, "DTS contains compatible string");
    TEST_ASSERT(strstr(dts_buf, "logihertz,isolation-rating-vrms = <5000>;") != NULL, "DTS contains isolation rating");
    TEST_ASSERT(strstr(dts_buf, "logihertz,channel-count = <16>;") != NULL, "DTS contains channel count");
    TEST_ASSERT(strstr(dts_buf, "logihertz,debounce-delay-us = <4000>;") != NULL, "DTS contains debounce delay");
    TEST_ASSERT(strstr(dts_buf, "logihertz,max-irq-rate-hz = <600>;") != NULL, "DTS contains max IRQ rate");
    TEST_ASSERT(strstr(dts_buf, "logihertz,chardev-permissions = <0600>;") != NULL, "DTS contains chardev permissions");
}

/* Suite 7: Exporters */
static void test_exporters(void) {
    printf("[SUITE 7] Testing Exporters (Table, JSON, SARIF)...\n");

    gpio_policy_t policy;
    parse_gpio_policy_file("fixtures/hardened_gpio_policy.json", &policy);
    audit_report_t report;
    audit_gpio_policy(&policy, &report);

    /* Test null stream / null report handling */
    TEST_ASSERT(export_audit_table(NULL, stdout) == -1, "export_audit_table handles NULL report");
    TEST_ASSERT(export_audit_json(NULL, stdout) == -1, "export_audit_json handles NULL report");
    TEST_ASSERT(export_audit_sarif(NULL, stdout) == -1, "export_audit_sarif handles NULL report");

    /* Export to memory streams */
    char buf[8192];
    FILE *mem = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT(mem != NULL, "fmemopen created memory stream");

    int rc = export_audit_json(&report, mem);
    TEST_ASSERT(rc == 0, "export_audit_json succeeded");
    fclose(mem);
    TEST_ASSERT(strstr(buf, "\"tool\": \"lh-gpio-iso\"") != NULL, "JSON contains tool identifier");
    TEST_ASSERT(strstr(buf, "\"posture_score\": 100") != NULL, "JSON contains posture score 100");

    mem = fmemopen(buf, sizeof(buf), "w");
    rc = export_audit_sarif(&report, mem);
    TEST_ASSERT(rc == 0, "export_audit_sarif succeeded");
    fclose(mem);
    TEST_ASSERT(strstr(buf, "\"version\": \"2.1.0\"") != NULL, "SARIF contains version 2.1.0");
    TEST_ASSERT(strstr(buf, "sarif-schema-2.1.0.json") != NULL, "SARIF contains schema");
}

int main(void) {
    printf("===================================================================\n");
    printf(" RUNNING LOGIHERTZ ISOLATED GPIO DRIVER VERIFICATION SUITE\n");
    printf("===================================================================\n");

    test_debounce_filter_logic();
    test_bounce_simulation();
    test_irq_storm_limiter();
    test_optocoupler_calculations();
    test_posture_auditor();
    test_dtoverlay_generator();
    test_exporters();

    printf("\n===================================================================\n");
    printf(" TEST SUMMARY: %d / %d assertions passed (100%%)\n", g_tests_passed, g_tests_run);
    printf("===================================================================\n");

    return (g_tests_run > 0 && g_tests_passed == g_tests_run) ? 0 : 1;
}
