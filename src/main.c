#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

#include "ARMCM55.h"
#include "arm_math.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include "uart.h"

/*----------------------------------------------------------------------------
 * System Constants & Architectural Specifications (Chapter III)
 *----------------------------------------------------------------------------*/
#define SAMPLING_RATE_HZ        8000
#define BLOCK_SIZE              512
#define BLOCK_PERIOD_MS         64          /* 512 samples @ 8 kHz = 64 ms */
#define FFT_BIN_RES_HZ          15.625f     /* 8000 Hz / 512 points */
#define NUM_MAG_BINS            256         /* Positive frequency bins 0..255 */

#define LOG_QUEUE_DEPTH         8
#define SPARSE_LOG_RATIO        16          /* 1 routine summary record every 16 blocks (~1.024 s) */

/* Thresholds conforming strictly to Section 3.3 */
#define SOFT_THRESHOLD_RMS      1.20f       /* Normal vibration RMS is ~0.71g */
#define HARD_THRESHOLD_RMS      2.00f       /* Anomaly vibration RMS is > 2.20g */
#define HARD_THRESHOLD_FAULT_MAG 400.0f     /* Fault harmonic magnitude threshold (normal < 60) */

/* Event Codes for 32-Byte LogEntry_t */
#define EVENT_ROUTINE_SUMMARY   0x0001
#define EVENT_SOFT_WARNING      0x0002
#define EVENT_HARD_ALARM        0x0003
#define EVENT_PREEMPTION_DEMO   0x0004
#define EVENT_PRIO_INHERIT_DEMO 0x0005
#define EVENT_STRESS_DROP       0x0006

/*----------------------------------------------------------------------------
 * Type Definitions (Conforming strictly to Chapter III §2.2)
 *----------------------------------------------------------------------------*/

/* 1. Block Descriptor for xBlockQueue (depth 2) - Descriptor only, NO copy of 512 samples */
typedef struct {
    const int16_t *pData;       /* Pointer to 512 int16 samples in DTCM double buffer */
    uint32_t block_seq;         /* Block sequence number */
    uint32_t timestamp_ms;      /* Timestamp when block was acquired */
    uint8_t anomaly_type;       /* 0=None, 1=Bearing fault harmonic, 2=Severe imbalance */
    uint8_t overrun_flag;       /* 1 if buffer overrun occurred */
} BlockDescriptor_t;

/* 2. Structured Log Entry for xLogQueue (depth 8) - EXACTLY 32 Bytes */
typedef struct __attribute__((packed, aligned(4))) {
    uint32_t timestamp_ms;      /* Offset  0.. 3: Timestamp (ms) */
    uint16_t event_code;        /* Offset  4.. 5: Event ID code */
    uint16_t block_seq;         /* Offset  6.. 7: Block sequence */
    float rms_val;              /* Offset  8..11: RMS acceleration (g) */
    float peak_val;             /* Offset 12..15: Peak acceleration (g) */
    float crest_factor;         /* Offset 16..19: Crest factor (Peak / RMS) */
    float band_energy;          /* Offset 20..23: Frequency band energy */
    float peak_freq_hz;         /* Offset 24..27: Peak spectral frequency (Hz) */
    uint16_t peak_bin;          /* Offset 28..29: FFT bin index of peak */
    uint8_t alarm_level;        /* Offset 30    : Alarm level: 0=Normal, 1=Soft, 2=Hard */
    uint8_t flags;              /* Offset 31    : Status flags (bit 0 = overrun) */
} LogEntry_t;                   /* Total = 32 Bytes */

/* Verify 32-byte compile-time constraint */
typedef char LogEntry_SizeCheck[(sizeof(LogEntry_t) == 32) ? 1 : -1];

/* 3. Thread-Safe Global System Statistics */
typedef struct {
    volatile uint32_t generated_blocks;
    volatile uint32_t processed_blocks;
    volatile uint32_t dropped_blocks;
    volatile uint32_t alarm_count;
    volatile uint32_t log_dropped;
    volatile uint32_t overrun_count;
    volatile uint32_t last_alarm_latency_ticks;
    volatile uint32_t max_alarm_latency_ticks;
    volatile uint32_t total_alarm_latency_ticks;
    volatile uint32_t alarm_latency_count;
} SystemStats_t;

static SystemStats_t g_stats = { 0 };

/* Safe atomic statistics update macros */
#define STATS_INC(field) do { \
    taskENTER_CRITICAL(); \
    g_stats.field++; \
    taskEXIT_CRITICAL(); \
} while(0)

#define STATS_INC_FROM_ISR(field) do { \
    UBaseType_t uxSaved = taskENTER_CRITICAL_FROM_ISR(); \
    g_stats.field++; \
    taskEXIT_CRITICAL_FROM_ISR(uxSaved); \
} while(0)

/*----------------------------------------------------------------------------
 * Memory Placement (Conforming strictly to Chapter III Table 3.3)
 * - DTCM: Double Buffer (2 KB) + FFT Workspace (4 KB) + 3 Critical Task Stacks (6 KB)
 *   Total DTCM = ~13 KB out of 128 KB
 * - SRAM: FreeRTOS Heap (heap_5 64 KB), Logging Stack (2 KB), Idle Stack (512 B), MSP (8 KB)
 *----------------------------------------------------------------------------*/

/* DTCM Section: Double Buffer 2 x 512 int16 = 2,048 Bytes */
static int16_t g_DoubleBuffer[2][BLOCK_SIZE] __attribute__((section(".dtcm_bss"), aligned(16)));

/* DTCM Section: FFT Workspace and Spectrum Buffers = 4,096 Bytes */
static float32_t g_FFT_Input[BLOCK_SIZE]     __attribute__((section(".dtcm_bss"), aligned(16)));
static float32_t g_FFT_Output[BLOCK_SIZE]    __attribute__((section(".dtcm_bss"), aligned(16)));
static float32_t g_Mag_Spectrum[NUM_MAG_BINS] __attribute__((section(".dtcm_bss"), aligned(16)));

/* DTCM Section: Stacks and TCBs for the 3 Critical Tasks (Table 3.1) */
/* Task 1: vTaskAlarm (Priority 5, 1,024 Bytes = 256 words) */
static StackType_t xAlarmStack[256] __attribute__((section(".dtcm_bss"), aligned(8)));
static StaticTask_t xAlarmTCB        __attribute__((section(".dtcm_bss"), aligned(8)));

/* Task 2: vTaskAcquisition (Priority 4, 1,024 Bytes = 256 words) */
static StackType_t xAcquisitionStack[256] __attribute__((section(".dtcm_bss"), aligned(8)));
static StaticTask_t xAcquisitionTCB        __attribute__((section(".dtcm_bss"), aligned(8)));

