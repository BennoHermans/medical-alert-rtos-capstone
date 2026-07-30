# Project summary 
A simulated medical patient call system that compares binary semaphore and direct task notification wake latency under idle, loaded, and fault injected conditions.

## System Architecture
-	GPIO 18 button represents a patient call event and triggers a falling edge interrupt.
-	The ISR records a timestamp, pulses GPIO 19 for logic analyzer timing, and wakes two bottom half tasks.
-	One bottom half uses a binary semaphore while the other uses a direct task notification.
-	Four periodic workloads create controlled contention on Core 1 using rate monotonic priorities.
-	Task A (priority 15) can preempt the priority 12 bottom halves while task B-D cannot.
## Operating Modes
-	`WITH_LOAD = 0`: baseline run without the four additional workloads causing contention.
-	`WITH_LOAD = 1`: the four periodic background tasks are enabled.
-	`INJECT_MISSING_ISR_YIELD = 1`: omits `portYIELD_FROM_ISR()` to demonstrate delayed task scheduling. 
## Key Concepts
-	ISR to task deferral and ISR safe FreeRTOS APIs
-	Binary semaphore versus direct task notification trade offs
-	Rate monotonic priority assignment, WCET observation, task heartbeats, and fault injection.
-	Logic analyzer/VCD evidence for interrupt response timing.

## Measured Results (100 button presses)
| Configuration | Notif. | Sem. |
|---------------|--------|------|
| No load, normal yield |	30 μs |	2604 μs |
| Loaded, normal yield |	2450 μs |	2646 μs |
| No load, yield removed	| 2279 μs |	2629 μs |
| Loaded, yield removed |	2467 μs |	2728 μs |

## Interpretation of Results
-	The direct task notification produced the lowest measured worst-case latency in both normal operating modes.
-	The notification path increases from 30 μs to 2450 μs under load because higher priority Task A could delay the bottom half.
-	Removing the ISR exit yield increased latency, but the effect was smaller than expected in the loaded run.
-	Representative VCD captures measured 15.35 μs from the GPIO falling edge to GPIO 19 rising edge in both idle and loaded cases.
-	
## Known Limitations
-	The periodic workloads were reduced in weight to prevent repeated watchdog resets.
-	Reported load task timing may include preemption and is best described as maximum observed duration rather than an isolated CPU execution time.
-	The project is a simulation and timing demonstration, not a production medical device.
-	Possible risk of multiple button presses merging into a single event for the binary semaphore whereas task notifications are incremented.

## Portfolio Contents
-	Wokwi project link and demo video.
-	Architecture diagram, task/WCET table, and hazard analysis within the README, supporting work pdf, and final reflection.
