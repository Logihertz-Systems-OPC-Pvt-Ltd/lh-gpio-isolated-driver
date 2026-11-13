#include "gpio_types.h"
#include "debounce_irq.h"
#include "gpio_auditor.h"
#include "exporters.h"
#include "json_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LH_GPIO_ISO_VERSION "0.1.0"

static void print_version(void) {
    printf("lh-gpio-iso %s\n", LH_GPIO_ISO_VERSION);
    printf("Logihertz Isolated Industrial GPIO Driver & Security Posture Auditor\n");
    printf("Copyright 2026 Logihertz Systems (OPC) Private Limited\n");
    printf("Author: Sameer Tawade <ops@logihertz.com>\n");
}

static void print_help(const char *prog_name) {
    print_version();
    printf("\nUsage:\n");
    printf("  %s <subcommand> [options]\n\n", prog_name);
    printf("Subcommands:\n");
    printf("  scan <policy.json>                Audit GPIO security posture against 10 industrial rules\n");
    printf("  sim-debounce <trace.json>         Simulate Kuhn digital integrator against contact bounce\n");
    printf("  calc-opto                         Calculate optocoupler CTR saturation & thermal limits\n");
    printf("  gen-dtoverlay                     Generate Tegra234 / Jetson Orin Device-Tree Overlay\n");
    printf("  --version, -v                     Display version and author information\n");
    printf("  --help, -h                        Display this help manual\n\n");
    printf("Options for 'scan':\n");
    printf("  --format <table|json|sarif>       Output formatting (default: table)\n");
    printf("  --output <filepath>               Write output report to file instead of stdout\n\n");
    printf("Options for 'sim-debounce':\n");
    printf("  --debounce-us <us>                Debounce time window in microseconds (default: 5000)\n");
    printf("  --sample-us <us>                  Sampling interval in microseconds (default: 100)\n");
    printf("  --generate                        Generate a synthetic relay contact bounce trace\n");
    printf("  --format <table|json>             Output formatting (default: table)\n");
    printf("  --output <filepath>               Write output to file instead of stdout\n\n");
    printf("Options for 'calc-opto':\n");
    printf("  --vin <volts>                     Field input voltage (e.g. 24.0)\n");
    printf("  --rlimit <ohms>                   Series limiting resistor (e.g. 2200)\n");
    printf("  --vf <volts>                      LED forward voltage drop (default: 1.25)\n");
    printf("  --ctr <percent>                   Minimum datasheet CTR percentage (default: 100)\n");
    printf("  --aging <factor>                  LED aging/thermal derating factor (default: 0.70)\n");
    printf("  --vdd <volts>                     Logic domain supply voltage (default: 3.3)\n");
    printf("  --vce-sat <volts>                 Collector-emitter saturation voltage (default: 0.20)\n");
    printf("  --rpull <ohms>                    Pull-up resistor on logic collector (default: 4700)\n");
    printf("  --opto-cap <pF>                   Phototransistor output capacitance (default: 15.0)\n");
    printf("  --format <table|json>             Output formatting (default: table)\n");
    printf("  --output <filepath>               Write output to file instead of stdout\n\n");
    printf("Options for 'gen-dtoverlay':\n");
    printf("  --channels <num>                  Number of isolated GPIO channels (default: 8)\n");
    printf("  --isolation-vrms <volts>          Galvanic isolation rating in Vrms (default: 5000)\n");
    printf("  --debounce-us <us>                Hardware/timer debounce delay (default: 5000)\n");
    printf("  --max-irq-rate <Hz>               Maximum allowed IRQ rate before lockout (default: 500)\n");
    printf("  --cooldown-ms <ms>                Cooldown lockout period in ms (default: 1000)\n");
    printf("  --failsafe <low|high>             Failsafe default state on fault (default: low)\n");
    printf("  --output <filepath>               Write DTS overlay to file instead of stdout\n\n");
    printf("Examples:\n");
    printf("  %s scan fixtures/hardened_gpio_policy.json --format table\n", prog_name);
    printf("  %s sim-debounce fixtures/sample_bounce_trace.json --debounce-us 5000\n", prog_name);
    printf("  %s calc-opto --vin 24.0 --rlimit 2200 --ctr 100 --aging 0.70\n", prog_name);
    printf("  %s gen-dtoverlay --channels 16 --isolation-vrms 5000 --output dts/overlay.dts\n", prog_name);
}

