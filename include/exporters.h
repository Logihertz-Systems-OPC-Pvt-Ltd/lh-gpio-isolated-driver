#ifndef LH_EXPORTERS_H
#define LH_EXPORTERS_H

#include "gpio_types.h"
#include <stdio.h>

/* Audit Exporters */
int export_audit_table(const audit_report_t *report, FILE *stream);
int export_audit_json(const audit_report_t *report, FILE *stream);
int export_audit_sarif(const audit_report_t *report, FILE *stream);

/* Debounce Simulation Exporters */
int export_debounce_table(const debounce_sim_result_t *result, FILE *stream);
int export_debounce_json(const debounce_sim_result_t *result, FILE *stream);

/* Optocoupler Saturation Exporters */
int export_opto_table(const opto_params_t *params, const opto_result_t *result, FILE *stream);
int export_opto_json(const opto_params_t *params, const opto_result_t *result, FILE *stream);

#endif /* LH_EXPORTERS_H */
