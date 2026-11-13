# Changelog

All notable changes to `lh-gpio-isolated-driver` will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-11-13

### Added
- **Kuhn Digital Integrator Debounce Engine**:
  - Implemented configurable time-window saturation filter algorithm rejecting contact bounce and EMI spikes.
  - Sub-millisecond sampling counter logic with hysteretic saturation limits.
  - Contact bounce waveform generator and real-time transition analyzer.
- **Edge Interrupt-Storm Rate Limiter (CWE-400)**:
  - Token-bucket / sliding window IRQ rate monitor protecting host kernel against CPU livelock.
  - Automatic channel masking with configurable cooldown lockout windows and audit event logging.
- **Optocoupler Current Transfer Ratio (CTR) Saturation Engine**:
  - Analytical calculator for forward current $I_F$, temperature and aging derating, collector saturation current $I_{C,\text{req}}$, and overdrive ratio $\gamma_{\text{sat}}$.
  - Minimum safe overdrive margin verification ($\gamma_{\text{sat}} \ge 1.50$) and resistor power dissipation checks.
- **Device-Tree Overlay Synthesizer**:
  - Automated DTS overlay code generator for NVIDIA Jetson Orin (Tegra234) and generic embedded Linux targets.
  - Enforces pin direction locks, debounce delay, galvanic isolation attributes, and IRQ rate limits in DT nodes.
- **Linux 6.6 LTS Platform Driver Reference (`kernel/lh_gpio_iso_kmod.c`)**:
  - Modern descriptor-based GPIO API (`gpiod_get_array`) with threaded oneshot IRQ handlers.
  - High-resolution timer (`hrtimer`) sampling loop and lockless `atomic64_t` edge counters.
  - Restricted character device interface (`/dev/lh_gpio_iso*`, mode 0600) with strict `ioctl` command set.
- **10-Rule Security Posture Auditor**:
  - Complete rule implementation (`LH-GPIO-001` through `LH-GPIO-010`) covering galvanic isolation, storm limiting, sysfs deprecation, debounce filtering, failsafe outputs, CTR overdrive, atomic counters, creepage distance, strict IOCTLs, and monotonic timestamps.
  - Multi-format reporting in ANSI colored tables, canonical JSON, and OASIS SARIF v2.1.0 format.
- **Userspace Command-Line Interface (`bin/lh-gpio-iso`)**:
  - High-performance zero-dependency CLI providing `scan`, `sim-debounce`, `calc-opto`, `gen-dtoverlay`, `--version`, and `--help`.
- **Comprehensive Deterministic Verification Suite**:
  - Self-contained test runner (`tests/test_runner`) executing 85+ rigorous assertions.