static int handle_scan(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: 'scan' requires a policy JSON file argument.\n");
        fprintf(stderr, "Usage: lh-gpio-iso scan <policy.json> [--format table|json|sarif] [--output <path>]\n");
        return 2;
    }

    const char *policy_file = argv[2];
    const char *format = "table";
    const char *output_file = NULL;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else {
            fprintf(stderr, "Unknown option for scan: %s\n", argv[i]);
            return 2;
        }
    }

    gpio_policy_t policy;
    if (parse_gpio_policy_file(policy_file, &policy) != 0) {
        fprintf(stderr, "Error: Failed to parse policy JSON file: %s\n", policy_file);
        return 2;
    }

    audit_report_t report;
    if (audit_gpio_policy(&policy, &report) != 0) {
        fprintf(stderr, "Error: Failed to evaluate policy security posture.\n");
        return 2;
    }

    FILE *out_stream = stdout;
    if (output_file) {
        out_stream = fopen(output_file, "w");
        if (!out_stream) {
            fprintf(stderr, "Error: Unable to open output file: %s\n", output_file);
            return 2;
        }
    }

    if (strcmp(format, "json") == 0) {
        export_audit_json(&report, out_stream);
    } else if (strcmp(format, "sarif") == 0) {
        export_audit_sarif(&report, out_stream);
    } else {
        export_audit_table(&report, out_stream);
    }

    if (output_file) {
        fclose(out_stream);
        printf("Report written to %s\n", output_file);
    }

    /* Return 0 if posture score >= 80, 1 if posture score < 80 */
    return (report.posture_score >= 80) ? 0 : 1;
}

static int handle_sim_debounce(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: 'sim-debounce' requires a trace JSON file or --generate.\n");
        fprintf(stderr, "Usage: lh-gpio-iso sim-debounce <trace.json> [--debounce-us <us>] [--sample-us <us>] [--format table|json]\n");
        return 2;
    }

    const char *trace_file = argv[2];
    uint32_t debounce_us = 5000;
    uint32_t sample_us = 100;
    bool generate_mode = false;
    const char *format = "table";
    const char *output_file = NULL;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--debounce-us") == 0 && i + 1 < argc) {
            debounce_us = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--sample-us") == 0 && i + 1 < argc) {
            sample_us = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--generate") == 0) {
            generate_mode = true;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    bounce_trace_t trace;
    if (generate_mode || strcmp(trace_file, "--generate") == 0) {
        if (generate_synthetic_bounce_trace(20000, 4000, sample_us, &trace) != 0) {
            fprintf(stderr, "Error: Failed to generate synthetic bounce trace.\n");
            return 2;
        }
    } else {
        if (parse_bounce_trace_file(trace_file, &trace) != 0) {
            fprintf(stderr, "Error: Failed to parse trace JSON file: %s\n", trace_file);
            return 2;
        }
    }

    debounce_sim_result_t result;
    if (run_debounce_simulation(&trace, debounce_us, sample_us, &result) != 0) {
        fprintf(stderr, "Error: Debounce simulation failed.\n");
        return 2;
    }

    FILE *out_stream = stdout;
    if (output_file) {
        out_stream = fopen(output_file, "w");
        if (!out_stream) {
            fprintf(stderr, "Error: Unable to open output file: %s\n", output_file);
            return 2;
        }
    }

    if (strcmp(format, "json") == 0) {
        export_debounce_json(&result, out_stream);
    } else {
        export_debounce_table(&result, out_stream);
    }

    if (output_file) {
        fclose(out_stream);
        printf("Simulation results written to %s\n", output_file);
    }

    return 0;
}

