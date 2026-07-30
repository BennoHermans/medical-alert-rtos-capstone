# Medical Alert - Real-Time Systems Final Capstone

FreeRTOS medical-alert simulation comparing semaphore and direct-notification interrupt latency under idle, loaded, and fault-injected conditions.

## Demo
- Video: <YouTube / Wokwi link>
- Live Wokwi: HERMANS-FINAL-RTS26Summer

## Architecture
<diagram + 2–3 sentences on the data/control flow>

```mermaid
---
config:
  layout: fixed
  theme: redux
---
flowchart TB
    BTN["GPIO 18 Button<br>Medical patient call input"] -- falling edge interrupt --> ISR["button_isr<br>ISR, not a task<br>IRAM_ATTR"]
    ISR -- sets high/low --> PULSE(["GPIO 19 ISR pulse<br>logic analyzer signal"])
    ISR -- writes timestamp/count --> TIME(["Shared latency telemetry<br>isr_entry_time_us<br>presses_observed<br>latency_max_*"])
    ISR -- xSemaphoreGiveFromISR --> SEM(["Binary semaphore<br>btn_sem"])
    SEM -- wakes --> SEMTASK["btn_task_sem<br>Core 1, priority 12"]
    NOTIF(["Direct task notification<br>task_notif_handle"]) -- wakes --> NOTIFTASK["btn_task_notif<br>Core 1, priority 12"]
    SEMTASK -- reads/updates latency --> TIME
    NOTIFTASK -- reads/updates latency --> TIME
    LOADA["load_task_a<br>Core 1, priority 15<br>10 ms period"] -. can preempt priority 12 bottom halves .-> SEMTASK & NOTIFTASK
    LOADBCD["load_task_b/c/d<br>Core 1, priorities 10/5/2"] -. lower priority, cannot preempt bottom halves .-> NOTIFTASK & SEMTASK
    ISR -- vTaskNotifyGiveFromISR --> NOTIF
```

## Tasks & timing (WCET evidence)
| Task | Period T | WCET C | U=C/T | Priority | Deadline |
|------|---------:|-------:|------:|---------:|---------:|
<rows from the calculator>
Total utilization U = <value>  (RM bound / EDF feasible: <note>)

## Hazard analysis & standard mapping
<hazard, effect, mitigation; mapped to the standard clause>

## Graceful degradation
<what fails, how it is detected, what the system does instead>

## Build & run
<toolchain, board, how to reproduce>

## Tailored for
<target role> — <why these choices fit that role>