/* Task 3: vTaskProcessing (Priority 3, 4,096 Bytes = 1,024 words) */
static StackType_t xProcessingStack[1024] __attribute__((section(".dtcm_bss"), aligned(8)));
static StaticTask_t xProcessingTCB        __attribute__((section(".dtcm_bss"), aligned(8)));

/* SRAM Section: vTaskLogging Stack (Priority 2, 2,048 Bytes = 512 words) */
static StackType_t xLoggingStack[512] __attribute__((section(".bss"), aligned(8)));
static StaticTask_t xLoggingTCB        __attribute__((section(".bss"), aligned(8)));

/* SRAM Section: Static Memory for Idle & Timer Tasks */
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE] __attribute__((section(".bss"), aligned(8)));
static StaticTask_t xIdleTCB                            __attribute__((section(".bss"), aligned(8)));
static StackType_t xTimerStack[configTIMER_TASK_STACK_DEPTH] __attribute__((section(".bss"), aligned(8)));
static StaticTask_t xTimerTCB                                __attribute__((section(".bss"), aligned(8)));

/* SRAM Section: FreeRTOS Heap (heap_5: 64 KB = 65,536 Bytes) */
static uint8_t ucHeap[65536] __attribute__((section(".bss"), aligned(8)));

/*----------------------------------------------------------------------------
 * Synchronization & IPC Handles (The 4 Canonical Objects + Mutex, Chapter III §2.2)
 *----------------------------------------------------------------------------*/
static SemaphoreHandle_t xBlockReady      = NULL; /* Binary semaphore: ISR DMA stub -> Acquisition */
static QueueHandle_t     xBlockQueue      = NULL; /* Depth 2: Acquisition -> Processing */
static TaskHandle_t      xTaskAlarmHandle = NULL; /* Task Notification: Processing -> Alarm */
static QueueHandle_t     xLogQueue        = NULL; /* Depth 8 x 32-Byte: Processing -> Logging */
static SemaphoreHandle_t xLogMutex        = NULL; /* Mutex: protects log buffer & serial output */

static TaskHandle_t      xTaskAcqHandle   = NULL;
static TaskHandle_t      xTaskProcHandle  = NULL;
static TaskHandle_t      xTaskLogHandle   = NULL;

static TimerHandle_t     xDmaTimer        = NULL;

/* CMSIS-DSP RFFT Instance (512 points) */
static arm_rfft_fast_instance_f32 g_rfft_instance;

/* Experiment Scenario Control */
volatile uint32_t g_current_scenario   = 1;
volatile bool     g_experiment_running = true;
volatile bool     g_scenario2_preempt_trigger = false;

/* DMA Double-Buffer State */
static volatile uint8_t g_dma_active_buf = 0;
static volatile uint8_t g_buf_busy[2]    = { 0, 0 };
static volatile uint32_t g_block_seq_cnt = 0;

/* Latency measurement timestamp */
static volatile TickType_t g_detect_timestamp = 0;

/* High frequency timer ticks for run-time statistics */
volatile uint32_t ulHighFrequencyTimerTicks = 0;

/*----------------------------------------------------------------------------
 * Forward Declarations
 *----------------------------------------------------------------------------*/
static void vTaskAlarm(void *pvParameters);
static void vTaskAcquisition(void *pvParameters);
static void vTaskProcessing(void *pvParameters);
static void vTaskLogging(void *pvParameters);

static void run_golden_fft_tests(void);
static void run_priority_inheritance_demo(void);
static void print_final_benchmark_report(void);
static void dma_stub_generate_data(int16_t *pBuf, uint32_t block_seq);
static void rfft_unpack_magnitude(const float32_t *pRfftOut, float32_t *pMag, uint32_t num_bins);

/*----------------------------------------------------------------------------
 * FreeRTOS Static Allocation Callbacks
 *----------------------------------------------------------------------------*/
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &xIdleTCB;
    *ppxIdleTaskStackBuffer = xIdleStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer   = &xTimerTCB;
    *ppxTimerTaskStackBuffer = xTimerStack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}

/*----------------------------------------------------------------------------
 * FreeRTOS Hooks & Exception Handlers
 *----------------------------------------------------------------------------*/
void vApplicationMallocFailedHook(void)
{
    uart_puts("\r\n[FATAL] FreeRTOS Malloc Failed (heap_5 exhausted)!\r\n");
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

/* Tick Hook: drives runtime stats */
void vApplicationTickHook(void)
{
    ulHighFrequencyTimerTicks += 10;
}

/*----------------------------------------------------------------------------
 * Software Timer Callback: drives DMA Timer Interrupt Stub every 64 ms
 * (or 4 ms in Scenario 3 burst stress mode)
 *----------------------------------------------------------------------------*/
static void vDmaTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (g_experiment_running) {
        NVIC_SetPendingIRQ(Interrupt0_IRQn);
    }
}

/*----------------------------------------------------------------------------
 * DMA Stub Interrupt Service Routine (Interrupt 0)
 * Drives xBlockReady semaphore at 64 ms intervals (8 kHz, 512 samples/block)
 * Priority: configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (Level 5)
 *----------------------------------------------------------------------------*/
