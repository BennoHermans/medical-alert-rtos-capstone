# Dual-Core Architecture and Design

## Core 1
A GPIO 18 button represents a patient call event and triggers a falling edge interrupt. The ISR records a timestamp, pulses GPIO 19 for logic analyzer timing, and wakes two bottom half tasks. One bottom half uses a binary semaphore while the other uses a direct task notification. Four periodic workloads create controlled contention on Core 1 using rate monotonic priorities. Task A (priority 15) can preempt the priority 12 bottom halves while task B-D cannot.

[![image](https://github.com/BennoHermans/medical-alert-rtos-capstone/blob/966a6aaf5c976bb8c262d9157645269f5c02f9d1/SystemArchitecture.jpg)](https://github.com/BennoHermans/medical-alert-rtos-capstone/blob/main/SystemArchitecture.jpg?raw=true)

Link directly to public Wokwi Project: [https://wokwi.com/projects/470700742254929921](https://wokwi.com/projects/470700742254929921)

# Core 0

The application separates real time processing from monitoring and observability:

- **Core 1 - Real time plane:** Runs the semaphore and direct notification alert handlers along with the controlled background workload.
- **Core 0 - Observability plane:** Runs the low priority monitoring task and system services.

Core pinning prevents logging and monitoring activity from unpredictably interfering with latency sensitive alert handling. 
The background tasks are intentionally placed on the real time core to create measurable contention.

The GPIO ISR acts as the producer recording the event timestamp and signaling two consumer tasks:

- A binary semaphore provides conventional ISR to task synchronization but may merge repeated events while already available.
- A direct task notification provides a lower overhead one to one signaling path and can retain a pending event count.

Both consumer tasks block until signaled, measure wake-up latency, and process the same patient call event. 
The producer/consumer contract requires the ISR to perform only bounded, non-blocking work, while all logging and event processing occur in task context.

<img width="900" height="1056" alt="image" src="https://github.com/user-attachments/assets/86c980c2-5562-49d3-84e8-b17d66679394" />
