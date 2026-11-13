#include "exporters.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *severity_to_str(severity_t sev) {
    switch (sev) {
        case SEV_CRITICAL: return "CRITICAL";
        case SEV_HIGH:     return "HIGH";
        case SEV_MEDIUM:   return "MEDIUM";
        case SEV_LOW:      return "LOW";
        default:           return "INFO";
    }
}

static const char *severity_to_sarif_level(severity_t sev) {
    switch (sev) {
        case SEV_CRITICAL:
        case SEV_HIGH:     return "error";
        case SEV_MEDIUM:   return "warning";
        case SEV_LOW:      return "note";
        default:           return "none";
    }
}

int export_audit_table(const audit_report_t *report, FILE *stream) {
    if (!report || !stream) return -1;

    fprintf(stream, "\n========================================================================================\n");
    fprintf(stream, " LOGIHERTZ ISOLATED INDUSTRIAL GPIO DRIVER - POSTURE AUDIT REPORT\n");
    fprintf(stream, " Policy: %s\n", report->policy_name);
    fprintf(stream, "========================================================================================\n");
    fprintf(stream, " %-12s | %-6s | %-8s | %-4s | %-42s\n", "RULE ID", "STATUS", "SEVERITY", "DED", "RULE NAME");
    fprintf(stream, "----------------------------------------------------------------------------------------\n");

    for (uint32_t i = 0; i < report->total_rules; i++) {
        const audit_finding_t *f = &report->findings[i];
        const char *status_str = f->passed ? "\033[32mPASS\033[0m" : "\033[31mFAIL\033[0m";
        fprintf(stream, " %-12s | %-15s | %-8s | -%-2u | %-42s\n",
            f->rule_id,
            status_str,
            severity_to_str(f->severity),
            f->passed ? 0 : f->deduction,
            f->rule_name);
    }

    fprintf(stream, "========================================================================================\n");
    fprintf(stream, " SUMMARY: Total Rules: %u | Passed: %u | Failed: %u | Deductions: -%u\n",
        report->total_rules, report->passed_rules, report->failed_rules, report->total_deductions);
    fprintf(stream, " POSTURE SCORE: \033[%sm%u / 100\033[0m [%s]\n",
        (report->posture_score >= 80) ? "32" : (report->posture_score >= 50 ? "33" : "31"),
        report->posture_score,
        (report->posture_score >= 80) ? "PASS" : "FAIL");
    fprintf(stream, "========================================================================================\n\n");

    return 0;
}

int export_audit_json(const audit_report_t *report, FILE *stream) {
    if (!report || !stream) return -1;

    fprintf(stream, "{\n");
    fprintf(stream, "  \"tool\": \"lh-gpio-iso\",\n");
    fprintf(stream, "  \"policy_name\": \"%s\",\n", report->policy_name);
    fprintf(stream, "  \"posture_score\": %u,\n", report->posture_score);
    fprintf(stream, "  \"total_rules\": %u,\n", report->total_rules);
    fprintf(stream, "  \"passed_rules\": %u,\n", report->passed_rules);
    fprintf(stream, "  \"failed_rules\": %u,\n", report->failed_rules);
    fprintf(stream, "  \"total_deductions\": %u,\n", report->total_deductions);
    fprintf(stream, "  \"status\": \"%s\",\n", (report->posture_score >= 80) ? "PASS" : "FAIL");
    fprintf(stream, "  \"findings\": [\n");

    for (uint32_t i = 0; i < report->total_rules; i++) {
        const audit_finding_t *f = &report->findings[i];
        fprintf(stream, "    {\n");
        fprintf(stream, "      \"rule_id\": \"%s\",\n", f->rule_id);
        fprintf(stream, "      \"rule_name\": \"%s\",\n", f->rule_name);
        fprintf(stream, "      \"cwe_id\": \"%s\",\n", f->cwe_id);
        fprintf(stream, "      \"severity\": \"%s\",\n", severity_to_str(f->severity));
        fprintf(stream, "      \"deduction\": %u,\n", f->passed ? 0 : f->deduction);
        fprintf(stream, "      \"passed\": %s,\n", f->passed ? "true" : "false");
        fprintf(stream, "      \"details\": \"%s\",\n", f->details);
        fprintf(stream, "      \"recommendation\": \"%s\"\n", f->recommendation);
        fprintf(stream, "    }%s\n", (i + 1 < report->total_rules) ? "," : "");
    }

    fprintf(stream, "  ]\n");
    fprintf(stream, "}\n");
    return 0;
}

