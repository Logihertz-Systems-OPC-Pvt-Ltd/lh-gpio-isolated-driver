#include "json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void skip_ws(const char **p) {
    while (**p && (isspace((unsigned char)**p) || **p == ',' || **p == ':')) {
        (*p)++;
    }
}

static const char *find_json_key(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char search_buf[128];
    snprintf(search_buf, sizeof(search_buf), "\"%s\"", key);
    
    const char *pos = strstr(json, search_buf);
    while (pos) {
        /* Verify it's a key followed by optional whitespace and ':' */
        const char *after = pos + strlen(search_buf);
        while (*after && isspace((unsigned char)*after)) after++;
        if (*after == ':') {
            after++;
            while (*after && isspace((unsigned char)*after)) after++;
            return after;
        }
        pos = strstr(pos + 1, search_buf);
    }
    return NULL;
}

static int parse_string_val(const char *val_pos, char *out, size_t max_len) {
    if (!val_pos || !out || max_len == 0) return -1;
    skip_ws(&val_pos);
    if (*val_pos != '"') return -1;
    val_pos++;
    
    size_t i = 0;
    while (*val_pos && *val_pos != '"' && i + 1 < max_len) {
        if (*val_pos == '\\' && *(val_pos + 1)) {
            val_pos++;
        }
        out[i++] = *val_pos++;
    }
    out[i] = '\0';
    return 0;
}

static int parse_bool_val(const char *val_pos, bool *out) {
    if (!val_pos || !out) return -1;
    skip_ws(&val_pos);
    if (strncmp(val_pos, "true", 4) == 0) {
        *out = true;
        return 0;
    }
    if (strncmp(val_pos, "false", 5) == 0) {
        *out = false;
        return 0;
    }
    return -1;
}

static int parse_int_val(const char *val_pos, int64_t *out) {
    if (!val_pos || !out) return -1;
    skip_ws(&val_pos);
    if (*val_pos == '"') {
        /* String-encoded integer, e.g. "0600" */
        val_pos++;
        char *endp = NULL;
        long long v = 0;
        if (val_pos[0] == '0' && isdigit((unsigned char)val_pos[1])) {
            v = strtoll(val_pos, &endp, 8);
        } else {
            v = strtoll(val_pos, &endp, 10);
        }
        *out = (int64_t)v;
        return 0;
    }
    char *endp = NULL;
    long long v = strtoll(val_pos, &endp, 10);
    if (endp == val_pos) return -1;
    *out = (int64_t)v;
    return 0;
}

static int parse_double_val(const char *val_pos, double *out) {
    if (!val_pos || !out) return -1;
    skip_ws(&val_pos);
    char *endp = NULL;
    double v = strtod(val_pos, &endp);
    if (endp == val_pos) return -1;
    *out = v;
    return 0;
}