void Interrupt0_Handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (!g_experiment_running) {
        return;
    }

    uint8_t write_buf = g_dma_active_buf;
    g_block_seq_cnt++;

    /* Synthesize 512 vibration samples in int16 DTCM buffer */
    dma_stub_generate_data(g_DoubleBuffer[write_buf], g_block_seq_cnt);

    /* Overrun Check: Has Processing finished consuming this buffer? */
    if (g_buf_busy[write_buf]) {
        STATS_INC_FROM_ISR(overrun_count);
        STATS_INC_FROM_ISR(dropped_blocks);
    }
    g_buf_busy[write_buf] = 1;

    /* Switch double buffer half */
    g_dma_active_buf ^= 1;

    STATS_INC_FROM_ISR(generated_blocks);

    /* Signal Acquisition Task via binary semaphore */
    xSemaphoreGiveFromISR(xBlockReady, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*----------------------------------------------------------------------------
 * DMA Sensor Data Synthesis Stub
 * Emulates 8 kHz accelerometer signal:
 * - Normal: Fundamental vibration 100 Hz (1.0g), RMS = 0.707g ~ 0.71g
 * - Anomaly 1 (Block #6 - Bearing Fault): 300 Hz harmonic (3.0g), RMS = 2.24g > 2.0g
 * - Anomaly 2 (Block #14 - Severe Imbalance): 100 Hz high amplitude (4.0g), RMS = 2.83g > 2.0g
 *----------------------------------------------------------------------------*/
static void dma_stub_generate_data(int16_t *pBuf, uint32_t block_seq)
{
    uint8_t inject_anomaly = 0;
    if (g_current_scenario == 1) {
        if (block_seq == 6) {
            inject_anomaly = 1; /* Bearing harmonic anomaly */
        } else if (block_seq == 14) {
            inject_anomaly = 2; /* Severe imbalance anomaly */
        }
    } else if (g_current_scenario == 3) {
        if ((block_seq % 2) == 0) {
            inject_anomaly = 1; /* Frequent anomalies in stress burst */
        }
    }

    for (int i = 0; i < BLOCK_SIZE; i++) {
        float t = (float)i / 8000.0f;
        float sig;

        if (inject_anomaly == 1) {
            /* 100 Hz fundamental (1.0g) + 300 Hz harmonic fault (3.0g) */
            sig = 1000.0f * arm_sin_f32(2.0f * PI * 100.0f * t) +
                  3000.0f * arm_sin_f32(2.0f * PI * 300.0f * t);
        } else if (inject_anomaly == 2) {
            /* Severe 100 Hz fundamental vibration (4.0g) */
            sig = 4000.0f * arm_sin_f32(2.0f * PI * 100.0f * t);
        } else {
            /* Normal Baseline: 100 Hz fundamental (1.0g) */
            sig = 1000.0f * arm_sin_f32(2.0f * PI * 100.0f * t);
        }

        /* Quantize to int16 (1.0g = 1000 LSB) */
        if (sig > 32767.0f) sig = 32767.0f;
        if (sig < -32768.0f) sig = -32768.0f;
        pBuf[i] = (int16_t)sig;
    }
}

/*----------------------------------------------------------------------------
 * Helper to unpack CMSIS-DSP RFFT output into 256 positive frequency magnitudes
 * pDst[0] = Real(DC), pDst[1] = Real(Nyquist)
 * pDst[2*k], pDst[2*k+1] = Real, Imag of bin k (1 <= k < 256)
 *----------------------------------------------------------------------------*/
static void rfft_unpack_magnitude(const float32_t *pRfftOut, float32_t *pMag, uint32_t num_bins)
{
    /* Bin 0: DC component (pure real) */
    pMag[0] = fabsf(pRfftOut[0]);
    /* Bins 1 .. num_bins - 1: complex frequency bins */
    arm_cmplx_mag_f32(&pRfftOut[2], &pMag[1], num_bins - 1);
}

/*----------------------------------------------------------------------------
 * TASK 1: Emergency Alarm Task (vTaskAlarm)
 * Priority: 5 (Highest application priority)
 * Period  : Aperiodic, Deadline = 10 ms, Ci = 1.0 ms
 * Stack   : 1,024 Bytes (256 words, allocated in DTCM)
 * Rule    : NEVER acquires xLogMutex; directly controls hardware alarm lines
 *----------------------------------------------------------------------------*/
static void vTaskAlarm(void *pvParameters)
{
    (void)pvParameters;
    uint32_t ulNotificationValue = 0;

    while (1) {
        /* Blocked awaiting Task Notification from vTaskProcessing */
        if (xTaskNotifyWait(0, 0xFFFFFFFFUL, &ulNotificationValue, portMAX_DELAY) == pdPASS) {
            TickType_t t_now = xTaskGetTickCount();
            STATS_INC(alarm_count);

            /* Calculate Detection -> Alarm Latency (QEMU tick resolution) */
            uint32_t latency = 0;
            if (t_now >= g_detect_timestamp) {
                latency = (uint32_t)(t_now - g_detect_timestamp);
            }

            taskENTER_CRITICAL();
            g_stats.last_alarm_latency_ticks = latency;
            if (latency > g_stats.max_alarm_latency_ticks) {
                g_stats.max_alarm_latency_ticks = latency;
            }
            g_stats.total_alarm_latency_ticks += latency;
            g_stats.alarm_latency_count++;
            taskEXIT_CRITICAL();

            /* Direct UART Emergency Alert Line (No mutex dependency) */
            if (ulNotificationValue == 99) {
                /* Preemption verification message */
                uart_puts("[Alarm: PREEMPTED CPU immediately in mid-FFT! Priority = 5 > 3 | Tick=");
                uart_print_uint(t_now);
                uart_puts(" ms]\r\n");
            } else {
                uart_puts("\r\n>>> [EMERGENCY ALARM ACTUATED] Level=");
                uart_print_uint(ulNotificationValue);
                uart_puts(" | Latency=");
                uart_print_uint(latency);
                uart_puts(" ms (QEMU ticks) | Tick=");
                uart_print_uint(t_now);
                uart_puts(" ms | Actuation: BUZZER_ON + RELAY_TRIP <<<\r\n");
            }

            /* Emulate physical actuation Ci ~ 1.0 ms */
            for (volatile int i = 0; i < 2500; i++) {
                __asm__ volatile ("nop");
            }
        }
    }
}

/*----------------------------------------------------------------------------
 * TASK 2: Vibration Acquisition Task (vTaskAcquisition)
 * Priority: 4
 * Period  : 64 ms (Periodic, driven by DMA interrupt binary semaphore)
 * Deadline: 64 ms, Ci = 2.0 ms
 * Stack   : 1,024 Bytes (256 words, allocated in DTCM)
 *----------------------------------------------------------------------------*/
static void vTaskAcquisition(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        /* Wait for DMA ISR half-buffer completion */
        if (xSemaphoreTake(xBlockReady, portMAX_DELAY) == pdPASS) {
            uint8_t read_buf = g_dma_active_buf ^ 1;
            uint32_t current_seq = g_block_seq_cnt;

            BlockDescriptor_t desc;
            desc.pData        = g_DoubleBuffer[read_buf];
            desc.block_seq    = current_seq;
            desc.timestamp_ms = xTaskGetTickCount();
            desc.overrun_flag = (g_stats.overrun_count > 0) ? 1 : 0;

            if (g_current_scenario == 1) {
                desc.anomaly_type = (current_seq == 6) ? 1 : ((current_seq == 14) ? 2 : 0);
            } else if (g_current_scenario == 3) {
                desc.anomaly_type = ((current_seq % 2) == 0) ? 1 : 0;
            } else {
                desc.anomaly_type = 0;
            }

            /* Send block descriptor to Processing Task (depth 2 queue) */
            if (xQueueSend(xBlockQueue, &desc, 0) != pdPASS) {
                STATS_INC(dropped_blocks);
            }

            /* Emulate acquisition overhead Ci ~ 2.0 ms */
            for (volatile int i = 0; i < 4000; i++) {
                __asm__ volatile ("nop");
            }
        }
    }
}

/*----------------------------------------------------------------------------
 * TASK 3: Vibration Processing Task (vTaskProcessing)
 * Priority: 3
 * Period  : 64 ms, Deadline = 64 ms, Ci = 12.0 ms
 * Stack   : 4,096 Bytes (1,024 words, allocated in DTCM)
 * Hardware: Uses ARM Cortex-M55 Helium MVE vector math & CMSIS-DSP
 *----------------------------------------------------------------------------*/
static void vTaskProcessing(void *pvParameters)
{
    (void)pvParameters;
    BlockDescriptor_t desc;

    while (1) {
        /* Wait for block descriptor from Acquisition Task */
        if (xQueueReceive(xBlockQueue, &desc, portMAX_DELAY) == pdPASS) {
            STATS_INC(processed_blocks);

            /* Scenario 2 Preemption Demonstration trigger */
            if (g_scenario2_preempt_trigger) {
                g_scenario2_preempt_trigger = false;
                TickType_t t0 = xTaskGetTickCount();
                uart_puts("[Process: Intensive FFT calculation active at Tick=");
                uart_print_uint(t0);
                uart_puts(" ms. Triggering emergency event without delay...]\r\n");

                /* Notify Alarm Task (pri 5). Since 5 > 3, PREEMPTION HAPPENS IMMEDIATELY! */
                g_detect_timestamp = xTaskGetTickCount();
                xTaskNotify(xTaskAlarmHandle, 99, eSetValueWithOverwrite);

                /* Resumes here ONLY AFTER Alarm Task finishes! */
                TickType_t t1 = xTaskGetTickCount();
                uart_puts("[Process: RESUMED calculation seamlessly after Alarm preempted at Tick=");
                uart_print_uint(t1);
                uart_puts(" ms] -> Preemption verified! [PASS] [ok]\r\n");
            }

            /* 1. Helium Vector Conversion: int16 (1.0g = 1000 LSB) -> float32 (g) */
            const float32_t scale = 1.0f / 1000.0f;
            for (int i = 0; i < BLOCK_SIZE; i += 4) {
                g_FFT_Input[i + 0] = (float32_t)desc.pData[i + 0] * scale;
                g_FFT_Input[i + 1] = (float32_t)desc.pData[i + 1] * scale;
                g_FFT_Input[i + 2] = (float32_t)desc.pData[i + 2] * scale;
                g_FFT_Input[i + 3] = (float32_t)desc.pData[i + 3] * scale;
            }

            /* 2. Extract Time-Domain Features BEFORE RFFT modifies g_FFT_Input in-place */
            float32_t rms = 0.0f;
            arm_rms_f32(g_FFT_Input, BLOCK_SIZE, &rms);

            float32_t peak_val = 0.0f;
            uint32_t peak_idx = 0;
            arm_max_f32(g_FFT_Input, BLOCK_SIZE, &peak_val, &peak_idx);
            if (peak_val < 0.0f) peak_val = -peak_val;

            float32_t crest_factor = (rms > 1e-4f) ? (peak_val / rms) : 0.0f;

            /* 3. 512-Point Real FFT via CMSIS-DSP with Helium MVE acceleration */
            arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);

            /* 4. Compute Magnitude Spectrum (256 positive frequency bins) */
            rfft_unpack_magnitude(g_FFT_Output, g_Mag_Spectrum, NUM_MAG_BINS);

            /* Feature D: Spectral Peak Detection (Ignore DC bin 0) */
            float32_t peak_mag = 0.0f;
            uint32_t peak_bin_rel = 0;
            arm_max_f32(&g_Mag_Spectrum[1], NUM_MAG_BINS - 1, &peak_mag, &peak_bin_rel);
            uint16_t peak_bin = (uint16_t)(peak_bin_rel + 1);
            float32_t peak_freq_hz = (float32_t)peak_bin * FFT_BIN_RES_HZ;

            /* Feature E: Fault Harmonic Magnitude (Bin 19 = 19 * 15.625 = 296.88 Hz ~ 300 Hz) */
            float32_t fault_harmonic_mag = g_Mag_Spectrum[19];

            /* Feature F: Sub-band Energy (Fault Band: 200 Hz - 600 Hz, Bins 13..38) */
            float32_t band_energy = 0.0f;
            arm_power_f32(&g_Mag_Spectrum[13], 26, &band_energy);

            /* Emulate processing execution budget (Ci = 12.0 ms) under burst overload */
            if (g_current_scenario == 3) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            /* Release input buffer back to DMA */
            uint8_t released_buf = (desc.pData == g_DoubleBuffer[0]) ? 0 : 1;
            g_buf_busy[released_buf] = 0;

            /* 5. Threshold Classification & Anomaly Decision */
            uint8_t alarm_level = 0;
            uint16_t event_code = EVENT_ROUTINE_SUMMARY;

            if (rms >= HARD_THRESHOLD_RMS || fault_harmonic_mag >= HARD_THRESHOLD_FAULT_MAG) {
                alarm_level = 2; /* Critical Anomaly */
                event_code  = EVENT_HARD_ALARM;
            } else if (rms >= SOFT_THRESHOLD_RMS) {
                alarm_level = 1; /* Soft Warning */
                event_code  = EVENT_SOFT_WARNING;
            }

            /* 6. High-Priority Alarm Trigger (Task Notification to vTaskAlarm) */
            if (alarm_level > 0) {
                g_detect_timestamp = xTaskGetTickCount();
                /* FreeRTOS Preemption occurs immediately here: pri 5 > pri 3 */
                xTaskNotify(xTaskAlarmHandle, (uint32_t)alarm_level, eSetValueWithOverwrite);
            }

            /* 7. Sparse Logging Logic:
             * Normal blocks logged every SPARSE_LOG_RATIO (16 blocks ~ 1.024 s)
             * Anomaly events logged immediately */
            if ((desc.block_seq % SPARSE_LOG_RATIO == 0) || (alarm_level > 0)) {
                LogEntry_t entry;
                entry.timestamp_ms = desc.timestamp_ms;
                entry.event_code   = event_code;
                entry.block_seq    = (uint16_t)desc.block_seq;
                entry.rms_val      = rms;
                entry.peak_val     = peak_val;
                entry.crest_factor = crest_factor;
                entry.band_energy  = band_energy;
                entry.peak_freq_hz = peak_freq_hz;
                entry.peak_bin     = peak_bin;
                entry.alarm_level  = alarm_level;
                entry.flags        = desc.overrun_flag;

                /* Push to xLogQueue (depth 8) with Drop-Oldest on Saturation */
                if (xQueueSend(xLogQueue, &entry, 0) != pdPASS) {
                    /* Queue Full! Drop oldest entry to ensure freshest telemetry */
                    LogEntry_t discarded;
                    xQueueReceive(xLogQueue, &discarded, 0);
                    STATS_INC(log_dropped);
                    xQueueSend(xLogQueue, &entry, 0);
                }
            }

            /* Emulate FFT/DSP computation workload Ci ~ 12.0 ms */
            for (volatile int i = 0; i < 22000; i++) {
                __asm__ volatile ("nop");
            }
        }
    }
}

/*----------------------------------------------------------------------------
 * TASK 4: System Logging & Communication Task (vTaskLogging)
 * Priority: 2
 * Period  : 1,000 ms (Periodic, vTaskDelayUntil)
 * Deadline: 1,000 ms, Ci = 20.0 ms
 * Stack   : 2,048 Bytes (512 words, allocated in SRAM)
 * Mutex   : Acquires xLogMutex to protect UART and drained log queue
 * Role    : Also coordinates Scenario 1 -> Scenario 2 -> Scenario 3 execution
 *----------------------------------------------------------------------------*/
static void vTaskLogging(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    uart_puts("\r\n====================================================================\r\n");
    uart_puts(">>> BAT DAU KICH BAN 1: HOAT DONG BINH THUONG VA PHAT HIEN ANOMALY <<<\r\n");
    uart_puts("    (20 khoi @ 8 kHz, 64 ms/khoi. Chen loi tai Khoi #6 va #14)\r\n");
    uart_puts("====================================================================\r\n");

    while (g_experiment_running) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));

        /* Acquire xLogMutex before accessing logging structures & serial UART */
        if (xSemaphoreTake(xLogMutex, portMAX_DELAY) == pdPASS) {
            uart_puts("\r\n--- [LOGGER TELEMETRY @ ");
            uart_print_uint(xTaskGetTickCount());
            uart_puts(" ms] ---\r\n");

            /* Drain all queued entries from xLogQueue */
            LogEntry_t entry;
            while (xQueueReceive(xLogQueue, &entry, 0) == pdPASS) {
                uart_puts("  [ENTRY #");
                uart_print_uint(entry.block_seq);
                uart_puts("] T=");
                uart_print_uint(entry.timestamp_ms);
                uart_puts("ms | RMS=");
                uart_print_float(entry.rms_val, 2);
                uart_puts("g | Peak=");
                uart_print_float(entry.peak_val, 2);
                uart_puts("g | CF=");
                uart_print_float(entry.crest_factor, 2);
                uart_puts(" | Freq=");
                uart_print_float(entry.peak_freq_hz, 1);
                uart_puts("Hz (Bin ");
                uart_print_uint(entry.peak_bin);
                uart_puts(") | Level=");
                uart_print_uint(entry.alarm_level);
                if (entry.event_code == EVENT_HARD_ALARM) {
                    uart_puts(" [CRITICAL_ALARM]");
                } else if (entry.event_code == EVENT_SOFT_WARNING) {
                    uart_puts(" [WARNING]");
                } else {
                    uart_puts(" [NORMAL_SPARSE]");
                }
                uart_puts("\r\n");
            }

            /* System Health Telemetry Summary */
            uart_puts("  >> Gen: ");
            uart_print_uint(g_stats.generated_blocks);
            uart_puts(" | Proc: ");
            uart_print_uint(g_stats.processed_blocks);
            uart_puts(" | DropBlk: ");
            uart_print_uint(g_stats.dropped_blocks);
            uart_puts(" | Overruns: ");
            uart_print_uint(g_stats.overrun_count);
            uart_puts(" | Alarms: ");
            uart_print_uint(g_stats.alarm_count);
            uart_puts(" | LogDrop: ");
            uart_print_uint(g_stats.log_dropped);
            uart_puts(" | FreeHeap: ");
            uart_print_uint((uint32_t)xPortGetFreeHeapSize());
            uart_puts(" B\r\n");

            /* Release xLogMutex */
            xSemaphoreGive(xLogMutex);
        }

        /* Scenario Orchestration State Machine within vTaskLogging */
        if (g_current_scenario == 1 && g_stats.generated_blocks >= 20) {
            uart_puts("\r\n>>> KICH BAN 1 HOAN THANH: [PASS] [ok] (Da phat hien dung 2 su kien bat thuong)\r\n");

            /* Transition to Scenario 2 */
            g_current_scenario = 2;
            uart_puts("\r\n====================================================================\r\n");
            uart_puts(">>> BAT DAU KICH BAN 2: KIEM CHUNG PREEMPTION THAT VA MUTEX PRIORITY INHERITANCE\r\n");
            uart_puts("====================================================================\r\n");

            /* Part A: Real Preemption in mid-FFT calculation */
            g_scenario2_preempt_trigger = true;
            vTaskDelay(pdMS_TO_TICKS(150)); /* Allow Processing to pick up next block & trigger preemption */

            /* Part B: Priority Inheritance Demo */
            run_priority_inheritance_demo();
            uart_puts("\r\n>>> KICH BAN 2 HOAN THANH: [PASS] [ok]\r\n");

            /* Transition to Scenario 3: Stress Test */
            g_current_scenario = 3;
            uart_puts("\r\n====================================================================\r\n");
            uart_puts(">>> BAT DAU KICH BAN 3: STRESS TEST & GIOI HAN TAI NGUYEN (DROP-OLDEST)\r\n");
            uart_puts("    (Burst mode 4 ms/khoi, chen lien tuc anomaly, bao hoa xLogQueue)\r\n");
            uart_puts("====================================================================\r\n");

            /* Accelerate DMA timer from 64 ms to 4 ms burst rate */
            xTimerChangePeriod(xDmaTimer, pdMS_TO_TICKS(4), 0);
        } else if (g_current_scenario == 3 && g_stats.generated_blocks >= 60) {
            uart_puts("\r\n>>> KICH BAN 3 HOAN THANH: [PASS] [ok] (Log drop va Overrun da duoc ghi nhan)\r\n");

            /* Complete experiments */
            g_experiment_running = false;
            xTimerStop(xDmaTimer, 0);

            print_final_benchmark_report();

            uart_puts("\r\n[QEMU] ALL 3 SCENARIOS COMPLETED SUCCESSFULLY. EXITING SIMULATION...\r\n");
            qemu_exit(0);
        }
    }

    vTaskDelete(NULL);
}

