/*
 * Interrupts & bottom-half pattern
 *
 * @brief FreeRTOS medical alert interrupt latency demonstration
 * 
 * A button generates a GPIO interrupt representing a patient call event.
 * The ISR wakes two bottom half task using:
 *
 * 1. A Binary Semaphore
 * 2. A Direct Task Notification
 *
 * Optional periodic background tasks create controlled CPU contention so the 
 * wake up latency of both mechanisms can be compared under idle and loaded
 * conditions. GPIO 19 pulses during ISR execution for logic analyzer timing.
 *
 * Run modes:
 *  WITH_LOAD = 0: Run without the artificial CPU workload.
 *  WITH_LOAD = 1: Run with four periodic tasks on Core 1.
 *
 * Fault injection modes:
 *  INJECT_MISSING_ISR_YIELD = 0: Normal ISR exit yield.
 *  INJECT_MISSING_ISR_YIELD = 1: Skip the ISR exit yield.
 *
 * ============================================================
 * Theme: Medical Alert
 * ============================================================
 */

#ifndef WITH_LOAD
#define WITH_LOAD 1
#endif

#ifndef INJECT_MISSING_ISR_YIELD
#define INJECT_MISSING_ISR_YIELD 0
#endif

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"

#define BUTTON_GPIO   GPIO_NUM_18      /* input — button to GND */
#define ISR_PULSE_GPIO GPIO_NUM_19     /* output — scope this for latency */
#define DEBOUNCE_US   250

#define ALERT_TASK_STACK_SIZE   4096
#define MONITOR_TASK_STACK_SIZE 4096
#define LOAD_A_STACK_SIZE       2048
#define LOAD_B_STACK_SIZE       2048
#define LOAD_C_STACK_SIZE       2048
#define LOAD_D_STACK_SIZE       2048

#define LOAD_A_PERIOD_MS        10
#define LOAD_B_PERIOD_MS        20
#define LOAD_C_PERIOD_MS        50
#define LOAD_D_PERIOD_MS        100

#define MONITOR_TASK_PRIORITY   1
#define ALERT_TASK_PRIORITY     12
#define LOAD_A_PRIORITY         15
#define LOAD_B_PRIORITY         10
#define LOAD_C_PRIORITY         5
#define LOAD_D_PRIORITY         2

#define CONFIG_LOG_DEFAULT_LEVEL_INFO 1
#define CONFIG_LOG_MAXIMUM_LEVEL  5

static const char *TAG = "final";

/* Signaling primitives */
static SemaphoreHandle_t alert_sem;            /* binary semaphore path */
static TaskHandle_t      alert_task_notif_handle;  /* direct notification path */

/* Latency telemetry */
static volatile int64_t isr_entry_time_us;
static volatile uint32_t presses_observed;
static volatile uint64_t latency_max_sem_us;
static volatile uint64_t latency_max_notif_us;

/* Debounce — track time of last accepted edge */
static volatile int64_t last_edge_us;

/* ============================================================
 *  ISR — runs in interrupt context. IRAM_ATTR avoids the
 *  first-execution cache-fill penalty from flash.
 * ============================================================ */
static void IRAM_ATTR button_isr(void *arg)
{
    int64_t now = esp_timer_get_time();

    /* Debounce: drop edges within DEBOUNCE_US of last accepted one. */
    if (now - last_edge_us < DEBOUNCE_US) return;
    last_edge_us = now;

    /* 1. Toggle the scope output HIGH so the logic analyzer can see ISR entry. */
    gpio_set_level(ISR_PULSE_GPIO, 1);

    isr_entry_time_us = now;
    presses_observed++;

    BaseType_t higher_woken = pdFALSE;

    /* 2. Signal via binary semaphore.
     *    Multiple presses while taken can be LOST — binary sem has no count. */
    xSemaphoreGiveFromISR(alert_sem, &higher_woken);

    /* 3. Signal via direct task notification.
     *    Faster than the semaphore on most ports; one-to-one. */
    vTaskNotifyGiveFromISR(alert_task_notif_handle, &higher_woken);

    /* 4. Toggle scope output LOW — ISR is about to return. */
    gpio_set_level(ISR_PULSE_GPIO, 0);

    /* 5. Request a context switch on ISR exit if a higher-priority task is ready. */
#if !INJECT_MISSING_ISR_YIELD
  portYIELD_FROM_ISR(higher_woken);
#endif

}

/* ============================================================
 *  Bottom-half task: binary-semaphore path
 * ============================================================ */
static void btn_task_sem(void *arg)
{
    for (;;) {
        if (xSemaphoreTake(alert_sem, portMAX_DELAY) == pdTRUE) {
            int64_t wake = esp_timer_get_time();
            int64_t lat = wake - isr_entry_time_us;
            if ((uint64_t)lat > latency_max_sem_us) latency_max_sem_us = (uint64_t)lat;

            // Process and report the patient call event in task context.
            ESP_LOGI(TAG, "[sem] patient call button press #%lu  latency=%lld us (max=%llu)",
                     (unsigned long)presses_observed,
                     (long long)lat,
                     (unsigned long long)latency_max_sem_us);
        }
    }
}

