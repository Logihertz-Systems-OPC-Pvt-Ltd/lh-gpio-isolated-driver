# Contributing to Logihertz Isolated Industrial GPIO Driver

Thank you for your interest in contributing to `lh-gpio-isolated-driver`. This project provides a production-grade C11 optocoupled industrial GPIO driver, hardware/timer debounce and interrupt-storm guard, optocoupler CTR saturation calculator, device-tree overlay synthesizer, and 10-rule industrial I/O security posture auditor.

## Architectural Mandates & Principles

1. **Zero Third-Party Library Dependencies**:
   - The userspace CLI tool (`bin/lh-gpio-iso`) and test runner (`tests/test_runner`) must depend strictly on standard ISO C11 libc and the standard math library (`-lm`).
   - No external JSON parsers, CLI frameworks, or test frameworks are permitted. All parsers, mathematical engines, and serializers are self-contained.
2. **Strict Compiler Conformance**:
   - Code must compile with zero warnings under both GCC and Clang with:
     ```bash
     -std=c11 -O2 -Wall -Wextra -Werror -pedantic
     ```
3. **Deterministic Memory Safety**:
   - No uninitialized buffers, buffer overflows, or unchecked array accesses.
   - All input buffers must be bounds-checked and validated.
   - Dynamic allocations must be checked and freed cleanly without memory leaks.
4. **Deterministic Physical Modeling**:
   - Kuhn digital integrator arithmetic must be exact and numerically stable.
   - Optocoupler overdrive calculations must adhere to standard physical semiconductor formulations.

## Development Workflow

### Prerequisites

- GCC 10+ or Clang 12+
- GNU Make 4.0+
- Standard POSIX utilities

### Building and Testing

```bash
# Build userspace CLI binary and test runner
make all

# Run deterministic test suite
make test

# Build with Clang (if available)
make CC=clang clean all test

# Clean build artifacts
make clean
```

### Submitting Pull Requests

1. Fork the repository and create a feature branch (`git checkout -b feature/debounce-enhancement`).
2. Adhere to Conventional Commits:
   - `feat(debounce): add hysteretic sampling window`
   - `fix(storm): address counter overflow under high IRQ rates`
   - `test(auditor): add assertion for isolation creepage validation`
3. Ensure `make all test` passes with 100% success and zero compiler warnings.
4. Open a pull request against the `main` branch.
