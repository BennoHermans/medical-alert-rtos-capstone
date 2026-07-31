# Results and Analysis

This document contains all data results, fault injection, and hazard analysis.

## Supporting Work
Link to Supporting Document Displaying Screenshots of:
- System Architecture diagram
- Simulated Device
- Latency Measurements/Logging for both types of load
- Induced Failure for examination
- A Rate Monotonic Scheduling Demonstration for the System
- VCD File Photos

[https://docs.google.com/document/d/1a35MclGWcAXferpTt4EepKjPaPM3lRFfPcIsqnxgWd8/edit?tab=t.0
](https://docs.google.com/document/d/1FozwS1DciWfD8zV1R5hGs88mmnGM5QEti6sUAQWQi54/edit?tab=t.0)

**Note:** Can also be found as a pdf within the Github pages.

## Task / ISR Table

| | Type | Core | Period / Trigger | Role | WCET | U = C/T | Deadline | Priority |
|-|------|------|------------------|------|------|---------|----------|----------|
| GPIO 18 Button | Hardware interrupt source | N/A | Button falling edge | Medical patient call button input | N/A | N/A | N/A | N/A | 
| `button_isr` | ISR, not a task | Hardware interrupt | GPIO 18 falling edge | Debounce, timestamp, pulse GPIO 19, signal bottom halves | N/A | N/A | N/A |Interrupt context | 
| `btn_task_notif` | Bottom-half task | Core 1 | Event driven | Handles patient call event through notification path | 2467 us | N/A | N/A | 12 |
| `btn_task_sem` | Bottom-half task | Core 1 | Event driven | Handles patient call event through semaphore path | 2728 us | N/A | N/A | 12 |
| `load_task_a` | Background load task | Core 1 | 10 ms | Higher-priority load task. Can delay bottom half tasks | 138 us | 0.014 | 15 ms | 15 |
| `load_task_b` | Background load task | Core 1 | 20 ms | Lower than bottom-half priority. Cannot preempt bottom half tasks | 12599 us | 0.630 | 30 ms | 10 |
| `load_task_c` | Background load task | Core 1 | 50 ms | Lower than bottom-half priority. Cannot preempt bottom half tasks | 10589 us | 0.212 | 75 ms | 5 |
| `load_task_d` | Background load task | Core 1 | 100 ms | Lower than bottom-half priority. Cannot preempt bottom half tasks | 10413 us | 0.104 | 150 ms | 2 |

U = 0.96 which is higher than the RM bound of 0.757 making the system possibly schedulable under rate monotonic scheduling with testing. After performing the tests it was seen
that the heartbeats of each task incremented consistently confirming there was no starvation and RMS works.

The load task timer records elapsed time around each work load. For lower priority tasks this interval includes time spent preempted making these
values the maximum observed task duration not isolated execution times.

---

## Latency-max Table

Both signaling paths were left enabled during measurement because the scaffold is designed around comparing the semaphore and notification 
paths under the same interrupt event. I recorded the maximum latency printed by each path after 100 button presses.

| Load Type              | Notification Max Latency | Semaphore Max Latency |
|------------------------|--------------------------|-----------------------|
| No load, normal yield  |   30 us                  | 2604 us               |     
| Loaded, normal yield   | 2450 us                  | 2646 us               |
| No load, yield removed | 2279 us                  | 2629 us               |     
| Loaded, yield removed  | 2467 us                  | 2728 us               |

## Total Interrupt Response Time

To make this calculation a representative GPIO 18 falling edge was chosen.

| Load Type   | GPIO 18 Falling Edge | GPIO 19 Rising Edge | Total Time (GPIO 19 Rising Edge - GPIO 18 Falling Edge) |
|-------------|----------------------|---------------------|--------------------------------|
| WITH_LOAD 0 | 4.217061942 s        | 4.217077292 s       | 15.35 us                       |   
| WITH_LOAD 1 | 19.959745806 s       | 19.959761156 s      | 15.35 us                       |   

In this setup, the measured GPIO 18 to GPIO 19 response time stayed the same in both idle and loaded runs. 
This suggests the background tasks did not significantly affect ISR entry timing while they did affect bottom half latency.
This is likely because the ISR runs above all other tasks so loads should not delay entry and the structure within 
the ISR is unchanged.

---

## Engineering analysis

### What's in the ISR? What's NOT?

Below is each line with a sentence on the need/use of each line.

```c
IRAM_ATTR
```
This line which is part of the ISR creation is an important addition as it is what allows consistent
memory fetches to IRAM to aid latency critical handlers.

```c
int64_t now = esp_timer_get_time();
if (now - last_edge_us < DEBOUNCE_US) return;
last_edge_us = now;
```
These three lines work together in the recording of ISR entry time and performance of two necessary actions. The first action is to check debounce time has passed without a delay which
risks throwing off timing and a fast exit if debounce time has not been met. The second action is tracking of the last edge so that debounce time
can be tracked for the next press.

```c
gpio_set_level(ISR_PULSE_GPIO, 1);
```
This line is what drives GPIO 19 high so the logic analyzer can see the ISR beginning.

```c
isr_entry_time_us = now;
presses_observed++;
```
These update values that can then be processed in the bottom-half where the work is properly deferred to.

```c
BaseType_t higher_woken = pdFALSE;
```
This variable is necessary for tracking if a waking task should cause a context switch upon ISR exit.

```c
xSemaphoreGiveFromISR(alert_sem, &higher_woken);
```
This is necessary for signaling to the semaphore based bottom-half to be take and run using the ISR safe API.

```c
vTaskNotifyGiveFromISR(alert_task_notif_handle, &higher_woken);
```
This is necessary for signaling the direct task notification bottom-half to run using a ISR safe API.

```c
gpio_set_level(ISR_PULSE_GPIO, 0);
```
This line is needed to drop the GPIO 19 signal to low allowing the logic analyzer to see the ISR pulse.

```c
portYIELD_FROM_ISR(higher_woken);
```
This allows for the appropriate context switching if a higher priority task has been awoken.

What is not in the ISR are printf, logs, malloc, delays, or complex functions to avoid blocking or excessively long
interrupts. These actions are instead deferred properly to the bottom-half semaphore and notification tasks with
the ISR remaining short, with safe APIs, captured time stamps using `esp_timer_get_time()`, and IRAM_ATTR.


### Binary Semaphore vs Direct Task Notification
**Which is faster? Why?**

I measured both paths for 100 button presses. In both runs (`WITH_LOAD = 0` and `WITH_LOAD = 1`), the direct task notification had the lower worst-case latency.

In the idle run, the notification path was much faster at 30 us compared to 2604 us for the semaphore path. In the loaded run, the notification path was still 
faster at 2450 us compared to 2646 us for the semaphore path, but the difference between the two paths became much smaller.

The task notification was faster based on my results. This makes sense because the notification path directly wakes a specific task
while the semaphore path uses a separate object that the bottom half task must first take. Since the assignment says the internal details will be covered later
I am only using the measured Wokwi results to choose the faster path which is the notification path.

### Latency Under Load
**idle (`WITH_LOAD 0`) vs loaded (`WITH_LOAD 1`) numbers. By what factor does latency increase?**

While under load the latency of the notification path increased from 30 us to 2450 us meaning an 81.67 times increase in latency. (2450/30 = 81.67)

The semaphore path increased from 2604 us to 2646 us under load which is only a 1.02 times increase. (2646/2604 = 1.02)

These differences are odd and worth noting. It is odd that when idle the semaphore is already experience close to its worst latencies.

The worst case increase is being caused by task A which runs at a priority of 15 while the bottom half tasks run at a priority of 12. This means that task A can preempt the bottom half tasks making them wait.
The worst case latency is not caused by tasks B, C, or D because those three are outranked by the bottom half tasks meaning they cannot preempt the bottom half tasks and cause further latency.

### Induced Failure
**Rule broken, predicted symptom, observed symptom, and how they match (or don't).**

The rule broken was omitting `portYIELD_FROM_ISR`. I predicted that the ISR would still signal both bottom halves, but they would not be scheduled immediately on ISR exit.
Under load, notification maximum latency increase from 2450 us to 2467 us while semaphore maximum latency increase from 2646 us to 2728 us. The effect was measureable but 
smaller than expected, particularly for the notification path. This partially supports the prediction that removing the yield delayed execution but the existing scheduling and
artificaial work load already dominated much the worst case latency.

---

## Hazard analysis

| Hazard | Effect | Mitigation  |
|--------|--------|------------|
| ISR exit yield removed | Delayed patient call processing | Proper yielding was put in place and its removal is optional using fault injection to measure latency changes. |
| Excessive high priority load | Bottom half delay | Kept high priority tasks minimal |
| Task Starvation | Background operation stops | Heartbeat monitor gives a visual for detecting stalled counters |
| Long ISR processing | Increased interrupt blocking | Deferred as much work as possible to bottom half tasks |
| Binary Semaphore merging events | Multiple presses could be at risk by one wake | Notification count is tracked on the other hand. The limitation is noted and the debouncing doubles as time to allow for processing. |

---

## Graceful Degradation

This project demonstrates degraded timing behavior rather than an automatic
graceful degradation response.

When `portYIELD_FROM_ISR()` is omitted, the ISR still signals both bottom-half
tasks, but they may not run immediately after the interrupt exits. The fault is
identified through increased measured wake latency in the serial logs.

The system continues operating and still processes patient call events, but
with reduced timing responsiveness. A production version would detect repeated
deadline violations and enter a fault state.
