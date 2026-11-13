#include "gpio_auditor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int audit_gpio_policy(const gpio_policy_t *policy, audit_report_t *report) {
    if (!policy || !report) return -1;
    memset(report, 0, sizeof(audit_report_t));

    snprintf(report->policy_name, sizeof(report->policy_name), "%s", policy->policy_name);
    report->total_rules = LH_GPIO_MAX_RULES;

    uint32_t total_deductions = 0;
    uint32_t passed_count = 0;
    uint32_t failed_count = 0;

    /* Rule 1: LH-GPIO-001 (CRITICAL, 25, CWE-693) */
    {
        audit_finding_t *f = &report->findings[0];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-001");
        snprintf(f->rule_name, sizeof(f->rule_name), "Galvanic Isolation & Ground Domain Separation");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-693");
        f->severity = SEV_CRITICAL;
        f->deduction = 25;
        if (policy->galvanic_isolation_vrms >= 2500 && !policy->shared_field_logic_ground) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "Galvanic isolation rating %u Vrms meets threshold; field and logic grounds are strictly isolated.", policy->galvanic_isolation_vrms);
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with industrial isolation standards.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "Isolation rating (%u Vrms < 2500 Vrms) or field ground shared with logic ground.", policy->galvanic_isolation_vrms);
            snprintf(f->recommendation, sizeof(f->recommendation), "Enforce galvanic isolation >= 2500 Vrms and isolate field GND from MCU logic GND.");
        }
    }

    /* Rule 2: LH-GPIO-002 (CRITICAL, 25, CWE-400) */
    {
        audit_finding_t *f = &report->findings[1];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-002");
        snprintf(f->rule_name, sizeof(f->rule_name), "Interrupt-Storm Rate Limiting & Threaded IRQ");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-400");
        f->severity = SEV_CRITICAL;
        f->deduction = 25;
        if (policy->irq_storm_limit_hz > 0 && policy->irq_storm_limit_hz <= 2000 && policy->threaded_oneshot_irq) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "IRQ storm rate limit %d Hz bounded with IRQF_ONESHOT threaded handler.", policy->irq_storm_limit_hz);
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with kernel DoS mitigation guidelines.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "Unbounded or missing IRQ rate limit (%d Hz) or threaded_oneshot_irq is disabled.", policy->irq_storm_limit_hz);
            snprintf(f->recommendation, sizeof(f->recommendation), "Enable sliding-window rate limiting (<= 2000 Hz) with IRQF_ONESHOT threaded handler.");
        }
    }

    /* Rule 3: LH-GPIO-003 (CRITICAL, 25, CWE-284) */
    {
        audit_finding_t *f = &report->findings[2];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-003");
        snprintf(f->rule_name, sizeof(f->rule_name), "Chardev Access Control & Deprecated Sysfs Disable");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-284");
        f->severity = SEV_CRITICAL;
        f->deduction = 25;
        if (!policy->legacy_sysfs_gpio_enabled && policy->chardev_permissions_octal == 600) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "Legacy sysfs disabled; character device permissions restricted to 0600.");
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with Linux GPIO character device security.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "Insecure legacy sysfs enabled (%s) or chardev permissions too open (0%o != 0600).",
                policy->legacy_sysfs_gpio_enabled ? "true" : "false", policy->chardev_permissions_octal);
            snprintf(f->recommendation, sizeof(f->recommendation), "Disable /sys/class/gpio and restrict character device permissions strictly to 0600.");
        }
    }

    /* Rule 4: LH-GPIO-004 (HIGH, 10, CWE-682) */
    {
        audit_finding_t *f = &report->findings[3];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-004");
        snprintf(f->rule_name, sizeof(f->rule_name), "Digital Integrator Debounce Filter Engine");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-682");
        f->severity = SEV_HIGH;
        f->deduction = 10;
        if (policy->debounce_enabled && policy->debounce_window_us >= 1000) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "Kuhn digital integrator debounce enabled with %u us window.", policy->debounce_window_us);
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with industrial contact bounce rejection.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "Debounce disabled or debounce window insufficient (%u us < 1000 us).", policy->debounce_window_us);
            snprintf(f->recommendation, sizeof(f->recommendation), "Enable Kuhn digital integrator debounce filter with a minimum window of 1000 us (1 ms).");
        }
    }

    /* Rule 5: LH-GPIO-005 (HIGH, 10, CWE-754) */
    {
        audit_finding_t *f = &report->findings[4];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-005");
        snprintf(f->rule_name, sizeof(f->rule_name), "Failsafe Output State & Supply UVLO Monitor");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-754");
        f->severity = SEV_HIGH;
        f->deduction = 10;
        if (policy->failsafe_output_state_on_fault && policy->isolated_vcc2_uvlo_monitor) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "Deterministic failsafe output clamp active with isolated VCC2 UVLO monitoring.");
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with industrial failsafe standards.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "Missing failsafe output clamp on fault or missing isolated VCC2 UVLO monitor.");
            snprintf(f->recommendation, sizeof(f->recommendation), "Define failsafe default output clamp (e.g. LOW) on driver fault and monitor isolated VCC2 supply.");
        }
    }

    /* Rule 6: LH-GPIO-006 (HIGH, 10, CWE-682) */
    {
        audit_finding_t *f = &report->findings[5];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-006");
        snprintf(f->rule_name, sizeof(f->rule_name), "Optocoupler CTR Overdrive & Aging Margin");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-682");
        f->severity = SEV_HIGH;
        f->deduction = 10;
        if (policy->optocoupler_ctr_saturation_ratio >= 1.50 && policy->led_aging_derating_applied) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "CTR overdrive ratio %.2f >= 1.50 with LED thermal/aging derating applied.", policy->optocoupler_ctr_saturation_ratio);
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with semiconductor saturation margins.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "CTR saturation ratio (%.2f < 1.50) or LED aging derating not applied.", policy->optocoupler_ctr_saturation_ratio);
            snprintf(f->recommendation, sizeof(f->recommendation), "Ensure saturation overdrive ratio >= 1.50 and account for 30%% LED aging/thermal derating.");
        }
    }

    /* Rule 7: LH-GPIO-007 (HIGH, 10, CWE-662) */
    {
        audit_finding_t *f = &report->findings[6];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-007");
        snprintf(f->rule_name, sizeof(f->rule_name), "Atomic Edge Counters & Exclusive Chardev Open");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-662");
        f->severity = SEV_HIGH;
        f->deduction = 10;
        if (policy->atomic_edge_counters && policy->chardev_exclusive_open) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "Lockless atomic64_t edge counters with O_EXCL exclusive access control.");
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with SMP concurrency and race prevention.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "Non-atomic edge counter updates or multi-process concurrent access race conditions permitted.");
            snprintf(f->recommendation, sizeof(f->recommendation), "Use atomic64_t for edge counters and enforce O_EXCL exclusive open semantics on driver chardev.");
        }
    }

    /* Rule 8: LH-GPIO-008 (MEDIUM, 4, CWE-1300) */
    {
        audit_finding_t *f = &report->findings[7];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-008");
        snprintf(f->rule_name, sizeof(f->rule_name), "PCB Creepage/Clearance & TVS Clamping");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-1300");
        f->severity = SEV_MEDIUM;
        f->deduction = 4;
        if (policy->isolation_creepage_mm >= 6.0 && policy->tvs_field_surge_protection) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "PCB isolation creepage %.2f mm >= 6.0 mm with TVS surge protection diodes.", policy->isolation_creepage_mm);
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with IEC 60664-1 insulation coordination.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "PCB isolation creepage (%.2f mm < 6.0 mm) or missing TVS field surge protection.", policy->isolation_creepage_mm);
            snprintf(f->recommendation, sizeof(f->recommendation), "Enforce >= 6.0 mm creepage/clearance distance across isolation barrier and place TVS diodes.");
        }
    }

    /* Rule 9: LH-GPIO-009 (MEDIUM, 4, CWE-20) */
    {
        audit_finding_t *f = &report->findings[8];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-009");
        snprintf(f->rule_name, sizeof(f->rule_name), "Strict IOCTL Validation & DT Direction Lock");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-20");
        f->severity = SEV_MEDIUM;
        f->deduction = 4;
        if (policy->strict_ioctl_validation && policy->lock_channel_direction_in_dt) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "Strict IOCTL argument boundary validation and compile-time Device Tree direction lock active.");
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with defensive driver programming.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "Unvalidated IOCTL command parameters or dynamic pin direction modification allowed.");
            snprintf(f->recommendation, sizeof(f->recommendation), "Enforce strict IOCTL parameter boundary checks and lock channel directions in Device Tree.");
        }
    }

    /* Rule 10: LH-GPIO-010 (LOW, 1, CWE-778) */
    {
        audit_finding_t *f = &report->findings[9];
        snprintf(f->rule_id, sizeof(f->rule_id), "LH-GPIO-010");
        snprintf(f->rule_name, sizeof(f->rule_name), "Monotonic Edge Timestamps & Storm Audit Logging");
        snprintf(f->cwe_id, sizeof(f->cwe_id), "CWE-778");
        f->severity = SEV_LOW;
        f->deduction = 1;
        if (policy->monotonic_raw_edge_timestamps && policy->log_irq_storm_events) {
            f->passed = true;
            snprintf(f->details, sizeof(f->details), "Monotonic hardware timestamps captured on edge transitions with syslog storm event logging.");
            snprintf(f->recommendation, sizeof(f->recommendation), "Compliant with security audit trail requirements.");
            passed_count++;
        } else {
            f->passed = false;
            total_deductions += f->deduction;
            failed_count++;
            snprintf(f->details, sizeof(f->details), "Missing monotonic timestamps for raw edges or unlogged IRQ storm lockout events.");
            snprintf(f->recommendation, sizeof(f->recommendation), "Capture CLOCK_MONOTONIC timestamps on edge events and log IRQ storm lockouts to syslog.");
        }
    }

    report->passed_rules = passed_count;
    report->failed_rules = failed_count;
    report->total_deductions = total_deductions;
    if (total_deductions >= 100) {
        report->posture_score = 0;
    } else {
        report->posture_score = 100 - total_deductions;
    }

    return 0;
}
