# Security Policy

## Supported Versions

Logihertz Systems provides security maintenance and vulnerability updates for the following release branches of `lh-gpio-isolated-driver`:

| Version | Supported          |
|---------|--------------------|
| 0.1.x   | :white_check_mark: |
| < 0.1.0 | :x:                |

## Industrial Threat Model & Security Posture

`lh-gpio-isolated-driver` addresses critical security and reliability boundaries for industrial automation, robotics, avionics, and edge computing platforms interfacing with 12V/24V high-voltage field instrumentation:

1. **Galvanic Isolation & High-Voltage Transient Immunity**:
   - Protection against destructive common-mode voltage spikes, ground loops, and lightning transients (minimum 2500 Vrms, recommended 5000 Vrms).
   - Strict physical and electrical separation of field ground (`GND_FIELD`) from logic ground (`GND_LOGIC`).
2. **Interrupt-Storm Mitigation (CWE-400 CPU Livelock)**:
   - High-frequency contact bounce, EMI burst noise, or hostile signal injection on external interrupt lines can induce kernel livelock.
   - Built-in rate limiting with automatic IRQ masking and configurable cooldown windows protects system responsiveness.
3. **Chardev Access Control (CWE-284 Privilege Escalation)**:
   - Replaces deprecated insecure sysfs GPIO controls (`/sys/class/gpio`) with a restricted character device interface (`/dev/lh_gpio_iso*`) enforced with `0600` permissions.
4. **Deterministic Filtering & Saturation Margins (CWE-682)**:
   - Kuhn digital integrator state machines ensure stable debounce and glitch rejection.
   - Rigorous Current Transfer Ratio (CTR) saturation margin analysis prevents missed pulses due to LED thermal and lifetime degradation.

## Reporting a Vulnerability

If you discover a security vulnerability or flaw in `lh-gpio-isolated-driver`:

1. **Do not disclose publicly** through issue trackers or public forums.
2. Email the maintainers directly at **`ops@logihertz.com`** with the subject line:
   `[SECURITY] Vulnerability in lh-gpio-isolated-driver: <Brief Summary>`
3. Include in your report:
   - Affected component (kernel module, userspace CLI, debounce filter, or auditor engine).
   - Step-by-step reproduction instructions or proof-of-concept trace/policy file.
   - Potential impact on host kernel stability, isolation integrity, or real-time determinism.
4. The Logihertz security team will acknowledge receipt within 48 hours and provide a coordinated disclosure timeline and security advisory.