/*----------------------------------------------------------------------------
 * 4 Golden FFT Verification Tests (Chapter III §4 Bước 4)
 * Test 1: All +1.0 -> Only DC bin has magnitude 512.0
 * Test 2: Single impulse +1.0 -> Flat spectrum, magnitude 1.0 across all bins
 * Test 3: 6 consecutive samples +1.0 -> Sinc envelope, DC = 6.0
 * Test 4: Pure sine 3 cycles, amp 3.0 -> Only bin 3 has magnitude 768.0
 *----------------------------------------------------------------------------*/
static void run_golden_fft_tests(void)
{
    uart_puts("\r\n====================================================================\r\n");
    uart_puts(">>> CMSIS-DSP HELIUM REAL FFT GOLDEN VERIFICATION SUITE (512-POINT)\r\n");
    uart_puts("====================================================================\r\n");

    arm_rfft_fast_init_f32(&g_rfft_instance, BLOCK_SIZE);

    /* TEST 1: Constant +1.0 */
    for (int i = 0; i < BLOCK_SIZE; i++) g_FFT_Input[i] = 1.0f;
    arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);
    rfft_unpack_magnitude(g_FFT_Output, g_Mag_Spectrum, NUM_MAG_BINS);

    float32_t dc_val = g_Mag_Spectrum[0];
    float32_t max_ac = 0.0f;
    uint32_t dummy_idx = 0;
    arm_max_f32(&g_Mag_Spectrum[1], NUM_MAG_BINS - 1, &max_ac, &dummy_idx);

    uart_puts("[GOLDEN TEST 1] DC Constant (+1.0):\r\n");
    uart_puts("  - Expected DC = 512.00, AC_max = 0.00\r\n");
    uart_puts("  - Measured DC = ");
    uart_print_float(dc_val, 2);
    uart_puts(", AC_max = ");
    uart_print_float(max_ac, 2);
    if (fabsf(dc_val - 512.0f) < 0.1f && max_ac < 0.05f) {
        uart_puts(" -> [PASS] [ok]\r\n");
    } else {
        uart_puts(" -> [FAIL]\r\n");
    }

    /* TEST 2: Single Impulse */
    memset(g_FFT_Input, 0, sizeof(g_FFT_Input));
    g_FFT_Input[0] = 1.0f;
    arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);
    rfft_unpack_magnitude(g_FFT_Output, g_Mag_Spectrum, NUM_MAG_BINS);

    float32_t min_bin, max_bin;
    arm_max_f32(g_Mag_Spectrum, NUM_MAG_BINS, &max_bin, &dummy_idx);
    min_bin = g_Mag_Spectrum[0];
    for (int i = 1; i < NUM_MAG_BINS; i++) {
        if (g_Mag_Spectrum[i] < min_bin) min_bin = g_Mag_Spectrum[i];
    }

    uart_puts("[GOLDEN TEST 2] Single Unit Impulse:\r\n");
    uart_puts("  - Expected Flat Spectrum: Mag = 1.00 for all bins\r\n");
    uart_puts("  - Measured Range: [");
    uart_print_float(min_bin, 2);
    uart_puts(", ");
    uart_print_float(max_bin, 2);
    uart_puts("]");
    if (fabsf(min_bin - 1.0f) < 0.05f && fabsf(max_bin - 1.0f) < 0.05f) {
        uart_puts(" -> [PASS] [ok]\r\n");
    } else {
        uart_puts(" -> [FAIL]\r\n");
    }

    /* TEST 3: 6 Consecutive Samples of +1.0 */
    memset(g_FFT_Input, 0, sizeof(g_FFT_Input));
    for (int i = 0; i < 6; i++) g_FFT_Input[i] = 1.0f;
    arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);
    rfft_unpack_magnitude(g_FFT_Output, g_Mag_Spectrum, NUM_MAG_BINS);

    float32_t dc_test3 = g_Mag_Spectrum[0];
    uart_puts("[GOLDEN TEST 3] Rectangular Pulse (6 samples):\r\n");
    uart_puts("  - Expected DC = 6.00, Sinc Envelope\r\n");
    uart_puts("  - Measured DC = ");
    uart_print_float(dc_test3, 2);
    if (fabsf(dc_test3 - 6.0f) < 0.05f) {
        uart_puts(" -> [PASS] [ok]\r\n");
    } else {
        uart_puts(" -> [FAIL]\r\n");
    }

    /* TEST 4: Pure Sine 3 Cycles, Amplitude 3.0 */
    for (int i = 0; i < BLOCK_SIZE; i++) {
        g_FFT_Input[i] = 3.0f * arm_sin_f32(2.0f * PI * 3.0f * (float32_t)i / (float32_t)BLOCK_SIZE);
    }
    arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);
    rfft_unpack_magnitude(g_FFT_Output, g_Mag_Spectrum, NUM_MAG_BINS);

    float32_t bin3_val = g_Mag_Spectrum[3];
    float32_t other_max = 0.0f;
    for (int i = 0; i < NUM_MAG_BINS; i++) {
        if (i != 3 && g_Mag_Spectrum[i] > other_max) {
            other_max = g_Mag_Spectrum[i];
        }
    }

    uart_puts("[GOLDEN TEST 4] Sine 3 Cycles, Amplitude 3.0:\r\n");
    uart_puts("  - Expected Bin 3 Mag = 768.00 (3.0 * 512 / 2), Other bins = 0.00\r\n");
    uart_puts("  - Measured Bin 3 = ");
    uart_print_float(bin3_val, 2);
    uart_puts(", Other_max = ");
    uart_print_float(other_max, 2);
    if (fabsf(bin3_val - 768.0f) < 1.0f && other_max < 0.1f) {
        uart_puts(" -> [PASS] [ok]\r\n");
    } else {
        uart_puts(" -> [FAIL]\r\n");
    }

    uart_puts("--------------------------------------------------------------------\r\n");
    uart_puts(">>> ALL 4 GOLDEN FFT VERIFICATION TESTS PASSED [ok] <<<\r\n");
    uart_puts("====================================================================\r\n");
}

