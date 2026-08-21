---
name: embedded-iot-cpp-electric
description: "Use when working on embedded programming, IoT devices, C++ firmware, ESP32 or Arduino projects, electric/electronic design, power systems, sensor interfaces, or hardware-software debugging for microcontroller-based products."
---

# Embedded Systems, IoT, C++, and Electric Design Workflow

## When to use this skill
- Troubleshooting firmware on microcontrollers or SoCs
- Building or refactoring C++/Arduino/ESP32/PlatformIO code
- Designing or debugging IoT devices and connected sensors
- Reviewing power, control loops, GPIO, SPI/I2C/UART, and timing issues
- Reasoning about electrical safety, grounding, signal integrity, and hardware assumptions

## Core workflow

### 1. Establish the hardware boundary
- Confirm the target board, MCU, power source, voltage levels, and communication buses.
- Document the exact pins, interfaces, and external components in use.
- Check whether the issue is purely software, purely hardware, or mixed.

### 2. Verify the build and configuration first
- Inspect the platform and board config before editing logic.
- Confirm compiler flags, include paths, library versions, and board definitions.
- Validate that the environment matches the target hardware and framework.

### 3. Reproduce and isolate the failure
- Reduce the problem to the smallest reproducible scenario.
- Log key signals, serial output, sensor values, timing, and state transitions.
- Separate control logic from hardware or peripheral initialization issues.

### 4. Test the electrical assumptions
- Check voltage levels, ground references, pull-ups, pull-downs, and common-mode issues.
- Verify wiring polarity, signal direction, current draw, and reset behavior.
- Treat power instability, boot problems, brownouts, and ADC noise as first-class suspects.

### 5. Form a root-cause hypothesis
- Prefer one testable theory over broad changes.
- Investigate likely failure modes in order: power, reset, clocks, wiring, bus timing, interrupts, memory, and library configuration.
- If a fix depends on hardware behavior, verify the behavior with measurement or observation before finalizing the code change.

### 6. Implement the smallest correct fix
- Change only the cause, not the surrounding system.
- Keep ISR code short, deterministic, and free of blocking operations.
- Favor explicit state machines and bounded loops over complex hidden behavior.
- Preserve compatibility with the target microcontroller’s memory, CPU, and timing limits.

### 7. Validate with evidence
- Rebuild the project for the actual target board.
- Test the specific behavior that failed and confirm the fix.
- Check for serial/log output, timing, stability, and watchdog resets.
- Validate power draw, thermal behavior, and hardware interactions where relevant.

### 8. Document the final state
- Record the root cause, fix, hardware assumptions, and validation evidence.
- Note any known constraints, calibration values, or edge-case behavior affecting the device in the field.

## Decision points

- If the board fails to boot or resets unexpectedly: check power rails, reset circuitry, oscillator stability, and watchdog settings.
- If GPIO or bus communication is unreliable: verify wiring, pull-ups, logic levels, and timing constraints.
- If sensors return noisy or drifting values: inspect ADC reference, grounding, shielding, filtering, and sampling timing.
- If firmware compiles but behaves incorrectly: focus on initialization order, state transitions, interrupt timing, and library usage.
- If the problem is ambiguous across hardware and software: validate the simplest hardware path first, then isolate the firmware layer.

## Quality bar for completion
- The code builds for the target board or framework without avoidable warnings.
- The root cause is explained in terms of hardware and software interaction.
- The fix is minimal, readable, and maintainable.
- Power, timing, and interface assumptions are explicit.
- Validation evidence is present: compile output, logs, measurement, or observed device behavior.
- Safety and electrical correctness are respected before shipping a prototype or deployment build.

## Best practices for embedded and IoT work
- Prefer deterministic code over clever code.
- Keep memory and stack assumptions visible.
- Avoid long blocking delays in time-sensitive paths.
- Use clear pin and bus naming conventions.
- Treat firmware updates as hardware-aware changes, not just software patches.
- Validate wiring and power before chasing subtle software bugs.
- Keep logs structured and actionable when debugging real hardware.

## Example prompts
- "Diagnose why the ESP32 boot loop occurs after adding a sensor on I2C."
- "Refactor this Arduino sketch to improve stability and reduce blocking delays."
- "Review this C++ embedded driver for memory safety, timing issues, and initialization order."
- "Explain the likely power or grounding cause of noisy ADC readings in this IoT device."
- "Help me build a PlatformIO project for an ESP32-S3 display board with stable LVGL rendering."

## Related customizations to create next
- A project-specific firmware debug checklist for PlatformIO and ESP32 boards.
- A C++/embedded code review instruction set for safe driver and interrupt patterns.
- A battery/power validation prompt for low-power IoT hardware design.
- An electronics safety and wiring checklist for maker and prototype builds.
