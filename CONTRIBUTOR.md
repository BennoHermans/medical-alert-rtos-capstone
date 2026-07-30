# Contributing

Thank you for your interest in contributing to the **Medical Alert Interrupt Latency Demonstration**.

This project is an educational FreeRTOS and ESP32 simulation that compares binary semaphore and direct task notification wake latency under idle, loaded, and fault injected conditions.

## Development Environment

- ESP32
- ESP IDF with FreeRTOS
- C
- Wokwi simulation
- GitHub Pages for documentation

## Running the Project

1. Open the Wokwi project your ESP-IDF environment.
2. Confirm the push button is connected between GPIO 18 and GND.
3. Confirm GPIO 19 is connected to the logic analyzer.
4. Select the desired configuration in `main`:

```c
#define WITH_LOAD 1
#define INJECT_MISSING_ISR_YIELD 0
```

5. Build and start the simulation.
6. Open the serial monitor.
7. Press the GPIO 18 button to generate a patient-call event.
8. Observe the semaphore and direct notification latency values.

## Test Configurations

| Configuration | `WITH_LOAD` | `INJECT_MISSING_ISR_YIELD` |
|---|---:|---:|
| Idle, normal yield | 0 | 0 |
| Loaded, normal yield | 1 | 0 |
| Idle, yield removed | 0 | 1 |
| Loaded, yield removed | 1 | 1 |

Restart the simulation after changing either compile time setting.

## Contribution Process

1. Fork the repository.
2. Create a branch with a descriptive name.
3. Make one focused change.
4. Verify that the project builds and runs.
5. Test the affected configuration in Wokwi.
6. Update the documentation when behavior or measurements change.
7. Open a pull request describing the change and test results.

## Expectations

- Keep interrupt service routine work short and non blocking.
- Use only ISR safe FreeRTOS APIs inside the ISR.
- Do not add logging, delays, dynamic allocation, or complex processing inside the ISR.
- Preserve the idle, loaded, and fault injection test modes.
- Keep task priorities and timing values clearly documented.
- Use comments to explain design decisions.
- Do not change measured results without including supporting logs or captures.

## Reporting Issues

Include the following information when reporting a problem:

- `WITH_LOAD` value
- `INJECT_MISSING_ISR_YIELD` value
- Wokwi version
- Relevant output
- Steps needed to reproduce the issue
- Screenshots or VCD evidence if possible

## Project Scope

This repository is an educational real time systems demonstration. It is not intended for use as a production medical device or safety certified system.