int export_audit_sarif(const audit_report_t *report, FILE *stream) {
    if (!report || !stream) return -1;

    fprintf(stream, "{\n");
    fprintf(stream, "  \"$schema\": \"https://docs.oasis-open.org/sarif/sarif/v2.1.0/cos01/schemas/sarif-schema-2.1.0.json\",\n");
    fprintf(stream, "  \"version\": \"2.1.0\",\n");
    fprintf(stream, "  \"runs\": [\n");
    fprintf(stream, "    {\n");
    fprintf(stream, "      \"tool\": {\n");
    fprintf(stream, "        \"driver\": {\n");
    fprintf(stream, "          \"name\": \"lh-gpio-iso\",\n");
    fprintf(stream, "          \"version\": \"0.1.0\",\n");
    fprintf(stream, "          \"informationUri\": \"https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-gpio-isolated-driver\",\n");
    fprintf(stream, "          \"rules\": [\n");

    for (uint32_t i = 0; i < report->total_rules; i++) {
        const audit_finding_t *f = &report->findings[i];
        fprintf(stream, "            {\n");
        fprintf(stream, "              \"id\": \"%s\",\n", f->rule_id);
        fprintf(stream, "              \"name\": \"%s\",\n", f->rule_name);
        fprintf(stream, "              \"shortDescription\": { \"text\": \"%s\" },\n", f->rule_name);
        fprintf(stream, "              \"help\": { \"text\": \"%s\" },\n", f->recommendation);
        fprintf(stream, "              \"defaultConfiguration\": {\n");
        fprintf(stream, "                \"level\": \"%s\"\n", severity_to_sarif_level(f->severity));
        fprintf(stream, "              }\n");
        fprintf(stream, "            }%s\n", (i + 1 < report->total_rules) ? "," : "");
    }

    fprintf(stream, "          ]\n");
    fprintf(stream, "        }\n");
    fprintf(stream, "      },\n");
    fprintf(stream, "      \"results\": [\n");

    uint32_t printed_results = 0;
    for (uint32_t i = 0; i < report->total_rules; i++) {
        const audit_finding_t *f = &report->findings[i];
        if (!f->passed) {
            if (printed_results > 0) fprintf(stream, ",\n");
            fprintf(stream, "        {\n");
            fprintf(stream, "          \"ruleId\": \"%s\",\n", f->rule_id);
            fprintf(stream, "          \"level\": \"%s\",\n", severity_to_sarif_level(f->severity));
            fprintf(stream, "          \"message\": { \"text\": \"%s\" },\n", f->details);
            fprintf(stream, "          \"locations\": [\n");
            fprintf(stream, "            {\n");
            fprintf(stream, "              \"physicalLocation\": {\n");
            fprintf(stream, "                \"artifactLocation\": { \"uri\": \"%s\" }\n", report->policy_name);
            fprintf(stream, "              }\n");
            fprintf(stream, "            }\n");
            fprintf(stream, "          ]\n");
            fprintf(stream, "        }");
            printed_results++;
        }
    }
    if (printed_results > 0) fprintf(stream, "\n");

    fprintf(stream, "      ]\n");
    fprintf(stream, "    }\n");
    fprintf(stream, "  ]\n");
    fprintf(stream, "}\n");
    return 0;
}

int export_debounce_table(const debounce_sim_result_t *result, FILE *stream) {
    if (!result || !stream) return -1;

    fprintf(stream, "\n===================================================================\n");
    fprintf(stream, " KUHN DIGITAL INTEGRATOR CONTACT BOUNCE SIMULATION RESULTS\n");
    fprintf(stream, "===================================================================\n");
    fprintf(stream, " Total Duration       : %.2f ms (%u samples)\n", result->total_duration_ms, result->total_samples);
    fprintf(stream, " Raw Rising Edges     : %lu\n", (unsigned long)result->raw_rising_count);
    fprintf(stream, " Raw Falling Edges    : %lu\n", (unsigned long)result->raw_falling_count);
    fprintf(stream, " Debounced Rising     : %lu\n", (unsigned long)result->debounced_rising_count);
    fprintf(stream, " Debounced Falling    : %lu\n", (unsigned long)result->debounced_falling_count);
    fprintf(stream, " Glitches Filtered    : %lu\n", (unsigned long)result->glitches_rejected);
    fprintf(stream, " Final Output Level   : %u\n", result->final_output_state);
    fprintf(stream, " Noise Reduction      : %.1f %%\n", result->noise_reduction_ratio * 100.0);
    fprintf(stream, "===================================================================\n\n");
    return 0;
}