/* ============================================================
 *  Bottom-half task: direct-notification path
 * ============================================================ */
static void btn_task_notif(void *arg)
{
    for (;;) {
        uint32_t count = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (count == 0) continue;

        int64_t wake = esp_timer_get_time();
        int64_t lat = wake - isr_entry_time_us;
        if ((uint64_t)lat > latency_max_notif_us) latency_max_notif_us = (uint64_t)lat;

        /*
        * Process the same patient call event through a direct task notification.
        * Both paths are retained so their wake up latency can be compared.
        */
        ESP_LOGI(TAG, "[notif] patient call button press #%lu  latency=%lld us (max=%llu) "
                      "(notif count=%lu)",
                 (unsigned long)presses_observed,
                 (long long)lat,
                 (unsigned long long)latency_max_notif_us,
                 (unsigned long)count);
    }
}

#if WITH_LOAD
/* ============================================================
 *  BACKGROUND LOAD  (WITH_LOAD = 1)
 * ============================================================
 *
 * Four tasks from App 2 are used to create controlled contention on Core 1.
 * Each task uses observable output data to prevent compiler optimization and
 * records its maximum measured execution time.
 * 
 * Periods and Priorities:
 * Task A:  10 ms, priority 15
 * Task B:  20 ms, priority 10
 * Task C:  50 ms, priority  5
 * Task D: 100 ms, priority  2
 */
static volatile uint32_t hb_a, hb_b, hb_c, hb_d;
static uint64_t wcet_a_max_us, wcet_b_max_us, wcet_c_max_us, wcet_d_max_us;

// Measure a code block and retain its largest observed execution time.
#define MEASURE_WCET(_max_var, _body) do {                       \
    int64_t _t0 = esp_timer_get_time();                          \
    _body;                                                        \
    int64_t _dt = esp_timer_get_time() - _t0;                    \
    if ((uint64_t)_dt > (_max_var)) (_max_var) = (uint64_t)_dt;  \
} while (0)

