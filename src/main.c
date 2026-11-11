#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "uart.h"

/* Structure for Vibration Data (Section 4.2.3) */
typedef struct {
    uint32_t sample_id;
    TickType_t timestamp;
    float accel_x;
    float accel_y;
    float accel_z;
    float rms_val;
} VibrationSample_t;

/* IPC Handles */
static QueueHandle_t xVibrationQueue = NULL;
static SemaphoreHandle_t xAlarmSemaphore = NULL;
static SemaphoreHandle_t xUartMutex = NULL;

/* Task Handles */
static TaskHandle_t xTaskHandle_Sensor  = NULL;
static TaskHandle_t xTaskHandle_Process = NULL;
static TaskHandle_t xTaskHandle_Alarm   = NULL;
static TaskHandle_t xTaskHandle_Logger  = NULL;

/* Global Experiment Statistics */
volatile uint32_t g_samples_generated = 0;
volatile uint32_t g_samples_processed = 0;
volatile uint32_t g_dropped_packets   = 0;
volatile uint32_t g_alarm_count       = 0;
volatile uint32_t g_last_alarm_latency = 0;

/* Experiment Scenario Control */
volatile uint32_t g_current_scenario = 1; /* 1, 2, or 3 */
volatile bool g_experiment_finished = false;

/* Heap tracking */
size_t g_heap_initial_free = 0;
size_t g_heap_after_tasks_free = 0;

/* Runtime stats counter */
volatile uint32_t ulHighFrequencyTimerTicks = 0;

/* Mutex-protected UART print helper */
void safe_uart_puts(const char *str)
{
    if (xUartMutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTake(xUartMutex, portMAX_DELAY);
    }
    uart_puts(str);
    if (xUartMutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGive(xUartMutex);
    }
}

/* Fast FIR/Moving Average DSP Filter Emulation */
static float apply_dsp_filter(float val, int iterations)
{
    float acc = val;
    for (volatile int i = 0; i < iterations; i++) {
        acc = (acc * 0.95f) + (val * 0.05f);
    }
    return acc;
}

/* Forward declarations */
static void Task_SensorSim(void *pvParameters);
static void Task_Process(void *pvParameters);
static void Task_Alarm(void *pvParameters);
static void Task_Logger(void *pvParameters);
static void Print_Final_Report(void);

/* FreeRTOS Hooks */
void vApplicationMallocFailedHook(void)
{
    uart_puts("\r\n[ERROR] FreeRTOS Malloc Failed!\r\n");
    qemu_exit(1);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    uart_puts("\r\n[FATAL] Stack Overflow in Task: ");
    uart_puts(pcTaskName);
    uart_puts("\r\n");
    qemu_exit(2);
}

void vAssertCalled(const char *pcFile, uint32_t ulLine)
{
    uart_puts("\r\n[ASSERT] File: ");
    uart_puts(pcFile);
    uart_puts(" Line: ");
    uart_print_uint(ulLine);
    uart_puts("\r\n");
    qemu_exit(3);
}

/*-----------------------------------------------------------
 * TASK 1: Sensor Simulator (Priority 3, Stack: 512 words)
 *----------------------------------------------------------*/
