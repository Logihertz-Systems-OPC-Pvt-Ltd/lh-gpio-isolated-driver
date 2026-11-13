# Logihertz Isolated Industrial GPIO Driver (`lh-gpio-isolated-driver`)

[![CI](https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-gpio-isolated-driver/actions/workflows/ci.yml/badge.svg)](https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-gpio-isolated-driver/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![Standard](https://img.shields.io/badge/C-C11-00599C.svg)](https://en.cppreference.com/w/c/11)
[![Linux Kernel](https://img.shields.io/badge/Linux-6.6_LTS-FCC624.svg)](https://kernel.org)
[![Security Posture](https://img.shields.io/badge/MAC_Posture-100%2F100_PASS-success.svg)](#10-rule-industrial-io-security-posture-auditor)

**`lh-gpio-isolated-driver`** is a production-grade, zero-dependency **C11 Optocoupled Industrial GPIO Driver, Hardware/Timer Debounce & Edge Interrupt-Storm Guard, Optocoupler CTR Saturation Calculator, Device-Tree Overlay Synthesizer, and 10-Rule Industrial I/O Security Posture Auditor**.

Engineered specifically for autonomous robotics, avionics mission computers, automated guided vehicles (AGVs), and industrial edge AI platforms (such as NVIDIA Jetson Orin / Tegra234 and NXP i.MX8/9) interfacing with harsh 12V/24V PLC field logic, inductive contactors, and high-voltage sensors.

---

## Architecture Overview

```
 [ Field Domain: 12V / 24V PLC ]
        |
   +----+------------------------------------------------+
   |  [Field Signal In: 24V]                             |
   |         |                                           |
   |    [TVS Clamping] (Transient Voltage Suppressor)    |
   |         |                                           |
   |    [R_limit: 2.2k] (Current Limiting Resistor)      |
   |         |                                           |
   |    +----+----+                                      |
   |    | LED     | (V_F ~= 1.25V, I_F ~= 10.3mA)        |
   |    +----+----+                                      |
   |         |                                           |
   |    [Field Ground: GND_FIELD]                        |
   +---------|-------------------------------------------+
             | Galvanic Isolation Barrier (>= 5000 Vrms)
   +---------|-------------------------------------------+
   |    [Logic Ground: GND_LOGIC]                        |
   |         |                                           |
   |    +----+----+                                      |
   |    | Phototransistor | (I_C_avail >= gamma * I_C_req)
   |    +----+----+                                      |
   |         |                                           |
   |    [R_pull: 4.7k] ---> [V_DD: 3.3V Logic Rail]      |
   |         |                                           |
   |   [Debounced In]                                    |
   |         |                                           |
   |  [Kuhn Digital Integrator Filter] (T_deb = 5 ms)    |
   |         |                                           |
   |  [IRQ Storm Limiter Engine] (CWE-400 Rate Limiter)  |
   |         |                                           |
   |  [Linux 6.6 LTS Kernel Chardev /dev/lh_gpio_iso*]   |
   +-----------------------------------------------------+
 [ Host SoC Domain: Jetson Orin Tegra234 / Edge AI ]
```

---

## Key Features

1. **Galvanic Isolation & Field Ground Separation**:
   - Rigorous physical separation between high-voltage field power (`GND_FIELD`) and sensitive SoC digital ground (`GND_LOGIC`).
   - Sizing and verification for $\ge 2500\text{ Vrms}$ (standard) and $5000\text{ Vrms}$ (reinforced) industrial barrier ratings.
2. **Kuhn Digital Integrator Debounce Filter**:
   - Time-window saturation state machine eliminating mechanical relay contact bounce, reed switch chattering, and sub-millisecond EMI transients.
   - Hysteretic saturation rail switching: transitions occur strictly when counter saturates at $c[n] = C_{\max}$ or $c[n] = 0$.
3. **Edge Interrupt-Storm Rate Limiter (CWE-400 Livelock Guard)**:
   - Kernel and userspace rate monitors preventing Denial-of-Service / CPU starvation caused by continuous edge triggers or oscillating lines.
   - Sliding-window frequency tracking with automatic hardware channel masking and configurable cooldown lockout intervals.
4. **Optocoupler CTR Saturation & Thermal Derating Engine**:
   - Comprehensive semiconductor physics calculations: forward LED current $I_F$, phototransistor collector current $I_{C,\text{avail}}$, pull-up collector current $I_{C,\text{req}}$, and overdrive ratio $\gamma_{\text{sat}} \ge 1.50$.
   - Integrates 30% LED lifetime/temperature derating ($\eta_{\text{aging}} = 0.70$) and input series resistor power dissipation checks.
5. **NVIDIA Jetson Orin (Tegra234) Device-Tree Overlay Synthesizer**:
   - Compiles compliant Device Tree source (`.dts`) overlays declaring channel counts, isolation attributes, pin direction locks, and interrupt limits.
6. **Reference Linux 6.6 LTS Platform Driver (`kernel/lh_gpio_iso_kmod.c`)**:
   - Demonstrates modern Linux kernel GPIO descriptor API (`gpiod_get_array`), threaded ONESHOT IRQs (`IRQF_ONESHOT`), lockless `atomic64_t` edge counters, and `0600` chardev nodes with exclusive opening (`O_EXCL`).
7. **10-Rule Security Posture Auditor**:
   - Deterministic policy evaluator checking 10 critical industrial I/O invariants, scoring from 0 to 100 with deduction tracking.
   - Multi-format exporters: ANSI color tables, canonical JSON, and OASIS SARIF v2.1.0.

---

## Mathematical Formulation

### 1. Kuhn Digital Integrator Debounce

Given debounce time window $T_{\text{debounce}}$ and sampling interval $\Delta t_{\text{sample}}$:

$$C_{\max} = \max\left(1, \left\lceil \frac{T_{\text{debounce}}}{\Delta t_{\text{sample}}} \right\rceil\right)$$

For raw input sample $x[n] \in \{0, 1\}$ at step $n$:

$$c[n] = \begin{cases} \min(C_{\max}, c[n-1] + 1) & \text{if } x[n] = 1 \\ \max(0, c[n-1] - 1) & \text{if } x[n] = 0 \end{cases}$$

The debounced output state $Y[n]$ transitions only upon rail saturation:

$$Y[n] = \begin{cases} 1 & \text{if } c[n] = C_{\max} \\ 0 & \text{if } c[n] = 0 \\ Y[n-1] & \text{if } 0 < c[n] < C_{\max} \end{cases}$$

### 2. Optocoupler CTR Saturation Overdrive

To guarantee sharp logic transitions across temperature variations ($-40^\circ\text{C}$ to $+85^\circ\text{C}$) and LED aging:

1. **Forward LED Current**:
   $$I_F = \frac{\max(0, V_{\text{in}} - V_F)}{R_{\text{limit}}}$$

2. **Available Collector Current with Aging Derating**:
   $$I_{C,\text{avail}} = I_F \cdot \left(\frac{\text{CTR}_{\min}}{100.0}\right) \cdot \eta_{\text{aging}}$$

3. **Required Pull-down Collector Current**:
   $$I_{C,\text{req}} = \frac{V_{\text{DD}} - V_{CE,\text{sat}}}{R_{\text{pull}}}$$

4. **Overdrive Saturation Ratio**:
   $$\gamma_{\text{sat}} = \frac{I_{C,\text{avail}}}{I_{C,\text{req}}} \ge 1.50$$

5. **Series Resistor Thermal Dissipation**:
   $$P_{R,\text{limit}} = I_F^2 \cdot R_{\text{limit}} \le P_{\text{rated}}$$

---

## 10-Rule Industrial I/O Security Posture Auditor

| Rule ID | Severity | Ded. | CWE Mapping | Invariant & Audit Rule Description |
|---|---|---|---|---|
| **`LH-GPIO-001`** | **CRITICAL** | **25** | CWE-693 | Galvanic Isolation $\ge 2500\text{ Vrms}$ & Ground Domain Separation |
| **`LH-GPIO-002`** | **CRITICAL** | **25** | CWE-400 | Interrupt-Storm Rate Limiting ($\le 2000\text{ Hz}$) & Threaded ONESHOT IRQ |
| **`LH-GPIO-003`** | **CRITICAL** | **25** | CWE-284 | Insecure Sysfs GPIO Disabled & Chardev Permissions Restricted to `0600` |
| **`LH-GPIO-004`** | **HIGH** | **10** | CWE-682 | Kuhn Digital Integrator Debounce Enabled with $T_{\text{deb}} \ge 1000\ \mu\text{s}$ |
| **`LH-GPIO-005`** | **HIGH** | **10** | CWE-754 | Failsafe Default Output Clamp Active & Isolated $V_{CC2}$ UVLO Monitored |
| **`LH-GPIO-006`** | **HIGH** | **10** | CWE-682 | Optocoupler CTR Saturation Overdrive $\gamma_{\text{sat}} \ge 1.50$ & LED Aging Derating |
| **`LH-GPIO-007`** | **HIGH** | **10** | CWE-662 | Lockless `atomic64_t` Edge Counters & `O_EXCL` Exclusive Chardev Open |
| **`LH-GPIO-008`** | **MEDIUM** | **4** | CWE-1300 | PCB Creepage/Clearance $\ge 6.0\text{ mm}$ & TVS Diode Field Surge Clamping |
| **`LH-GPIO-009`** | **MEDIUM** | **4** | CWE-20 | Strict IOCTL Parameter Bounds Validation & DT Channel Direction Lock |
| **`LH-GPIO-010`** | **LOW** | **1** | CWE-778 | Monotonic Edge Timestamps (`CLOCK_MONOTONIC`) & Syslog Storm Event Logging |

**Scoring Equation**:
$$S_{\text{posture}} = \max\left(0, 100 - \sum \text{Deductions}\right)$$

- **Hardened System**: 0 deductions $\to 100/100$, status **PASS** (Exit: `0`).
- **Vulnerable System**: $25\times 3 + 10\times 4 + 4\times 2 + 1 = 124$ deductions $\to 0/100$, status **FAIL** (Exit: `1`).

---

## Installation & Build Instructions

### Prerequisites
- GCC 10+ or Clang 12+
- GNU Make 4.0+
- Standard POSIX libc and math library (`-lm`)

### Compiling and Running Tests
```bash
# Clone the repository
git clone https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-gpio-isolated-driver.git
cd lh-gpio-isolated-driver

# Build userspace binary and execute 85+ verification tests
make clean all test

# Build with Clang
make CC=clang clean all test
```

---

## CLI Usage & Examples

### 1. Security Posture Audit (`scan`)
```bash
# Audit hardened policy (Table output, exit 0)
./bin/lh-gpio-iso scan fixtures/hardened_gpio_policy.json --format table

# Export audit findings to SARIF format for CI/CD integration
./bin/lh-gpio-iso scan fixtures/hardened_gpio_policy.json --format sarif --output audit.sarif

# Audit vulnerable policy (Table output, exit 1)
./bin/lh-gpio-iso scan fixtures/vulnerable_gpio_policy.json --format table
```

### 2. Contact Bounce Waveform Simulation (`sim-debounce`)
```bash
# Run Kuhn digital integrator against captured relay contact bounce
./bin/lh-gpio-iso sim-debounce fixtures/sample_bounce_trace.json --debounce-us 3000 --format table

# Generate and simulate a synthetic 24V contact bounce trace
./bin/lh-gpio-iso sim-debounce --generate --debounce-us 5000 --sample-us 100
```

### 3. Optocoupler Saturation & Resistor Sizing (`calc-opto`)
```bash
# Standard 24V field input with 2.2k resistor and 100% CTR optocoupler
./bin/lh-gpio-iso calc-opto --vin 24.0 --rlimit 2200 --ctr 100 --aging 0.70 --vdd 3.3 --rpull 4700

# High-voltage 48V input analysis
./bin/lh-gpio-iso calc-opto --vin 48.0 --rlimit 4700 --ctr 80 --aging 0.70 --format json
```

### 4. Device-Tree Overlay Synthesis (`gen-dtoverlay`)
```bash
# Generate Tegra234 / Jetson Orin Device Tree overlay
./bin/lh-gpio-iso gen-dtoverlay --channels 16 --isolation-vrms 5000 --debounce-us 4000 --output dts/tegra234-overlay.dts
```

---

## Repository Structure

```
lh-gpio-isolated-driver/
├── .github/
│   └── workflows/
│       └── ci.yml               # Multi-compiler CI matrix (GCC, Clang)
├── bin/
│   └── lh-gpio-iso              # Userspace verification and simulation CLI
├── dts/
│   └── tegra234-lh-gpio-isolated.dts # NVIDIA Jetson Orin Tegra234 DT overlay
├── fixtures/
│   ├── hardened_gpio_policy.json # Reference hardened policy (100/100 PASS)
│   ├── sample_bounce_trace.json  # 24V relay contact bounce waveform trace
│   └── vulnerable_gpio_policy.json # Vulnerable legacy policy (0/100 FAIL)
├── include/
│   ├── debounce_irq.h           # Kuhn filter, storm limiter & opto API
│   ├── exporters.h              # ANSI table, JSON, SARIF exporters
│   ├── gpio_auditor.h           # 10-rule posture evaluation engine
│   ├── gpio_types.h             # Core data structures and severity enums
│   └── json_parser.h            # Zero-dependency JSON parser
├── kernel/
│   └── lh_gpio_iso_kmod.c       # Reference Linux 6.6 LTS platform driver
├── src/
│   ├── debounce_irq.c           # Kuhn digital integrator implementation
│   ├── exporters.c              # Exporter formatters
│   ├── gpio_auditor.c           # Posture auditor rules implementation
│   ├── json_parser.c            # Zero-dependency JSON parser
│   └── main.c                   # CLI command-line driver
├── tests/
│   └── test_runner.c            # Deterministic test runner (85+ assertions)
├── CHANGELOG.md                 # Semantic versioning release log
├── CODE_OF_CONDUCT.md           # Contributor Covenant Code of Conduct
├── CONTRIBUTING.md              # Contributor guidelines and workflow
├── LICENSE                      # Apache-2.0 License
├── Makefile                     # Build system
├── NOTICE                       # Copyright attribution
├── README.md                    # Project documentation
└── SECURITY.md                  # Industrial threat model and security policy
```

---

## License

Copyright 2026 Logihertz Systems (OPC) Private Limited.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at:

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Maintainer: **Sameer Tawade** (`ops@logihertz.com`)
Organization: **Logihertz Systems (OPC) Private Limited**
Website: [https://github.com/Logihertz-Systems-OPC-Pvt-Ltd](https://github.com/Logihertz-Systems-OPC-Pvt-Ltd)
