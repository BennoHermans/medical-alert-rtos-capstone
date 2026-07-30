# Demo Video Script / Transcript

This is the voiceover script/transcript for the demo video, provided for accessibility
(for viewers without audio) and as a record of what is shown and said.

---

## Theme + System in One Sentence

**Visual:** README header / architecture diagram

This is a FreeRTOS medical alert system with a  simulated patient call button on an
ESP32 that compares two ways of waking a task from an interrupt, a binary
semaphore and a direct task notification, and measures how their response
latency changes under CPU load and under an intentionally injected fault.

---

## Live Demo

**Visual:** Wokwi simulation running; button presses; serial monitor output; task
monitor table with heartbeats/WCET

Here's the system running in Wokwi. GPIO 18 is the patient call button, and
GPIO 19 pulses high during ISR execution so a logic analyzer can time it.

When I press the button, the ISR debounces the input, timestamps the event,
pulses GPIO 19, and signals both bottom half tasks. One through a semaphore,
one through a direct notification.

In the serial monitor, you can see both paths report their latency after each
press. Here it is no background load displaying just path latencies.

Now I'll enable WITH_LOAD, which brings in four periodic background tasks at
different priorities to create realistic contention on Core 1.

---

## Diagram + Task Table Walkthrough

**Visual:** Architecture diagram; task table; RMS scheduling diagram/timeline;
latency table

Here's the system architecture. The ISR is minimal with no printf, no delays, no
blocking calls and it just timestamps, pulses the GPIO, and signals both bottom
halves using ISR safe APIs before yielding.

The task table shows the priority structure. The two bottom half tasks run at
priority 12. Task A runs at priority 15 which is higher than the bottom halves so
it can preempt them and add latency. Tasks B, C, and D run lower, at 10, 5,
and 2, so they can't preempt the bottom halves at all.

The measured results back this up since when under load, the notification path's
worst-case latency jumped from 30 microseconds to 2450, roughly an 81x
increase, almost entirely explained by Task A preempting it. The semaphore
path barely changed, because it was already near its worst case even when
idle.

---

## Induced Failure / Fault Injection

**Visual:** INJECT_MISSING_ISR_YIELD build running; serial output with higher
latency numbers; side-by-side comparison with earlier numbers

For fault injection, I removed portYIELD_FROM_ISR from the ISR exit. The rule
this breaks is that after signaling a higher-priority task from an ISR,
you're supposed to request an immediate context switch otherwise the woken
task has to wait for the next natural scheduling point.

I predicted this would meaningfully delay both bottom-half tasks. Here's what
I measured under load. Notification latency went from 2450 to 2467
microseconds, and semaphore latency went from 2646 to 2728. The effect was
real but smaller than I expected, especially for notifications.

The reason is that the background workload was already the dominant source
of latency — removing the yield adds a smaller delay on top of contention
that was already there. Importantly, the system didn't crash or lose events
it degraded gracefully, just with worse worst case timing, which is
the kind of failure mode a real medical device would need to detect and
respond to.

---

## What's Next / What Scales to Production

**Visual:** README closing / GitHub Pages / repo link

As a simulation, this proves the signaling and priority design, but a
production version would need a few things this doesn't have such as isolated CPU
execution time measurements instead of just durations that include preemption, an
automated test instead of manually pressing a button 100 times, and
a real fault response like detecting repeated deadline violations and
entering a defined fault state, rather than just logging a larger latency
number.