static void Task_SensorSim(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t sample_id = 0;

    /* SCENARIO 1: Normal Operation & Anomaly Injection */
    safe_uart_puts("\r\n====================================================================\r\n");
    safe_uart_puts(">>> GIAI DOAN 1: KICH BAN 1 - HOAT DONG BINH THUONG & PHAT HIEN ANOMALY\r\n");
    safe_uart_puts("    (Sampling period: 50ms / 20Hz. Injections at Sample #10 & #16)\r\n");
    safe_uart_puts("====================================================================\r\n");

    for (int i = 1; i <= 20; i++) {
        sample_id++;
        g_samples_generated++;
        VibrationSample_t sample;
        sample.sample_id = sample_id;
        sample.timestamp = xTaskGetTickCount();

        if (sample_id == 10 || sample_id == 16) {
            /* Anomaly sample: RMS ~ 4.18g (> 2.5g) */
            sample.accel_x = 4.1f;
            sample.accel_y = 4.2f;
            sample.accel_z = 4.25f;
        } else {
            /* Normal sample: RMS ~ 1.05g */
            sample.accel_x = 0.9f + (float)(i % 3) * 0.15f;
            sample.accel_y = 1.0f + (float)(i % 2) * 0.1f;
            sample.accel_z = 1.1f - (float)(i % 4) * 0.05f;
        }
        sample.rms_val = 0.0f;

        if (xQueueSend(xVibrationQueue, &sample, 0) != pdPASS) {
            g_dropped_packets++;
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
    }

    /* Wait for Process task to finish remaining items */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* SCENARIO 2: Preemption & Mutex Verification */
    g_current_scenario = 2;
    safe_uart_puts("\r\n====================================================================\r\n");
    safe_uart_puts(">>> GIAI DOAN 2: KICH BAN 2 - KIEM CHUNG PREEMPTION VA MUTEX\r\n");
    safe_uart_puts("    (Triggering emergency alarm during computation)\r\n");
    safe_uart_puts("====================================================================\r\n");

    sample_id++;
    g_samples_generated++;
    VibrationSample_t scen2_sample;
    scen2_sample.sample_id = sample_id;
    scen2_sample.timestamp = xTaskGetTickCount();
    scen2_sample.accel_x = 5.0f; /* High vibration to trigger alarm */
    scen2_sample.accel_y = 5.0f;
    scen2_sample.accel_z = 5.0f;
    scen2_sample.rms_val = 0.0f;
    xQueueSend(xVibrationQueue, &scen2_sample, 0);

    /* Allow Process and Alarm to execute Scenario 2 */
    vTaskDelay(pdMS_TO_TICKS(300));

    /* SCENARIO 3: Stress Test (Queue Saturation & Drop Rate) */
    g_current_scenario = 3;
    safe_uart_puts("\r\n====================================================================\r\n");
    safe_uart_puts(">>> GIAI DOAN 3: KICH BAN 3 - STRESS TEST & RESOURCE SATURATION\r\n");
    safe_uart_puts("    (High rate 2ms / 500Hz vs Slow Processing 8ms. Queue capacity: 10)\r\n");
    safe_uart_puts("====================================================================\r\n");

    xLastWakeTime = xTaskGetTickCount();
    for (int i = 1; i <= 80; i++) {
        sample_id++;
        g_samples_generated++;
        VibrationSample_t sample;
        sample.sample_id = sample_id;
        sample.timestamp = xTaskGetTickCount();

        if (i == 5) {
            /* High peak vibration sample injected into stress burst */
            sample.accel_x = 4.8f;
            sample.accel_y = 4.8f;
            sample.accel_z = 4.8f;
        } else {
            sample.accel_x = 1.0f;
            sample.accel_y = 1.1f;
            sample.accel_z = 0.9f;
        }
        sample.rms_val = 0.0f;

        if (xQueueSend(xVibrationQueue, &sample, 0) != pdPASS) {
            g_dropped_packets++;
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2));
    }

    /* Wait for Process task to finish draining remaining items in queue */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* End of all scenarios */
    g_experiment_finished = true;
    Print_Final_Report();

    /* Self-terminating QEMU via semihosting exit */
    uart_puts("\r\n[QEMU] All 3 Scenarios completed successfully. Exiting simulation...\r\n");
    qemu_exit(0);

    /* Suspended if QEMU semihosting does not terminate */
    vTaskSuspend(NULL);
}

/*-----------------------------------------------------------
 * TASK 2: Vibration Processing Task (Priority 2, Stack: 1024 words)
 *----------------------------------------------------------*/