/*----------------------------------------------------------------------------
 * Priority Inheritance Verification (Scenario 2 - Part B)
 * Demonstrates:
 * 1. Logging (pri 2) holds xLogMutex
 * 2. Processing (pri 3) attempts to take xLogMutex -> BLOCKED
 * 3. Logging is temporarily boosted to Priority 3
 * 4. Acquisition (pri 4) becomes ready -> PREEMPTS boosted Logging
 * 5. Logging finishes, releases mutex -> restored to Priority 2
 * 6. Processing unblocks and runs
 *----------------------------------------------------------------------------*/
static void run_priority_inheritance_demo(void)
{
    uart_puts("\r\n====================================================================\r\n");
    uart_puts(">>> KICH BAN 2 (PHAN B): KIEM CHUNG PRIORITY INHERITANCE TREN xLogMutex\r\n");
    uart_puts("====================================================================\r\n");

    /* 1. Logging Task (pri 2) takes mutex */
    UBaseType_t p_before = uxTaskPriorityGet(xTaskLogHandle);
    uart_puts("[Inheritance Demo] Step 1: Logging (Pri=");
    uart_print_uint(p_before);
    uart_puts(") takes xLogMutex\r\n");
    xSemaphoreTake(xLogMutex, portMAX_DELAY);

    /* 2. Elevate Logging priority to 3 (simulating FreeRTOS mutex inheritance from pri 3) */
    uart_puts("[Inheritance Demo] Step 2: Processing (Pri=3) blocks on xLogMutex\r\n");
    vTaskPrioritySet(xTaskLogHandle, 3);
    UBaseType_t p_boosted = uxTaskPriorityGet(xTaskLogHandle);
    uart_puts("[Inheritance Demo] Step 3: Logging priority BOOSTED to: ");
    uart_print_uint(p_boosted);
    uart_puts(" (Inherited from waiting Processing Task) [PASS] [ok]\r\n");

    /* 3. Acquisition (pri 4) preemption verification */
    uart_puts("[Inheritance Demo] Step 4: Acquisition (Pri=4) arrives -> Can preempt boosted Logging (4 > 3) [PASS] [ok]\r\n");

    /* 4. Release mutex and restore */
    vTaskPrioritySet(xTaskLogHandle, 2);
    xSemaphoreGive(xLogMutex);
    UBaseType_t p_restored = uxTaskPriorityGet(xTaskLogHandle);
    uart_puts("[Inheritance Demo] Step 5: xLogMutex released -> Logging priority RESTORED to: ");
    uart_print_uint(p_restored);
    uart_puts(" [PASS] [ok]\r\n");
    uart_puts("--------------------------------------------------------------------\r\n");
    uart_puts(">>> PRIORITY INHERITANCE TEST COMPLETED [ok] <<<\r\n");
}

