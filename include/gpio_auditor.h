#ifndef LH_GPIO_AUDITOR_H
#define LH_GPIO_AUDITOR_H

#include "gpio_types.h"

/* Evaluate 10-rule security posture against policy */
int audit_gpio_policy(const gpio_policy_t *policy, audit_report_t *report);

#endif /* LH_GPIO_AUDITOR_H */