static void Task_Process(void *pvParameters)
{
    (void)pvParameters;
    VibrationSample_t sample;
    const float THRESHOLD = 2.5f;

    while (1) {
        if (xQueueReceive(xVibrationQueue, &sample, portMAX_DELAY) == pdPASS) {
            g_samples_processed++;

            /* FPU calculation: RMS = sqrt((ax^2 + ay^2 + az^2) / 3) */
            float sum_sq = (sample.accel_x * sample.accel_x) +
                           (sample.accel_y * sample.accel_y) +
                           (sample.accel_z * sample.accel_z);
            float rms = sqrtf(sum_sq / 3.0f);
            sample.rms_val = rms;

            /* DSP filtering & computation delay emulation */
            if (g_current_scenario == 3) {
                /* Simulate heavy 8ms DSP processing load per sample to stress queue */
                vTaskDelay(pdMS_TO_TICKS(8));
                sample.rms_val = apply_dsp_filter(rms, 1000);
            } else {
                sample.rms_val = apply_dsp_filter(rms, 200);
            }

            /* Scenario-specific logging */
            if (g_current_scenario == 1) {
                if (rms > THRESHOLD) {
                    /* Log anomaly detection */
                    if (xUartMutex != NULL) xSemaphoreTake(xUartMutex, portMAX_DELAY);
                    uart_puts("[Process] Sample #");
                    uart_print_uint(sample.sample_id);
                    uart_puts(" ANOMALY DETECTED! RMS = ");
                    uart_print_float(rms, 2);
                    uart_puts("g > 2.50g -> Triggering Alarm!\r\n");
                    if (xUartMutex != NULL) xSemaphoreGive(xUartMutex);

                    /* Signal Alarm Task */
                    xSemaphoreGive(xAlarmSemaphore);
                } else {
                    if (sample.sample_id % 4 == 1) {
                        if (xUartMutex != NULL) xSemaphoreTake(xUartMutex, portMAX_DELAY);
                        uart_puts("[Process] Sample #");
                        uart_print_uint(sample.sample_id);
                        uart_puts(" Normal: RMS = ");
                        uart_print_float(rms, 2);
                        uart_puts("g\r\n");
                        if (xUartMutex != NULL) xSemaphoreGive(xUartMutex);
                    }
                }
            } else if (g_current_scenario == 2) {
                /* Preemption verification */
                TickType_t t0 = xTaskGetTickCount();

                if (xUartMutex != NULL) xSemaphoreTake(xUartMutex, portMAX_DELAY);
                uart_puts("[Process: Triggering Alarm at Tick T0 = ");
                uart_print_uint(t0);
                uart_puts("]\r\n");
                if (xUartMutex != NULL) xSemaphoreGive(xUartMutex);

                /* Give semaphore to wake Task_Alarm (Priority 4).
                   Because Priority 4 > Priority 2, context switch happens immediately! */
                xSemaphoreGive(xAlarmSemaphore);

                /* Execution resumes here ONLY AFTER Task_Alarm yields/blocks */
                if (xUartMutex != NULL) xSemaphoreTake(xUartMutex, portMAX_DELAY);
                uart_puts("[Process: Resumed execution after Alarm finished at Tick = ");
                uart_print_uint(xTaskGetTickCount());
                uart_puts("]\r\n");
                if (xUartMutex != NULL) xSemaphoreGive(xUartMutex);
            } else if (g_current_scenario == 3) {
                if (rms > THRESHOLD) {
                    if (xUartMutex != NULL) xSemaphoreTake(xUartMutex, portMAX_DELAY);
                    uart_puts("[Process-Stress] High RMS = ");
                    uart_print_float(rms, 2);
                    uart_puts("g detected -> Signaling Alarm!\r\n");
                    if (xUartMutex != NULL) xSemaphoreGive(xUartMutex);

                    xSemaphoreGive(xAlarmSemaphore);
                }
            }
        }
    }
}

/*-----------------------------------------------------------
 * TASK 3: Emergency Alarm Task (Priority 4, Stack: 512 words)
 *----------------------------------------------------------*/
static void Task_Alarm(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        /* Always blocked until emergency event */
        if (xSemaphoreTake(xAlarmSemaphore, portMAX_DELAY) == pdPASS) {
            TickType_t t_now = xTaskGetTickCount();
            g_alarm_count++;

            if (g_current_scenario == 2) {
                /* Exact required log for Scenario 2 preemption verification */
                if (xUartMutex != NULL) xSemaphoreTake(xUartMutex, portMAX_DELAY);
                uart_puts("[Alarm: Preempted CPU immediately at Tick T0 = ");
                uart_print_uint(t_now);
                uart_puts("! Priority = 4]\r\n");
                if (xUartMutex != NULL) xSemaphoreGive(xUartMutex);
            } else {
                /* Scenario 1 & 3 emergency alarm */
                if (xUartMutex != NULL) xSemaphoreTake(xUartMutex, portMAX_DELAY);
                uart_puts("\r\n>>> [EMERGENCY ALARM #");
                uart_print_uint(g_alarm_count);
                uart_puts("] Activated at Tick = ");
                uart_print_uint(t_now);
                uart_puts(" (Emergency Shutdown Signal Generated)\r\n\r\n");
                if (xUartMutex != NULL) xSemaphoreGive(xUartMutex);
            }
        }
    }
}

/*-----------------------------------------------------------
 * TASK 4: System Logger Task (Priority 1, Stack: 1024 words)
 *----------------------------------------------------------*/