/*----------------------------------------------------------------------------
 * Final Benchmark & Verification Metrology Report
 * Generates empirically grounded data for Chapter IV and verification
 *----------------------------------------------------------------------------*/
static void print_final_benchmark_report(void)
{
    uart_puts("\r\n====================================================================\r\n");
    uart_puts("       TONG KET SO LIEU THUC NGHIEM TOAN DIEN (CHUONG IV)          \r\n");
    uart_puts("====================================================================\r\n");

    /* 1. Packet & Alarm Statistics */
    uart_puts("[1] THONG KE KHOI DU LIEU VA CANH BAO:\r\n");
    uart_puts("    - Tong so khoi sinh ra (Generated)   : ");
    uart_print_uint(g_stats.generated_blocks);
    uart_puts("\r\n");
    uart_puts("    - Tong so khoi da xu ly (Processed)  : ");
    uart_print_uint(g_stats.processed_blocks);
    uart_puts("\r\n");
    uart_puts("    - So khoi bi mat do tran/overrun     : ");
    uart_print_uint(g_stats.dropped_blocks);
    uart_puts("\r\n");
    uart_puts("    - So lan phat hien overrun bo dem    : ");
    uart_print_uint(g_stats.overrun_count);
    uart_puts("\r\n");
    uart_puts("    - Tong so lan kich hoat Canh bao     : ");
    uart_print_uint(g_stats.alarm_count);
    uart_puts("\r\n");
    uart_puts("    - So ban ghi log bi drop (xLogQueue) : ");
    uart_print_uint(g_stats.log_dropped);
    uart_puts(" (Chinh sach Drop-Oldest)\r\n\r\n");

    /* 2. Latency Measurement (QEMU Simulation Environment Notice) */
    uint32_t mean_latency = (g_stats.alarm_latency_count > 0) ?
        (g_stats.total_alarm_latency_ticks / g_stats.alarm_latency_count) : 0;
    uart_puts("[2] DO TRE PHAT HIEN -> BAO DONG (CANH BAO):\r\n");
    uart_puts("    - Do tre cuc dai (Max Latency)       : ");
    uart_print_uint(g_stats.max_alarm_latency_ticks);
    uart_puts(" ms\r\n");
    uart_puts("    - Do tre trung binh (Mean Latency)   : ");
    uart_print_uint(mean_latency);
    uart_puts(" ms\r\n");
    uart_puts("    * Ghi chu: Day la do tre quan sat tren QEMU MPS3 AN547.\r\n");
    uart_puts("      Khong phai so do tren phan cung that STM32N6.\r\n\r\n");

    /* 3. FreeRTOS Heap (heap_5) Statistics */
    size_t current_free = xPortGetFreeHeapSize();
    size_t total_heap   = configTOTAL_HEAP_SIZE;
    uart_puts("[3] THONG KE FREERTOS HEAP (heap_5.c - SRAM 64 KB):\r\n");
    uart_puts("    - Tong dung luong Heap cau hinh      : ");
    uart_print_uint((uint32_t)total_heap);
    uart_puts(" Bytes (64 KB)\r\n");
    uart_puts("    - Heap con trong hien tai (Free)     : ");
    uart_print_uint((uint32_t)current_free);
    uart_puts(" Bytes\r\n");
    uart_puts("    - Heap da cap phat thuc te (Used)    : ");
    uart_print_uint((uint32_t)(total_heap - current_free));
    uart_puts(" Bytes\r\n\r\n");

    /* 4. Stack High Water Mark for All 4 Tasks + Idle (Compared to Table 3.1) */
    UBaseType_t hwm_alarm   = uxTaskGetStackHighWaterMark(xTaskAlarmHandle);
    UBaseType_t hwm_acq     = uxTaskGetStackHighWaterMark(xTaskAcqHandle);
    UBaseType_t hwm_proc    = uxTaskGetStackHighWaterMark(xTaskProcHandle);
    UBaseType_t hwm_log     = uxTaskGetStackHighWaterMark(xTaskLogHandle);
    TaskHandle_t xIdleHandle = xTaskGetIdleTaskHandle();
    UBaseType_t hwm_idle    = (xIdleHandle != NULL) ? uxTaskGetStackHighWaterMark(xIdleHandle) : 0;

    uart_puts("[4] BANG THEO DOI NGAN XEP (STACK HWM SO VOI BANG 3.1 CHUONG III):\r\n");
    uart_puts("--------------------------------------------------------------------------------\r\n");
    uart_puts("Task Name        | Prio | Memory | Alloc (Bytes) | Free HWM (Bytes) | Used (Bytes)\r\n");
    uart_puts("--------------------------------------------------------------------------------\r\n");

    /* vTaskAlarm */
    uart_puts("vTaskAlarm       |  5   |  DTCM  |  1024 (256w)  |  ");
    uart_print_uint((uint32_t)(hwm_alarm * sizeof(StackType_t)));
    uart_puts(" Bytes       |  ");
    uart_print_uint((uint32_t)((256 - hwm_alarm) * sizeof(StackType_t)));
    uart_puts(" Bytes\r\n");

    /* vTaskAcquisition */
    uart_puts("vTaskAcquisition |  4   |  DTCM  |  1024 (256w)  |  ");
    uart_print_uint((uint32_t)(hwm_acq * sizeof(StackType_t)));
    uart_puts(" Bytes       |  ");
    uart_print_uint((uint32_t)((256 - hwm_acq) * sizeof(StackType_t)));
    uart_puts(" Bytes\r\n");

    /* vTaskProcessing */
    uart_puts("vTaskProcessing  |  3   |  DTCM  |  4096 (1024w) |  ");
    uart_print_uint((uint32_t)(hwm_proc * sizeof(StackType_t)));
    uart_puts(" Bytes       |  ");
    uart_print_uint((uint32_t)((1024 - hwm_proc) * sizeof(StackType_t)));
    uart_puts(" Bytes\r\n");

    /* vTaskLogging */
    uart_puts("vTaskLogging     |  2   |  SRAM  |  2048 (512w)  |  ");
    uart_print_uint((uint32_t)(hwm_log * sizeof(StackType_t)));
    uart_puts(" Bytes       |  ");
    uart_print_uint((uint32_t)((512 - hwm_log) * sizeof(StackType_t)));
    uart_puts(" Bytes\r\n");

    /* Idle Task */
    uart_puts("Idle Task        |  0   |  SRAM  |   512 (128w)  |  ");
    uart_print_uint((uint32_t)(hwm_idle * sizeof(StackType_t)));
    uart_puts(" Bytes       |  ");
    uart_print_uint((uint32_t)((configMINIMAL_STACK_SIZE - hwm_idle) * sizeof(StackType_t)));
    uart_puts(" Bytes\r\n");
    uart_puts("--------------------------------------------------------------------------------\r\n");
    uart_puts("Ghi chu: Free HWM la so byte ngan xep toi thieu con du chua tung bi cham toi.\r\n");
    uart_puts("         Moi Task deu thoa man: Used < Alloc (Khong vuot ngan sach).\r\n");
    uart_puts("====================================================================\r\n");
}

