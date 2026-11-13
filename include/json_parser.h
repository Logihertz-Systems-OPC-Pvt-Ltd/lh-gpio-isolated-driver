#ifndef LH_JSON_PARSER_H
#define LH_JSON_PARSER_H

#include "gpio_types.h"

/* Parse policy configuration from JSON buffer or file */
int parse_gpio_policy_json(const char *json_str, gpio_policy_t *policy);
int parse_gpio_policy_file(const char *filepath, gpio_policy_t *policy);

/* Parse bounce trace waveform from JSON buffer or file */
int parse_bounce_trace_json(const char *json_str, bounce_trace_t *trace);
int parse_bounce_trace_file(const char *filepath, bounce_trace_t *trace);

#endif /* LH_JSON_PARSER_H */