static void Task_Logger(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (!g_experiment_finished) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200));

        if (xUartMutex != NULL) xSemaphoreTake(xUartMutex, portMAX_DELAY);
        uart_puts("[Logger Tick: ");
        uart_print_uint(xTaskGetTickCount());
        uart_puts(" ms] FreeHeap: ");
        uart_print_uint((uint32_t)xPortGetFreeHeapSize());
        uart_puts(" B | Gen: ");
        uart_print_uint(g_samples_generated);
        uart_puts(" | Proc: ");
        uart_print_uint(g_samples_processed);
        uart_puts(" | Drop: ");
        uart_print_uint(g_dropped_packets);
        uart_puts(" | Alarms: ");
        uart_print_uint(g_alarm_count);
        uart_puts("\r\n");
        if (xUartMutex != NULL) xSemaphoreGive(xUartMutex);
    }

    vTaskSuspend(NULL);
}

/*-----------------------------------------------------------
 * Print Final Metrology and Benchmark Report
 *----------------------------------------------------------*/
static void Print_Final_Report(void)
{
    uart_puts("\r\n====================================================================\r\n");
    uart_puts("       TONG KET SO LIEU THUC NGHIEM TOAN DIEN (SECTIONS 4.3 & 4.4)  \r\n");
    uart_puts("====================================================================\r\n");

    /* 1. Packet & Alarm Statistics */
    uart_puts("[1] THONG KE DULIEU VA CANH BAO:\r\n");
    uart_puts("    - Tong so mau sinh ra (Generated)   : ");
    uart_print_uint(g_samples_generated);
    uart_puts("\r\n");
    uart_puts("    - Tong so mau da xu ly (Processed)  : ");
    uart_print_uint(g_samples_processed);
    uart_puts("\r\n");
    uart_puts("    - So goi bi rot do tran Queue (Drop): ");
    uart_print_uint(g_dropped_packets);
    uart_puts(" (errQUEUE_FULL)\r\n");
    uart_puts("    - Tong so lan canh bao (Alarms)     : ");
    uart_print_uint(g_alarm_count);
    uart_puts("\r\n\r\n");

    /* 2. FreeRTOS Heap Statistics */
    size_t current_free = xPortGetFreeHeapSize();
    size_t total_heap = configTOTAL_HEAP_SIZE;
    uart_puts("[2] THONG KE FREERTOS HEAP (heap_4.c):\r\n");
    uart_puts("    - Tong dung luong Heap cau hinh     : ");
    uart_print_uint((uint32_t)total_heap);
    uart_puts(" Bytes (64 KB)\r\n");
    uart_puts("    - Heap con trong truoc khi tao Task : ");
    uart_print_uint((uint32_t)g_heap_initial_free);
    uart_puts(" Bytes\r\n");
    uart_puts("    - Heap con trong sau khi tao 4 Task : ");
    uart_print_uint((uint32_t)g_heap_after_tasks_free);
    uart_puts(" Bytes\r\n");
    uart_puts("    - Heap con trong hien tai (Free)    : ");
    uart_print_uint((uint32_t)current_free);
    uart_puts(" Bytes\r\n");
    uart_puts("    - Heap da cap phat thuc te (Used)   : ");
    uart_print_uint((uint32_t)(total_heap - current_free));
    uart_puts(" Bytes\r\n\r\n");

    /* 3. Stack High Water Mark for all 4 Tasks */
    UBaseType_t hwm_sensor  = uxTaskGetStackHighWaterMark(xTaskHandle_Sensor);
    UBaseType_t hwm_process = uxTaskGetStackHighWaterMark(xTaskHandle_Process);
    UBaseType_t hwm_alarm   = uxTaskGetStackHighWaterMark(xTaskHandle_Alarm);
    UBaseType_t hwm_logger  = uxTaskGetStackHighWaterMark(xTaskHandle_Logger);

    uart_puts("[3] BANG THEO DOI STACK HIGH WATER MARK (MUC 4.4.1):\r\n");
    uart_puts("--------------------------------------------------------------------\r\n");
    uart_puts("Task Name        | Prio | Alloc (Bytes) | Free HWM (Bytes) | Used (Bytes)\r\n");
    uart_puts("--------------------------------------------------------------------\r\n");

    /* SensorSim */
    uart_puts("Task_SensorSim   |  3   |  2048 (512w)  |  ");
    uart_print_uint((uint32_t)(hwm_sensor * sizeof(StackType_t)));
    uart_puts(" Bytes       |  ");
    uart_print_uint((uint32_t)((512 - hwm_sensor) * sizeof(StackType_t)));
    uart_puts(" Bytes\r\n");

    /* Process */
    uart_puts("Task_Process     |  2   |  4096 (1024w) |  ");
    uart_print_uint((uint32_t)(hwm_process * sizeof(StackType_t)));
    uart_puts(" Bytes       |  ");
    uart_print_uint((uint32_t)((1024 - hwm_process) * sizeof(StackType_t)));
    uart_puts(" Bytes\r\n");

    /* Alarm */
    uart_puts("Task_Alarm       |  4   |  2048 (512w)  |  ");
    uart_print_uint((uint32_t)(hwm_alarm * sizeof(StackType_t)));
    uart_puts(" Bytes       |  ");
    uart_print_uint((uint32_t)((512 - hwm_alarm) * sizeof(StackType_t)));
    uart_puts(" Bytes\r\n");

    /* Logger */
    uart_puts("Task_Logger      |  1   |  4096 (1024w) |  ");
    uart_print_uint((uint32_t)(hwm_logger * sizeof(StackType_t)));
    uart_puts(" Bytes       |  ");
    uart_print_uint((uint32_t)((1024 - hwm_logger) * sizeof(StackType_t)));
    uart_puts(" Bytes\r\n");
    uart_puts("--------------------------------------------------------------------\r\n");
    uart_puts("Ghi chu: Free HWM la so byte stack toi thieu con du chua tung bi dung toi.\r\n");
    uart_puts("====================================================================\r\n");
}