int export_debounce_json(const debounce_sim_result_t *result, FILE *stream) {
    if (!result || !stream) return -1;

    fprintf(stream, "{\n");
    fprintf(stream, "  \"tool\": \"lh-gpio-iso\",\n");
    fprintf(stream, "  \"subcommand\": \"sim-debounce\",\n");
    fprintf(stream, "  \"total_samples\": %u,\n", result->total_samples);
    fprintf(stream, "  \"total_duration_ms\": %.3f,\n", result->total_duration_ms);
    fprintf(stream, "  \"raw_rising_count\": %lu,\n", (unsigned long)result->raw_rising_count);
    fprintf(stream, "  \"raw_falling_count\": %lu,\n", (unsigned long)result->raw_falling_count);
    fprintf(stream, "  \"debounced_rising_count\": %lu,\n", (unsigned long)result->debounced_rising_count);
    fprintf(stream, "  \"debounced_falling_count\": %lu,\n", (unsigned long)result->debounced_falling_count);
    fprintf(stream, "  \"glitches_rejected\": %lu,\n", (unsigned long)result->glitches_rejected);
    fprintf(stream, "  \"final_output_state\": %u,\n", result->final_output_state);
    fprintf(stream, "  \"noise_reduction_ratio\": %.4f\n", result->noise_reduction_ratio);
    fprintf(stream, "}\n");
    return 0;
}

int export_opto_table(const opto_params_t *params, const opto_result_t *result, FILE *stream) {
    if (!params || !result || !stream) return -1;

    fprintf(stream, "\n===================================================================\n");
    fprintf(stream, " OPTOCOUPLER CTR SATURATION & THERMAL MARGIN ANALYSIS\n");
    fprintf(stream, "===================================================================\n");
    fprintf(stream, " Field Voltage (Vin)       : %.2f V\n", params->vin_v);
    fprintf(stream, " Series Resistor (Rlimit)  : %.1f Ohm\n", params->rlimit_ohm);
    fprintf(stream, " Forward LED Current (IF)  : %.2f mA\n", result->if_ma);
    fprintf(stream, " Resistor Dissipation      : %.1f mW (%s)\n",
        result->rlimit_power_mw, result->rlimit_power_safe ? "Safe <= 250mW" : "OVERHEAT WARNING");
    fprintf(stream, " Logic Supply (Vdd)        : %.2f V\n", params->vdd_v);
    fprintf(stream, " Pull-up Resistor (Rpull)  : %.1f Ohm\n", params->rpull_ohm);
    fprintf(stream, " Required Collector (IC)   : %.2f mA\n", result->ic_required_ma);
    fprintf(stream, " Available Collector (IC)  : %.2f mA (aging factor: %.2f)\n",
        result->ic_available_ma, params->aging_factor);
    fprintf(stream, " Saturation Ratio (gamma)  : %.2f (Min Safe: 1.50)\n", result->saturation_ratio);
    fprintf(stream, " Estimated Prop Delay (tpd): %.2f us\n", result->est_propagation_us);
    fprintf(stream, " Overall Verdict           : %s\n", result->safety_verdict);
    fprintf(stream, "===================================================================\n\n");
    return 0;
}

int export_opto_json(const opto_params_t *params, const opto_result_t *result, FILE *stream) {
    if (!params || !result || !stream) return -1;

    fprintf(stream, "{\n");
    fprintf(stream, "  \"tool\": \"lh-gpio-iso\",\n");
    fprintf(stream, "  \"subcommand\": \"calc-opto\",\n");
    fprintf(stream, "  \"vin_v\": %.2f,\n", params->vin_v);
    fprintf(stream, "  \"rlimit_ohm\": %.1f,\n", params->rlimit_ohm);
    fprintf(stream, "  \"vf_v\": %.2f,\n", params->vf_v);
    fprintf(stream, "  \"ctr_min_percent\": %.1f,\n", params->ctr_min_percent);
    fprintf(stream, "  \"aging_factor\": %.2f,\n", params->aging_factor);
    fprintf(stream, "  \"vdd_v\": %.2f,\n", params->vdd_v);
    fprintf(stream, "  \"rpull_ohm\": %.1f,\n", params->rpull_ohm);
    fprintf(stream, "  \"if_ma\": %.4f,\n", result->if_ma);
    fprintf(stream, "  \"ic_available_ma\": %.4f,\n", result->ic_available_ma);
    fprintf(stream, "  \"ic_required_ma\": %.4f,\n", result->ic_required_ma);
    fprintf(stream, "  \"saturation_ratio\": %.4f,\n", result->saturation_ratio);
    fprintf(stream, "  \"rlimit_power_mw\": %.2f,\n", result->rlimit_power_mw);
    fprintf(stream, "  \"est_propagation_us\": %.2f,\n", result->est_propagation_us);
    fprintf(stream, "  \"is_saturated\": %s,\n", result->is_saturated ? "true" : "false");
    fprintf(stream, "  \"rlimit_power_safe\": %s,\n", result->rlimit_power_safe ? "true" : "false");
    fprintf(stream, "  \"safety_verdict\": \"%s\"\n", result->safety_verdict);
    fprintf(stream, "}\n");
    return 0;
}