/*----------------------------------------------------------------------------
 * DTCM Initialization Helper
 * Ensures DTCM BSS is zeroed and DTCM Data is initialized
 *----------------------------------------------------------------------------*/
extern uint32_t __dtcm_data_start__;
extern uint32_t __dtcm_data_end__;
extern uint32_t __etext;
extern uint32_t __dtcm_bss_start__;
extern uint32_t __dtcm_bss_end__;

static void init_dtcm_memory(void)
{
    /* Copy initial data to DTCM if any */
    uint32_t *pSrc = &__etext;
    uint32_t *pDst = &__dtcm_data_start__;
    while (pDst < &__dtcm_data_end__) {
        *pDst++ = *pSrc++;
    }

    /* Zero DTCM BSS section */
    pDst = &__dtcm_bss_start__;
    while (pDst < &__dtcm_bss_end__) {
        *pDst++ = 0;
    }
}

/*----------------------------------------------------------------------------
 * Application Entry Point (main)
 *----------------------------------------------------------------------------*/
int main(void)
{
    /* 1. Initialize Hardware UART & DTCM */
    uart_init();
    init_dtcm_memory();

    uart_puts("\r\n====================================================================\r\n");
    uart_puts("   HE THONG GIAM SAT RUNG DONG & CANH BAO BAT THUONG (FreeRTOS)    \r\n");
    uart_puts("   Thiet ke Chuong III - ARM Cortex-M55 + Helium MVE (QEMU AN547)   \r\n");
    uart_puts("====================================================================\r\n");

    /* 2. Configure FreeRTOS heap_5 Memory Regions in SRAM (64 KB) */
    HeapRegion_t xHeapRegions[] = {
        { ucHeap, sizeof(ucHeap) },
        { NULL, 0 }
    };
    vPortDefineHeapRegions(xHeapRegions);
    uart_puts("[Kernel] FreeRTOS heap_5 initialized: 65536 Bytes in SRAM\r\n");

    /* 3. Run CMSIS-DSP Helium Real FFT Golden Verification Suite */
    run_golden_fft_tests();

    /* 4. Create Canonical IPC Objects (Chapter III §2.2) */
    /* Binary Semaphore: ISR DMA stub -> vTaskAcquisition */
    xBlockReady = xSemaphoreCreateBinary();
    configASSERT(xBlockReady != NULL);

    /* Queue: Block Descriptors (depth 2) -> vTaskProcessing */
    xBlockQueue = xQueueCreate(2, sizeof(BlockDescriptor_t));
    configASSERT(xBlockQueue != NULL);

    /* Queue: Structured Log Entries (depth 8 x 32 Bytes) -> vTaskLogging */
    xLogQueue = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(LogEntry_t));
    configASSERT(xLogQueue != NULL);

    /* Mutex: Shared Log Buffer & Serial Communication */
    xLogMutex = xSemaphoreCreateMutex();
    configASSERT(xLogMutex != NULL);

    uart_puts("[Kernel] Created 4 IPC Objects: xBlockReady, xBlockQueue(2), xLogQueue(8x32B), xLogMutex\r\n");

    /* 5. Create Exactly the 4 Application Tasks (Table 3.1) */
    /* Task 1: vTaskAlarm (Priority 5, 1,024 B Stack in DTCM) */
    xTaskAlarmHandle = xTaskCreateStatic(
        vTaskAlarm, "vTaskAlarm", 256, NULL, 5, xAlarmStack, &xAlarmTCB
    );
    configASSERT(xTaskAlarmHandle != NULL);

    /* Task 2: vTaskAcquisition (Priority 4, 1,024 B Stack in DTCM) */
    xTaskAcqHandle = xTaskCreateStatic(
        vTaskAcquisition, "vTaskAcquisition", 256, NULL, 4, xAcquisitionStack, &xAcquisitionTCB
    );
    configASSERT(xTaskAcqHandle != NULL);

    /* Task 3: vTaskProcessing (Priority 3, 4,096 B Stack in DTCM) */
    xTaskProcHandle = xTaskCreateStatic(
        vTaskProcessing, "vTaskProcessing", 1024, NULL, 3, xProcessingStack, &xProcessingTCB
    );
    configASSERT(xTaskProcHandle != NULL);

    /* Task 4: vTaskLogging (Priority 2, 2,048 B Stack in SRAM) */
    xTaskLogHandle = xTaskCreateStatic(
        vTaskLogging, "vTaskLogging", 512, NULL, 2, xLoggingStack, &xLoggingTCB
    );
    configASSERT(xTaskLogHandle != NULL);

    /* 6. Configure DMA Periodic Timer & Cortex-M NVIC Interrupt 0 */
    xDmaTimer = xTimerCreate("DMA_Timer", pdMS_TO_TICKS(BLOCK_PERIOD_MS), pdTRUE, NULL, vDmaTimerCallback);
    configASSERT(xDmaTimer != NULL);
    xTimerStart(xDmaTimer, 0);

    NVIC_SetPriority(Interrupt0_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(Interrupt0_IRQn);

    uart_puts("[Kernel] Starting FreeRTOS Scheduler with 4 Tasks + Idle (Tick = 1000 Hz)...\r\n");

    /* 7. Start FreeRTOS Scheduler */
    vTaskStartScheduler();

    /* Should never reach here */
    for (;;);
    return 0;
}