/*-----------------------------------------------------------
 * Application Entry Point
 *----------------------------------------------------------*/
int main(void)
{
    /* Initialize UART */
    uart_init();
    uart_puts("\r\n====================================================================\r\n");
    uart_puts("   HE THONG GIAM SAT RUNG DONG & CANH BAO BAT THUONG (FreeRTOS)    \r\n");
    uart_puts("   Architecture: ARM Cortex-M55 (ARMv8.1-M Mainline + FPU + MVE)   \r\n");
    uart_puts("   Emulated on : QEMU mps3-an547 (AN547 FPGA image)                 \r\n");
    uart_puts("====================================================================\r\n");

    /* Initialize Heap tracking */
    g_heap_initial_free = configTOTAL_HEAP_SIZE;
    uart_puts("[Kernel] Initial Free Heap: ");
    uart_print_uint((uint32_t)g_heap_initial_free);
    uart_puts(" Bytes (64 KB)\r\n");

    /* Create Queue (capacity: 10 items) */
    xVibrationQueue = xQueueCreate(10, sizeof(VibrationSample_t));
    if (xVibrationQueue == NULL) {
        uart_puts("[FATAL] Failed to create xVibrationQueue!\r\n");
        qemu_exit(1);
    }

    /* Create Binary Semaphore for Alarm */
    xAlarmSemaphore = xSemaphoreCreateBinary();
    if (xAlarmSemaphore == NULL) {
        uart_puts("[FATAL] Failed to create xAlarmSemaphore!\r\n");
        qemu_exit(1);
    }

    /* Create Mutex for UART protection (Priority Inheritance enabled) */
    xUartMutex = xSemaphoreCreateMutex();
    if (xUartMutex == NULL) {
        uart_puts("[FATAL] Failed to create xUartMutex!\r\n");
        qemu_exit(1);
    }

    /* Create 4 Tasks (Priorities: Alarm 4, Sensor 3, Process 2, Logger 1) */
    BaseType_t ret;

    ret = xTaskCreate(Task_Alarm, "Task_Alarm", 512, NULL, 4, &xTaskHandle_Alarm);
    configASSERT(ret == pdPASS);

    ret = xTaskCreate(Task_SensorSim, "Task_SensorSim", 512, NULL, 3, &xTaskHandle_Sensor);
    configASSERT(ret == pdPASS);

    ret = xTaskCreate(Task_Process, "Task_Process", 1024, NULL, 2, &xTaskHandle_Process);
    configASSERT(ret == pdPASS);

    ret = xTaskCreate(Task_Logger, "Task_Logger", 1024, NULL, 1, &xTaskHandle_Logger);
    configASSERT(ret == pdPASS);

    g_heap_after_tasks_free = xPortGetFreeHeapSize();
    uart_puts("[Kernel] Free Heap after creating 4 Tasks & IPCs: ");
    uart_print_uint((uint32_t)g_heap_after_tasks_free);
    uart_puts(" Bytes\r\n");
    uart_puts("[Kernel] Starting FreeRTOS Scheduler...\r\n");

    /* Start FreeRTOS Scheduler */
    vTaskStartScheduler();

    /* Should never reach here */
    for (;;);
    return 0;
}