int parse_gpio_policy_json(const char *json_str, gpio_policy_t *policy) {
    if (!json_str || !policy) return -1;
    memset(policy, 0, sizeof(gpio_policy_t));

    const char *pos = NULL;

    /* Policy Name */
    pos = find_json_key(json_str, "policy_name");
    if (pos) parse_string_val(pos, policy->policy_name, sizeof(policy->policy_name));
    else snprintf(policy->policy_name, sizeof(policy->policy_name), "default_policy");

    /* Galvanic Isolation Rating */
    pos = find_json_key(json_str, "galvanic_isolation_vrms");
    if (pos) {
        int64_t val = 0;
        if (parse_int_val(pos, &val) == 0) policy->galvanic_isolation_vrms = (uint32_t)val;
    }

    /* Shared Ground */
    pos = find_json_key(json_str, "shared_field_logic_ground");
    if (pos) parse_bool_val(pos, &policy->shared_field_logic_ground);

    /* IRQ Storm Limit */
    pos = find_json_key(json_str, "irq_storm_limit_hz");
    if (pos) {
        int64_t val = 0;
        if (parse_int_val(pos, &val) == 0) policy->irq_storm_limit_hz = (int32_t)val;
    }

    /* Threaded ONESHOT IRQ */
    pos = find_json_key(json_str, "threaded_oneshot_irq");
    if (pos) parse_bool_val(pos, &policy->threaded_oneshot_irq);

    /* Legacy Sysfs GPIO */
    pos = find_json_key(json_str, "legacy_sysfs_gpio_enabled");
    if (pos) parse_bool_val(pos, &policy->legacy_sysfs_gpio_enabled);

    /* Chardev Permissions */
    pos = find_json_key(json_str, "chardev_permissions_octal");
    if (pos) {
        int64_t val = 0;
        if (parse_int_val(pos, &val) == 0) {
            /* If entered as 600 in decimal notation or 0600 in octal */
            if (val == 600 || val == 0600) {
                policy->chardev_permissions_octal = 600;
            } else {
                policy->chardev_permissions_octal = (uint32_t)val;
            }
        }
    }

    /* Debounce Enabled */
    pos = find_json_key(json_str, "debounce_enabled");
    if (pos) parse_bool_val(pos, &policy->debounce_enabled);

    /* Debounce Window us */
    pos = find_json_key(json_str, "debounce_window_us");
    if (pos) {
        int64_t val = 0;
        if (parse_int_val(pos, &val) == 0) policy->debounce_window_us = (uint32_t)val;
    }

    /* Failsafe Output State */
    pos = find_json_key(json_str, "failsafe_output_state_on_fault");
    if (pos) parse_bool_val(pos, &policy->failsafe_output_state_on_fault);

    /* Isolated VCC2 UVLO */
    pos = find_json_key(json_str, "isolated_vcc2_uvlo_monitor");
    if (pos) parse_bool_val(pos, &policy->isolated_vcc2_uvlo_monitor);

    /* Optocoupler CTR Saturation Ratio */
    pos = find_json_key(json_str, "optocoupler_ctr_saturation_ratio");
    if (pos) parse_double_val(pos, &policy->optocoupler_ctr_saturation_ratio);

    /* LED Aging Derating */
    pos = find_json_key(json_str, "led_aging_derating_applied");
    if (pos) parse_bool_val(pos, &policy->led_aging_derating_applied);

    /* Atomic Edge Counters */
    pos = find_json_key(json_str, "atomic_edge_counters");
    if (pos) parse_bool_val(pos, &policy->atomic_edge_counters);

    /* Chardev Exclusive Open */
    pos = find_json_key(json_str, "chardev_exclusive_open");
    if (pos) parse_bool_val(pos, &policy->chardev_exclusive_open);

    /* Creepage Distance */
    pos = find_json_key(json_str, "isolation_creepage_mm");
    if (pos) parse_double_val(pos, &policy->isolation_creepage_mm);

    /* TVS Field Surge Protection */
    pos = find_json_key(json_str, "tvs_field_surge_protection");
    if (pos) parse_bool_val(pos, &policy->tvs_field_surge_protection);

    /* Strict IOCTL Validation */
    pos = find_json_key(json_str, "strict_ioctl_validation");
    if (pos) parse_bool_val(pos, &policy->strict_ioctl_validation);

    /* Lock Channel Direction in DT */
    pos = find_json_key(json_str, "lock_channel_direction_in_dt");
    if (pos) parse_bool_val(pos, &policy->lock_channel_direction_in_dt);

    /* Monotonic Raw Edge Timestamps */
    pos = find_json_key(json_str, "monotonic_raw_edge_timestamps");
    if (pos) parse_bool_val(pos, &policy->monotonic_raw_edge_timestamps);

    /* Log IRQ Storm Events */
    pos = find_json_key(json_str, "log_irq_storm_events");
    if (pos) parse_bool_val(pos, &policy->log_irq_storm_events);

    return 0;
}

int parse_gpio_policy_file(const char *filepath, gpio_policy_t *policy) {
    if (!filepath || !policy) return -1;
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 1024 * 1024) {
        fclose(f);
        return -1;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }

    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);

    int rc = parse_gpio_policy_json(buf, policy);
    free(buf);
    return rc;
}

int parse_bounce_trace_json(const char *json_str, bounce_trace_t *trace) {
    if (!json_str || !trace) return -1;
    memset(trace, 0, sizeof(bounce_trace_t));

    const char *pos = find_json_key(json_str, "sample_interval_us");
    if (pos) {
        int64_t val = 0;
        if (parse_int_val(pos, &val) == 0 && val > 0) {
            trace->sample_interval_us = (uint32_t)val;
        }
    }
    if (trace->sample_interval_us == 0) {
        trace->sample_interval_us = 100; /* Default 100 microseconds */
    }

    pos = find_json_key(json_str, "samples");
    if (!pos) return -1;

    while (*pos && *pos != '[') pos++;
    if (*pos != '[') return -1;
    pos++;

    uint32_t count = 0;
    while (*pos && *pos != ']' && count < LH_GPIO_MAX_TRACE_SAMPLES) {
        skip_ws(&pos);
        if (*pos == '0' || *pos == '1') {
            trace->samples[count] = (uint8_t)(*pos - '0');
            trace->timestamps_us[count] = (uint64_t)count * trace->sample_interval_us;
            count++;
            pos++;
        } else if (*pos == ',') {
            pos++;
        } else {
            pos++;
        }
    }
    trace->sample_count = count;
    return (count > 0) ? 0 : -1;
}

int parse_bounce_trace_file(const char *filepath, bounce_trace_t *trace) {
    if (!filepath || !trace) return -1;
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 10 * 1024 * 1024) {
        fclose(f);
        return -1;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }

    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);

    int rc = parse_bounce_trace_json(buf, trace);
    free(buf);
    return rc;
}