/* ---- Task A: xorshift32 churn (integer) ---- */
#define A_ITERS 100
static volatile uint32_t a_sink;
static void load_task_a(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(LOAD_A_PERIOD_MS);
    for (;;) {
        MEASURE_WCET(wcet_a_max_us, {
            uint32_t x = a_sink ? a_sink : 0xACE1u;   /* seed from sink (observable) */
            for (int i = 0; i < A_ITERS; i++) {
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            }
            a_sink = x;
        });
        hb_a++;
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Task B: single-precision FIR ---- */
#define B_SAMP 64                       /* power of two for the index mask */
#define B_TAPS 32                       /* <= B_SAMP */
static float b_buf[B_SAMP];
static float b_coef[B_TAPS];
static volatile float b_sink;
static void load_task_b(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(LOAD_B_PERIOD_MS);
    for (;;) {
        MEASURE_WCET(wcet_b_max_us, {
            float acc = b_sink;          /* seed from sink (observable) */
            for (int n = 0; n < B_SAMP; n++)
                for (int k = 0; k < B_TAPS; k++)
                    acc += b_buf[(n + B_SAMP - k) & (B_SAMP - 1)] * b_coef[k];
            b_sink = acc;
        });
        hb_b++;
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Task C: CRC-32 over a buffer ---- */
#define C_LEN 256
static uint8_t c_pkt[C_LEN];
static volatile uint32_t c_sink;
static void load_task_c(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(LOAD_C_PERIOD_MS);
    for (;;) {
        MEASURE_WCET(wcet_c_max_us, {
            uint32_t crc = 0xFFFFFFFFu ^ c_sink;     /* seed from sink */
            for (int n = 0; n < C_LEN; n++) {
                crc ^= c_pkt[n];
                for (int b = 0; b < 8; b++)
                    crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
            }
            c_sink = crc ^ 0xFFFFFFFFu;
        });
        hb_c++;
        vTaskDelayUntil(&last, period);
    }
}

/* ---- Task D: insertion sort, forced worst case ---- */
#define D_N 100
static int d_arr[D_N];
static volatile int d_sink;
static void load_task_d(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(LOAD_D_PERIOD_MS);
    for (;;) {
        MEASURE_WCET(wcet_d_max_us, {
            for (int i = 0; i < D_N; i++) d_arr[i] = D_N - i + (d_sink & 1);
            for (int i = 1; i < D_N; i++) {          /* insertion sort */
                int key = d_arr[i];                  /* split decls: a top-level */
                int j = i - 1;                       /* comma would break the macro arg */
                while (j >= 0 && d_arr[j] > key) { d_arr[j+1] = d_arr[j]; j--; }
                d_arr[j+1] = key;
            }
            d_sink = d_arr[D_N/2];
        });
        hb_d++;
        vTaskDelayUntil(&last, period);
    }
}

static void task_monitor(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000);

    for (;;) {
        printf("\n=== Medical Alert System: Background Task Monitor ===\n");
        printf("%-5s %-8s %-9s %-12s %-10s\n",
               "Task", "Period", "Priority", "Heartbeats", "WCET(us)");
        printf("%-5s %-8s %-9d %-12lu %-10llu\n",
               "A", "10 ms",  LOAD_A_PRIORITY, (unsigned long)hb_a, (unsigned long long)wcet_a_max_us);
        printf("%-5s %-8s %-9d %-12lu %-10llu\n",
               "B", "20 ms",  LOAD_B_PRIORITY, (unsigned long)hb_b, (unsigned long long)wcet_b_max_us);
        printf("%-5s %-8s %-9d %-12lu %-10llu\n",
               "C", "50 ms",   LOAD_C_PRIORITY, (unsigned long)hb_c, (unsigned long long)wcet_c_max_us);
        printf("%-5s %-8s %-9d %-12lu %-10llu\n",
               "D", "100 ms",  LOAD_D_PRIORITY, (unsigned long)hb_d, (unsigned long long)wcet_d_max_us);
        printf("(heartbeats should grow monotonically; a stalled counter = "
               "starved or hung task)\n\n");
        
        printf("Semaphore max latency: %4llu us\n", (unsigned long long)latency_max_sem_us);
        printf("Notification max latency: %llu us\n", (unsigned long long)latency_max_notif_us);

        vTaskDelayUntil(&last, period);
    }
}

/* Fill the load buffers exactly once, off the periodic path. */
static void load_init_buffers(void)
{
    for (int i = 0; i < B_SAMP; i++) b_buf[i]  = (float)((i * 2654435761u) & 0xFFFF) / 65536.0f;
    for (int k = 0; k < B_TAPS; k++) b_coef[k] = 1.0f / (float)B_TAPS;   /* boxcar */
    for (int n = 0; n < C_LEN;  n++) c_pkt[n]  = (uint8_t)(n * 31 + 7);
}

static void start_background_load(void)
{
    load_init_buffers();
    /* Rate-monotonic ladder, all on Core 1, mirroring App 2. These priorities
     * are FIXED here (this is a load fixture). Note A=15 outranks your
     * bottom-half tasks (12); B/C/D do not. */
    xTaskCreatePinnedToCore(load_task_a, "load_a", LOAD_A_STACK_SIZE, NULL, LOAD_A_PRIORITY, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(load_task_b, "load_b", LOAD_B_STACK_SIZE, NULL, LOAD_B_PRIORITY, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(load_task_c, "load_c", LOAD_C_STACK_SIZE, NULL, LOAD_C_PRIORITY, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(load_task_d, "load_d", LOAD_D_STACK_SIZE, NULL, LOAD_D_PRIORITY, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(task_monitor, "task_monitor", MONITOR_TASK_STACK_SIZE, NULL, MONITOR_TASK_PRIORITY, NULL, PRO_CPU_NUM);
}
#endif /* WITH_LOAD */

/* ============================================================
 *  app_main — wire everything up
 * ============================================================ */
void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "==== [Medical Alert] starting — ISR + bottom-half ====");

#if WITH_LOAD
    ESP_LOGI(TAG, "Run mode: UNDER LOAD (WITH_LOAD=1) — App 2's 4 tasks on Core 1");
#else
    ESP_LOGI(TAG, "Run mode: IDLE (WITH_LOAD=0) — baseline latency, no background tasks");
#endif

#if INJECT_MISSING_ISR_YIELD
    ESP_LOGW(TAG, "Fault injection enabled: ISR exit yield removed");
#else
    ESP_LOGI(TAG, "Fault injection Disabled: ISR exit yield present");
#endif

    /* Create signaling primitives. */
    alert_sem = xSemaphoreCreateBinary();

    /* Bottom-half tasks. Both pinned to Core 1, both high priority because
     * they're the "real-time response" path. */
    xTaskCreatePinnedToCore(btn_task_sem,  "alert_sem",   ALERT_TASK_STACK_SIZE, NULL, ALERT_TASK_PRIORITY, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(btn_task_notif,"btn_notif", ALERT_TASK_STACK_SIZE, NULL, ALERT_TASK_PRIORITY,
                            &alert_task_notif_handle, APP_CPU_NUM);

#if WITH_LOAD
    /* Bring App 2's periodic tasks online as a Core-1 load fixture. */
    start_background_load();
#endif

    /* Configure GPIOs. */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,    /* button pulls low when pressed */
    };
    gpio_config(&btn_cfg);

    gpio_config_t pulse_cfg = {
        .pin_bit_mask = 1ULL << ISR_PULSE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pulse_cfg);
    gpio_set_level(ISR_PULSE_GPIO, 0);

    /* Install GPIO ISR service. Flags = 0 means default (low) priority. */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL);

    ESP_LOGI(TAG, "Press the button on GPIO %d. Scope GPIO %d to time the ISR.",
             BUTTON_GPIO, ISR_PULSE_GPIO);

    /* app_main returns; tasks continue. */
}