static int handle_calc_opto(int argc, char **argv) {
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

    const char *format = "table";
    const char *output_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--vin") == 0 && i + 1 < argc) {
            params.vin_v = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--rlimit") == 0 && i + 1 < argc) {
            params.rlimit_ohm = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--vf") == 0 && i + 1 < argc) {
            params.vf_v = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--ctr") == 0 && i + 1 < argc) {
            params.ctr_min_percent = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--aging") == 0 && i + 1 < argc) {
            params.aging_factor = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--vdd") == 0 && i + 1 < argc) {
            params.vdd_v = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--vce-sat") == 0 && i + 1 < argc) {
            params.vce_sat_v = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--rpull") == 0 && i + 1 < argc) {
            params.rpull_ohm = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--opto-cap") == 0 && i + 1 < argc) {
            params.opto_cap_pf = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    opto_result_t result;
    if (calc_optocoupler_saturation(&params, &result) != 0) {
        fprintf(stderr, "Error: Optocoupler calculation failed due to invalid parameters.\n");
        return 2;
    }

    FILE *out_stream = stdout;
    if (output_file) {
        out_stream = fopen(output_file, "w");
        if (!out_stream) {
            fprintf(stderr, "Error: Unable to open output file: %s\n", output_file);
            return 2;
        }
    }

    if (strcmp(format, "json") == 0) {
        export_opto_json(&params, &result, out_stream);
    } else {
        export_opto_table(&params, &result, out_stream);
    }

    if (output_file) {
        fclose(out_stream);
        printf("Analysis written to %s\n", output_file);
    }

    return (result.is_saturated && result.rlimit_power_safe) ? 0 : 1;
}

static int handle_gen_dtoverlay(int argc, char **argv) {
    dtoverlay_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    snprintf(opts.node_name, sizeof(opts.node_name), "lh_gpio_isolated");
    snprintf(opts.compatible, sizeof(opts.compatible), "logihertz,gpio-isolated-v1");
    opts.channel_count = 8;
    opts.isolation_vrms = 5000;
    opts.debounce_delay_us = 5000;
    opts.max_irq_rate_hz = 500;
    opts.cooldown_ms = 1000;
    snprintf(opts.failsafe_state, sizeof(opts.failsafe_state), "low");
    opts.chardev_mode_octal = 0600;

    const char *output_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--channels") == 0 && i + 1 < argc) {
            opts.channel_count = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--isolation-vrms") == 0 && i + 1 < argc) {
            opts.isolation_vrms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--debounce-us") == 0 && i + 1 < argc) {
            opts.debounce_delay_us = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--max-irq-rate") == 0 && i + 1 < argc) {
            opts.max_irq_rate_hz = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--cooldown-ms") == 0 && i + 1 < argc) {
            opts.cooldown_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--failsafe") == 0 && i + 1 < argc) {
            snprintf(opts.failsafe_state, sizeof(opts.failsafe_state), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    char dts_buffer[4096];
    if (generate_dtoverlay(&opts, dts_buffer, sizeof(dts_buffer)) != 0) {
        fprintf(stderr, "Error: Failed to synthesize Device Tree overlay.\n");
        return 2;
    }

    if (output_file) {
        FILE *f = fopen(output_file, "w");
        if (!f) {
            fprintf(stderr, "Error: Failed to write to output file: %s\n", output_file);
            return 2;
        }
        fputs(dts_buffer, f);
        fclose(f);
        printf("Device-Tree overlay written to %s\n", output_file);
    } else {
        fputs(dts_buffer, stdout);
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        print_version();
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "scan") == 0) {
        return handle_scan(argc, argv);
    }

    if (strcmp(argv[1], "sim-debounce") == 0) {
        return handle_sim_debounce(argc, argv);
    }

    if (strcmp(argv[1], "calc-opto") == 0) {
        return handle_calc_opto(argc, argv);
    }

    if (strcmp(argv[1], "gen-dtoverlay") == 0) {
        return handle_gen_dtoverlay(argc, argv);
    }

    fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
    print_help(argv[0]);
    return 2;
}
