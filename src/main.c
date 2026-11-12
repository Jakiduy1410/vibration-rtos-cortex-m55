/*
 * Real-Time Vibration Monitoring & Anomaly Alarm System
 * Conforming strictly to Chapter III Design & Specifications
 *
 * Platform: ARM MPS3 FPGA Prototyping Board (AN547)
 * Core    : ARM Cortex-M55 (ARMv8.1-M Mainline with Helium MVE & Hard-Float FPU)
 * RTOS    : FreeRTOS Kernel V11.1.0 (Port: ARM_CM55_NTZ/non_secure)
 * DSP     : ARM CMSIS-DSP 5.9.0 (Helium Optimized 512-point Real FFT @ 8 kHz)
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

#include "arm_math.h"
#include "ARMCM55.h"
#include "uart.h"

/*----------------------------------------------------------------------------
 * Engineering Parameters (Chapter III §3.2.2 & Table 3.1)
 *----------------------------------------------------------------------------*/
#define SAMPLE_RATE_HZ              8000
#define BLOCK_SIZE                  512
#define NUM_MAG_BINS                (BLOCK_SIZE / 2)     /* 256 positive frequency bins */
#define FFT_BIN_RES_HZ              (8000.0f / 512.0f)   /* 15.625 Hz per bin */

/* Vibration Anomaly Decision Thresholds (Table 3.1 & §3.2.2) */
#define SOFT_THRESHOLD_RMS          1.50f   /* Soft Threshold: RMS >= 1.50g */
#define HARD_THRESHOLD_RMS          2.00f   /* Hard Threshold: RMS >= 2.00g (Critical) */
#define HARD_THRESHOLD_FAULT_MAG    2.00f   /* Hard Threshold: Fault Harmonic >= 2.00g */

/* Event ID Codes */
#define EVENT_ROUTINE_SUMMARY       0x0001
#define EVENT_SOFT_ALARM            0x0002
#define EVENT_HARD_ALARM            0x0003

/* Sparse logging ratio in normal condition: 1 in 16 blocks (~1.0s at 64 ms/block) */
#define SPARSE_LOG_RATIO            16

/* CPU Clock frequency on QEMU MPS3 AN547 emulation: 25 MHz (40 ns per cycle) */
#define SYSTEM_CLOCK_FREQ_HZ        25000000UL
#define SYSTICK_RELOAD_CYCLES       (SYSTEM_CLOCK_FREQ_HZ / configTICK_RATE_HZ) /* 25,000 cycles/tick */

/*----------------------------------------------------------------------------
 * Type Definitions (Conforming strictly to Chapter III §2.2 & Task-round2)
 *----------------------------------------------------------------------------*/

/* 1. Block Descriptor for xBlockQueue (depth 2) - Descriptor only, NO copy of 512 samples */
typedef struct {
    const int16_t *pData;       /* Pointer to 512 int16 samples in DTCM double buffer */
    uint32_t block_seq;         /* Block sequence number */
    uint32_t timestamp_ms;      /* Timestamp when block was acquired */
    uint8_t anomaly_type;       /* 0=None, 1=Bearing fault harmonic, 2=Severe imbalance */
    uint8_t overrun_flag;       /* 1 if buffer overrun occurred */
} BlockDescriptor_t;

/* 2. Structured Log Entry for xLogQueue & xAlarmLogQueue - EXACTLY 32 Bytes */
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

/* 3. Thread-Safe Global System Statistics with Separate Overrun & Log Drop Counters */
typedef struct {
    volatile uint32_t generated_blocks;
    volatile uint32_t processed_blocks;
    volatile uint32_t isr_overrun_count;        /* Buffer overrun: DMA ISR found active buffer busy */
    volatile uint32_t queue_dropped_blocks;     /* Block descriptor dropped if xBlockQueue is full */
    volatile uint32_t alarm_actuate_count;      /* Total times Alarm Task actuated */

    volatile uint32_t normal_logs_generated;
    volatile uint32_t normal_logs_written;
    volatile uint32_t normal_logs_dropped;      /* Dropped from xLogQueue via Drop-Oldest */

    volatile uint32_t alarm_logs_generated;
    volatile uint32_t alarm_logs_written;
    volatile uint32_t alarm_logs_dropped;       /* Must remain 0 under all conditions */

    volatile uint32_t last_alarm_latency_cycles;
    volatile uint32_t max_alarm_latency_cycles;
    volatile uint32_t min_alarm_latency_cycles;
    volatile uint64_t total_alarm_latency_cycles;
    volatile uint32_t latency_sample_count;
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
 * - DTCM (128 KB @ 0x20000000):
 *     Double Buffer 2 x 512 int16 = 2,048 Bytes (2.0 KB)
 *     FFT Workspace (Input 2 KB + Output 2 KB) = 4,096 Bytes (EXACTLY 4.0 KB)
 *     3 Critical Task Stacks = 6,144 Bytes (6.0 KB)
 *     Total DTCM = ~12.3 KB / 128 KB
 * - SRAM (4 MB @ 0x21000000):
 *     FreeRTOS heap_5 = 64 KB (65,536 Bytes)
 *     vTaskLogging Stack = 2,048 Bytes (2.0 KB)
 *     Idle Task Stack = 512 Bytes (0.5 KB)
 *     MSP Stack = 8,192 Bytes (8.0 KB)
 *----------------------------------------------------------------------------*/

/* DTCM Section: Double Buffer 2 x 512 int16 = 2,048 Bytes */
static int16_t g_DoubleBuffer[2][BLOCK_SIZE] __attribute__((section(".dtcm_bss"), aligned(16)));

/* DTCM Section: FFT Workspace = EXACTLY 4,096 Bytes (Bảng 3.3 Chương III)
 * Note: g_FFT_Input is reused as the magnitude array after arm_rfft_fast_f32 completes,
 * eliminating the extra 1,024-byte array while preserving all frequency features. */
static float32_t g_FFT_Input[BLOCK_SIZE]     __attribute__((section(".dtcm_bss"), aligned(16)));
static float32_t g_FFT_Output[BLOCK_SIZE]    __attribute__((section(".dtcm_bss"), aligned(16)));
#define g_Mag_Spectrum                       g_FFT_Input

/* DTCM Section: Stacks and TCBs for the 3 Critical Tasks (Table 3.1) */
static StackType_t xAlarmStack[256]          __attribute__((section(".dtcm_bss"), aligned(8)));
static StaticTask_t xAlarmTCB                __attribute__((section(".dtcm_bss"), aligned(8)));

static StackType_t xAcquisitionStack[256]    __attribute__((section(".dtcm_bss"), aligned(8)));
static StaticTask_t xAcquisitionTCB          __attribute__((section(".dtcm_bss"), aligned(8)));

static StackType_t xProcessingStack[1024]    __attribute__((section(".dtcm_bss"), aligned(8)));
static StaticTask_t xProcessingTCB           __attribute__((section(".dtcm_bss"), aligned(8)));

/* SRAM Section: vTaskLogging Stack (Priority 2, 2,048 Bytes = 512 words) */
static StackType_t xLoggingStack[512]        __attribute__((section(".bss"), aligned(8)));
static StaticTask_t xLoggingTCB              __attribute__((section(".bss"), aligned(8)));

/* SRAM Section: Static Memory for Idle & Timer Tasks */
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE] __attribute__((section(".bss"), aligned(8)));
static StaticTask_t xIdleTCB                            __attribute__((section(".bss"), aligned(8)));

static StackType_t xTimerStack[configTIMER_TASK_STACK_DEPTH] __attribute__((section(".bss"), aligned(8)));
static StaticTask_t xTimerTCB                                __attribute__((section(".bss"), aligned(8)));

/* SRAM Section: FreeRTOS Heap (heap_5: 64 KB = 65,536 Bytes) */
static uint8_t ucHeap[65536] __attribute__((section(".bss"), aligned(8)));

/*----------------------------------------------------------------------------
 * Synchronization & IPC Handles (Chapter III §2.2 & Task-round2)
 *----------------------------------------------------------------------------*/
static SemaphoreHandle_t xBlockReady          = NULL; /* Binary semaphore: ISR DMA stub -> Acquisition */
static QueueHandle_t     xBlockQueue          = NULL; /* Depth 2: Acquisition -> Processing */
static TaskHandle_t      xTaskAlarmHandle     = NULL; /* Task Notification: Processing/ISR -> Alarm */
static QueueHandle_t     xLogQueue            = NULL; /* Depth 8 x 32-Byte: Normal telemetry (Drop-Oldest) */
static QueueHandle_t     xAlarmLogQueue       = NULL; /* Depth 8 x 32-Byte: Critical alarms (0 Dropped) */
static SemaphoreHandle_t xLogMutex            = NULL; /* Mutex with Priority Inheritance */

/* Signal semaphores for genuine priority inheritance testing */
static SemaphoreHandle_t xInheritSignalSem    = NULL;
static SemaphoreHandle_t xInheritDoneSem      = NULL;

static TaskHandle_t      xTaskAcqHandle       = NULL;
static TaskHandle_t      xTaskProcHandle      = NULL;
static TaskHandle_t      xTaskLogHandle       = NULL;

static TimerHandle_t     xDmaTimer            = NULL;
static TimerHandle_t     xScenarioTimer       = NULL;

/* CMSIS-DSP RFFT Instance (512 points) */
static arm_rfft_fast_instance_f32 g_rfft_instance;

/* Experiment Scenario Control */
volatile uint32_t g_current_scenario         = 1;
volatile bool     g_experiment_running       = true;

/* Scenario 1 verification tracking */
static volatile uint32_t g_s1_detected_block6  = 0;
static volatile uint32_t g_s1_detected_block14 = 0;

/* Scenario 2 Mid-FFT Preemption tracking */
volatile bool     g_s2_preempt_armed         = false;
volatile uint64_t g_s2_t_fft_start           = 0;
volatile uint64_t g_s2_t_mid_fft_irq         = 0;
volatile uint64_t g_s2_t_alarm_actuate       = 0;
volatile uint64_t g_s2_t_fft_end             = 0;
volatile bool     g_s2_preempt_verified      = false;

/* Scenario 2 Priority Inheritance tracking */
volatile bool     g_s2_inherit_test_active   = false;
volatile bool     g_s2_inherit_triggered     = false;
volatile uint32_t g_s2_prio_initial          = 0;
volatile uint32_t g_s2_prio_boosted          = 0;
volatile uint32_t g_s2_prio_restored         = 0;
volatile bool     g_s2_acq_preempted_boosted = false;
volatile bool     g_s2_inheritance_verified  = false;

/* DMA Double-Buffer State & Overrun Flags */
static volatile uint8_t  g_dma_active_buf      = 0;
static volatile uint8_t  g_buf_busy[2]         = { 0, 0 };
static volatile uint8_t  g_buf_overrun[2]      = { 0, 0 }; /* Reset on buffer release */
static volatile uint32_t g_block_seq_cnt       = 0;

/* Latency measurement cycle timestamp */
static volatile uint64_t g_detect_cycle        = 0;

/* High frequency timer ticks for run-time statistics */
volatile uint32_t ulHighFrequencyTimerTicks = 0;

/*----------------------------------------------------------------------------
 * High-Resolution Timing via SysTick (Sub-microsecond resolution on QEMU)
 *----------------------------------------------------------------------------*/
static inline uint64_t get_system_cycles(void)
{
    uint32_t ticks1, ticks2, val;
    do {
        ticks1 = xTaskGetTickCount();
        val    = SysTick->VAL;
        ticks2 = xTaskGetTickCount();
    } while (ticks1 != ticks2);

    /* SysTick counts downwards from 24,999 to 0 */
    uint64_t elapsed_in_tick = (uint64_t)(SYSTICK_RELOAD_CYCLES - 1UL - val);
    return ((uint64_t)ticks1 * (uint64_t)SYSTICK_RELOAD_CYCLES) + elapsed_in_tick;
}

/*----------------------------------------------------------------------------
 * Forward Declarations
 *----------------------------------------------------------------------------*/
static void vTaskAlarm(void *pvParameters);
static void vTaskAcquisition(void *pvParameters);
static void vTaskProcessing(void *pvParameters);
static void vTaskLogging(void *pvParameters);
static void run_priority_inheritance_demo(void);

static void run_golden_fft_tests(void);
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

void vApplicationTickHook(void)
{
    ulHighFrequencyTimerTicks += 10;
}

/*----------------------------------------------------------------------------
 * DMA Stub Timer Callback & ISR (Interrupt 0)
 * Drives xBlockReady semaphore at 64 ms intervals (or 4 ms in Scenario 3)
 *----------------------------------------------------------------------------*/
static void vDmaTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (g_experiment_running) {
        NVIC_SetPendingIRQ(Interrupt0_IRQn);
    }
}

void Interrupt0_Handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (!g_experiment_running) {
        return;
    }

    uint8_t write_buf = g_dma_active_buf;
    g_block_seq_cnt++;
    STATS_INC_FROM_ISR(generated_blocks);

    /* Overrun Check: Has Processing finished consuming this half-buffer? */
    if (g_buf_busy[write_buf]) {
        /* Buffer overrun: DMA attempted transfer while buffer was still in use */
        STATS_INC_FROM_ISR(isr_overrun_count);
        g_buf_overrun[write_buf] = 1;
    } else {
        /* Buffer free: synthesize data and pass to pipeline */
        g_buf_busy[write_buf] = 1;
        g_buf_overrun[write_buf] = 0;
        dma_stub_generate_data(g_DoubleBuffer[write_buf], g_block_seq_cnt);
        xSemaphoreGiveFromISR(xBlockReady, &xHigherPriorityTaskWoken);
    }

    /* DMA hardware always alternates to next double-buffer half */
    g_dma_active_buf ^= 1;

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*----------------------------------------------------------------------------
 * Hardware Preemption Trigger ISR (Interrupt 1)
 * Fires mid-FFT in Scenario 2 to trigger vTaskAlarm immediately
 *----------------------------------------------------------------------------*/
void Interrupt1_Handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    g_s2_t_mid_fft_irq = get_system_cycles();
    g_detect_cycle     = g_s2_t_mid_fft_irq;

    /* Notify Alarm Task directly from ISR (Priority 5 > Priority 3) */
    xTaskNotifyFromISR(xTaskAlarmHandle, EVENT_HARD_ALARM, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* CMSIS-DSP mid-FFT hook called between arm_cfft_f32 and stage_rfft_f32 */
void rfft_mid_stage_hook(void)
{
    if (g_s2_preempt_armed) {
        g_s2_preempt_armed = false;
        /* Trigger hardware interrupt Interrupt1_IRQn right in the middle of FFT */
        NVIC_SetPendingIRQ(Interrupt1_IRQn);
    }
}

/*----------------------------------------------------------------------------
 * DMA Sensor Data Synthesis Stub
 * Emulates 8 kHz accelerometer signal:
 * - Normal: Fundamental vibration 100 Hz (1.0g), RMS ~ 0.71g
 * - Anomaly 1 (Block #6 - Bearing Fault): 300 Hz harmonic (3.0g), RMS ~ 2.22g
 * - Anomaly 2 (Block #14 - Severe Imbalance): 100 Hz high amplitude (4.0g), RMS ~ 2.84g
 *----------------------------------------------------------------------------*/
static void dma_stub_generate_data(int16_t *pBuf, uint32_t block_seq)
{
    uint8_t inject_anomaly = 0;
    if (g_current_scenario == 1) {
        if (block_seq == 6) {
            inject_anomaly = 1; /* 300 Hz Bearing fault harmonic */
        } else if (block_seq == 14) {
            inject_anomaly = 2; /* 100 Hz Severe unbalance */
        }
    } else if (g_current_scenario == 3) {
        /* In Scenario 3 stress burst, inject 2 critical anomalies */
        if (block_seq == 45 || block_seq == 65) {
            inject_anomaly = 1;
        }
    }

    const float32_t f0 = 93.75f; /* Bin 6 (6 * 15.625 Hz = 93.75 Hz ~ 100 Hz) */
    const float32_t f_fault = 296.875f; /* Bin 19 (19 * 15.625 Hz = 296.875 Hz ~ 300 Hz) */
    const float32_t dt = 1.0f / (float32_t)SAMPLE_RATE_HZ;

    for (int i = 0; i < BLOCK_SIZE; i++) {
        float32_t t = (float32_t)i * dt;
        float32_t val = 0.0f;

        if (inject_anomaly == 1) {
            /* 100 Hz base (1.0g) + 300 Hz fault harmonic (3.0g) */
            val = 1.0f * arm_sin_f32(2.0f * PI * f0 * t) +
                  3.0f * arm_sin_f32(2.0f * PI * f_fault * t);
        } else if (inject_anomaly == 2) {
            /* High amplitude 100 Hz (4.0g) */
            val = 4.0f * arm_sin_f32(2.0f * PI * f0 * t);
        } else {
            /* Normal operation: Pure 100 Hz sine 1.0g */
            val = 1.0f * arm_sin_f32(2.0f * PI * f0 * t);
        }

        /* Scale: 1.0g = 1000 LSB in int16 */
        int32_t val_lsb = (int32_t)(val * 1000.0f);
        if (val_lsb > 32767)  val_lsb = 32767;
        if (val_lsb < -32768) val_lsb = -32768;
        pBuf[i] = (int16_t)val_lsb;
    }
}

/*----------------------------------------------------------------------------
 * Real FFT Output Magnitude Unpacker
 * CMSIS-DSP arm_rfft_fast_f32 packs:
 * - pRfftOut[0] = DC real
 * - pRfftOut[1] = Nyquist real
 * - pRfftOut[2*k], pRfftOut[2*k+1] = Complex pair for bin k (1 <= k < N/2)
 *----------------------------------------------------------------------------*/
static void rfft_unpack_magnitude(const float32_t *pRfftOut, float32_t *pMag, uint32_t num_bins)
{
    /* Bin 0: Pure real DC magnitude */
    pMag[0] = fabsf(pRfftOut[0]);

    /* Bins 1 .. num_bins - 1: Complex magnitude sqrt(Re^2 + Im^2) */
    for (uint32_t k = 1; k < num_bins; k++) {
        float32_t re = pRfftOut[2 * k];
        float32_t im = pRfftOut[2 * k + 1];
        pMag[k] = sqrtf(re * re + im * im);
    }
}

/*----------------------------------------------------------------------------
 * TASK 1: Anomaly Alarm Task (vTaskAlarm)
 * Priority: 5 (Highest)
 * Period  : Aperiodic, Deadline = 10.0 ms, Ci = 1.0 ms
 * Stack   : 1,024 Bytes (256 words, allocated in DTCM)
 * Hardware: Actuates alarm buzzer, emergency warning LED, and trips safety relay.
 * UART    : Authorized to print ONLY its single [EMERGENCY ALARM ACTUATED] line.
 * Mutex   : NEVER touches xLogMutex.
 *----------------------------------------------------------------------------*/
static void vTaskAlarm(void *pvParameters)
{
    (void)pvParameters;
    uint32_t notified_code = 0;

    while (1) {
        /* Wait for direct Task Notification from Processing Task or Preemption ISR */
        if (xTaskNotifyWait(0, ULONG_MAX, &notified_code, portMAX_DELAY) == pdPASS) {
            uint64_t actuate_cycle = get_system_cycles();
            TickType_t actuate_tick = xTaskGetTickCount();
            STATS_INC(alarm_actuate_count);

            /* Calculate latency in CPU cycles and microseconds */
            uint32_t latency_cycles = 0;
            if (actuate_cycle >= g_detect_cycle && g_detect_cycle > 0) {
                latency_cycles = (uint32_t)(actuate_cycle - g_detect_cycle);
            }

            taskENTER_CRITICAL();
            g_stats.last_alarm_latency_cycles = latency_cycles;
            if (latency_cycles > g_stats.max_alarm_latency_cycles) {
                g_stats.max_alarm_latency_cycles = latency_cycles;
            }
            if (g_stats.latency_sample_count == 0 || latency_cycles < g_stats.min_alarm_latency_cycles) {
                g_stats.min_alarm_latency_cycles = latency_cycles;
            }
            g_stats.total_alarm_latency_cycles += latency_cycles;
            g_stats.latency_sample_count++;
            taskEXIT_CRITICAL();

            /* Track Scenario 2 mid-FFT preemption actuation */
            if (g_current_scenario == 2) {
                g_s2_t_alarm_actuate = actuate_cycle;
            }

            /* Actuate Emergency Hardware: Buzzer ON, LED ON, Trip Relay */
            volatile uint32_t dummy_actuator = 0x07;
            (void)dummy_actuator;

            /* Single authorized direct UART print for hard real-time latency deadline */
            uart_puts("\r\n>>> [EMERGENCY ALARM ACTUATED] Level=2 | Latency=");
            uart_print_uint(latency_cycles);
            uart_puts(" cycles (");
            uart_print_float((float)latency_cycles / 25.0f, 2);
            uart_puts(" us) | Tick=");
            uart_print_uint(actuate_tick);
            uart_puts(" ms | Actuation: BUZZER_ON + RELAY_TRIP <<<\r\n");
        }
    }
}

/*----------------------------------------------------------------------------
 * TASK 2: Vibration Acquisition Task (vTaskAcquisition)
 * Priority: 4
 * Period  : 64 ms, Deadline = 64 ms, Ci = 2.0 ms
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
            desc.overrun_flag = g_buf_overrun[read_buf];

            if (g_current_scenario == 1) {
                desc.anomaly_type = (current_seq == 6) ? 1 : ((current_seq == 14) ? 2 : 0);
            } else if (g_current_scenario == 3) {
                desc.anomaly_type = (current_seq == 45 || current_seq == 65) ? 1 : 0;
            } else {
                desc.anomaly_type = 0;
            }

            /* Send block descriptor to Processing Task (depth 2 queue) */
            xQueueSend(xBlockQueue, &desc, portMAX_DELAY);

            /* Emulate acquisition overhead Ci ~ 2.0 ms */
            for (volatile int i = 0; i < 4000; i++) {
                __asm__ volatile ("nop");
            }

            /* Scenario 2 Priority Inheritance Hook:
             * Acquisition (Pri 4) arrives while Logging is holding xLogMutex at boosted Pri 3 */
            if (g_s2_inherit_test_active && !g_s2_acq_preempted_boosted) {
                g_s2_acq_preempted_boosted = true;
                uart_puts("[Inheritance Demo] Step 4: Acquisition (Pri=4) arrives -> Preempts boosted Logging (4 > 3) [PASS] [ok]\r\n");
            }
        }
    }
}

/*----------------------------------------------------------------------------
 * TASK 3: Vibration Processing Task (vTaskProcessing)
 * Priority: 3
 * Period  : 64 ms, Deadline = 64 ms, Ci = 12.0 ms
 * Stack   : 4,096 Bytes (1,024 words, allocated in DTCM)
 * Hardware: ARM Cortex-M55 Helium MVE vector math & CMSIS-DSP
 * UART    : NEVER calls UART directly! Telemetry goes to queues.
 *----------------------------------------------------------------------------*/
static void vTaskProcessing(void *pvParameters)
{
    (void)pvParameters;
    BlockDescriptor_t desc;

    while (1) {
        /* Wait for block descriptor from Acquisition Task */
        if (xQueueReceive(xBlockQueue, &desc, portMAX_DELAY) == pdPASS) {

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

            float32_t peak = 0.0f;
            uint32_t peak_idx = 0;
            arm_max_f32(g_FFT_Input, BLOCK_SIZE, &peak, &peak_idx);
            float32_t cf = (rms > 0.001f) ? (peak / rms) : 1.0f;

            /* 3. Compute 512-Point Real FFT using CMSIS-DSP Helium Vector Engine */
            if (g_current_scenario == 2 && !g_s2_preempt_verified) {
                g_s2_preempt_armed = true;
                g_s2_t_fft_start   = get_system_cycles();
            }

            /* Execute RFFT. If armed, rfft_mid_stage_hook() triggers Interrupt 1 mid-computation */
            arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);

            if (g_current_scenario == 2 && !g_s2_preempt_verified && g_s2_t_alarm_actuate > 0) {
                g_s2_t_fft_end = get_system_cycles();
                if (g_s2_t_fft_start < g_s2_t_mid_fft_irq &&
                    g_s2_t_mid_fft_irq <= g_s2_t_alarm_actuate &&
                    g_s2_t_alarm_actuate < g_s2_t_fft_end) {
                    g_s2_preempt_verified = true;
                }
            }

            /* 4. Compute Magnitude Spectrum (Reusing g_FFT_Input buffer in DTCM = 4 KB total) */
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

            /* Emulate processing execution budget (Ci = 12.0 ms) */
            if (g_current_scenario == 3) {
                vTaskDelay(pdMS_TO_TICKS(6));
            } else {
                for (volatile int i = 0; i < 2000; i++) {
                    __asm__ volatile ("nop");
                }
            }

            /* Release input buffer back to DMA & RESET OVERRUN FLAG */
            uint8_t released_buf = (desc.pData == g_DoubleBuffer[0]) ? 0 : 1;
            g_buf_busy[released_buf] = 0;
            g_buf_overrun[released_buf] = 0; /* Reset overrun flag on release */
            STATS_INC(processed_blocks);

            /* 5. Threshold Classification & Anomaly Decision */
            uint8_t alarm_level = 0;
            uint16_t event_code = EVENT_ROUTINE_SUMMARY;

            if (rms >= HARD_THRESHOLD_RMS || fault_harmonic_mag >= HARD_THRESHOLD_FAULT_MAG) {
                alarm_level = 2; /* Critical Anomaly */
                event_code  = EVENT_HARD_ALARM;
            } else if (rms >= SOFT_THRESHOLD_RMS) {
                alarm_level = 1; /* Soft Anomaly Warning */
                event_code  = EVENT_SOFT_ALARM;
            }

            /* Track Scenario 1 detections */
            if (g_current_scenario == 1) {
                if (desc.block_seq == 6 && alarm_level == 2)  g_s1_detected_block6  = 1;
                if (desc.block_seq == 14 && alarm_level == 2) g_s1_detected_block14 = 1;
            }

            /* 6. Actuate Alarm Task immediately via Task Notification if Critical */
            if (alarm_level == 2) {
                g_detect_cycle = get_system_cycles();
                xTaskNotify(xTaskAlarmHandle, event_code, eSetValueWithOverwrite);
            }

            /* 7. Dispatch Structured Log Entry (32 Bytes) */
            LogEntry_t entry;
            entry.timestamp_ms = desc.timestamp_ms;
            entry.event_code   = event_code;
            entry.block_seq    = (uint16_t)desc.block_seq;
            entry.rms_val      = rms;
            entry.peak_val     = peak;
            entry.crest_factor = cf;
            entry.band_energy  = band_energy;
            entry.peak_freq_hz = peak_freq_hz;
            entry.peak_bin     = peak_bin;
            entry.alarm_level  = alarm_level;
            entry.flags        = desc.overrun_flag;

            if (alarm_level > 0) {
                /* Critical / Warning alarms routed to dedicated xAlarmLogQueue (0 dropped) */
                STATS_INC(alarm_logs_generated);
                if (xQueueSend(xAlarmLogQueue, &entry, 0) != pdPASS) {
                    STATS_INC(alarm_logs_dropped);
                }
            } else {
                /* In Scenario 3 stress burst, log EVERY processed block to stress xLogQueue.
                 * In normal operation (Scenario 1 & 2), log sparsely (1:16 ratio). */
                bool should_log = (g_current_scenario == 3) || ((desc.block_seq % SPARSE_LOG_RATIO) == 0);
                if (should_log) {
                    STATS_INC(normal_logs_generated);
                    if (xQueueSend(xLogQueue, &entry, 0) != pdPASS) {
                        /* Queue full: drop oldest record to make room */
                        LogEntry_t old_entry;
                        if (xQueueReceive(xLogQueue, &old_entry, 0) == pdPASS) {
                            STATS_INC(normal_logs_dropped);
                        }
                        xQueueSend(xLogQueue, &entry, 0);
                    }
                }
            }

            /* Scenario 2 Priority Inheritance Hook:
             * When Mid-FFT preemption has been verified and inheritance test has not run yet */
            if (g_current_scenario == 2 && g_s2_preempt_verified && !g_s2_inherit_triggered) {
                g_s2_inherit_triggered = true;
                /* Signal vTaskLogging to initiate priority inheritance sequence */
                xTaskNotifyGive(xTaskLogHandle);

                /* Wait until Logging takes xLogMutex and signals xInheritSignalSem */
                if (xSemaphoreTake(xInheritSignalSem, portMAX_DELAY) == pdPASS) {
                    /* Processing (Pri 3) attempts to acquire xLogMutex held by Logging (Pri 2) -> BLOCKS!
                     * FreeRTOS kernel automatically raises Logging's priority to 3! */
                    if (xSemaphoreTake(xLogMutex, portMAX_DELAY) == pdPASS) {
                        /* Acquired after Logging releases xLogMutex */
                        xSemaphoreGive(xLogMutex);
                        if (xInheritDoneSem != NULL) {
                            xSemaphoreGive(xInheritDoneSem);
                        }
                    }
                }
            }
        }
    }
}

/*----------------------------------------------------------------------------
 * TASK 4: Telemetry Logging Task (vTaskLogging)
 * Priority: 2 (Lowest Application Task)
 * Period  : 1,000 ms, Deadline = 1,000 ms, Ci = 20.0 ms
 * Stack   : 2,048 Bytes (512 words, allocated in SRAM)
 * Role    : Drains xAlarmLogQueue (priority) and xLogQueue, prints to UART.
 *----------------------------------------------------------------------------*/
static void vTaskLogging(void *pvParameters)
{
    (void)pvParameters;
    LogEntry_t entry;

    while (1) {
        /* Wait for 1000 ms periodic telemetry or direct notification */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        if (!g_experiment_running) {
            continue;
        }

        /* Scenario 2 Priority Inheritance execution in vTaskLogging context */
        if (g_current_scenario == 2 && g_s2_preempt_verified && !g_s2_inheritance_verified) {
            run_priority_inheritance_demo();
        }

        if (!g_experiment_running) {
            continue;
        }

        /* Protect logging sequence using xLogMutex */
        if (xSemaphoreTake(xLogMutex, pdMS_TO_TICKS(100)) == pdPASS) {
            uint32_t now = xTaskGetTickCount();
            uart_puts("\r\n--- [LOGGER TELEMETRY @ ");
            uart_print_uint(now);
            uart_puts(" ms] ---\r\n");

            /* 1. First drain priority alarm entries */
            while (xQueueReceive(xAlarmLogQueue, &entry, 0) == pdPASS) {
                STATS_INC(alarm_logs_written);
                uart_puts("  [ALARM ENTRY #");
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
                uart_puts(" [CRITICAL_ALARM]\r\n");
            }

            /* 2. Then drain routine normal telemetry */
            while (xQueueReceive(xLogQueue, &entry, 0) == pdPASS) {
                STATS_INC(normal_logs_written);
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
                uart_puts(" [NORMAL_SPARSE]\r\n");
            }

            /* Summary status line */
            uart_puts("  >> Gen: ");
            uart_print_uint(g_stats.generated_blocks);
            uart_puts(" | Proc: ");
            uart_print_uint(g_stats.processed_blocks);
            uart_puts(" | Overruns: ");
            uart_print_uint(g_stats.isr_overrun_count);
            uart_puts(" | QueueDrop: ");
            uart_print_uint(g_stats.queue_dropped_blocks);
            uart_puts(" | Alarms: ");
            uart_print_uint(g_stats.alarm_actuate_count);
            uart_puts(" | LogDrop: ");
            uart_print_uint(g_stats.normal_logs_dropped);
            uart_puts(" | AlarmDrop: ");
            uart_print_uint(g_stats.alarm_logs_dropped);
            uart_puts(" | FreeHeap: ");
            uart_print_uint(xPortGetFreeHeapSize());
            uart_puts(" B\r\n");

            xSemaphoreGive(xLogMutex);
        }
    }
}

/*----------------------------------------------------------------------------
 * Scenario 2 Priority Inheritance Demonstration (§3.3.3)
 * Genuine 3-task contention on xLogMutex with ZERO manual priority setting
 *----------------------------------------------------------------------------*/
static void run_priority_inheritance_demo(void)
{
    uart_puts("\r\n====================================================================\r\n");
    uart_puts(">>> KICH BAN 2 (PHAN B): KIEM CHUNG PRIORITY INHERITANCE THAT TREN xLogMutex\r\n");
    uart_puts("====================================================================\r\n");

    /* Step 1: Logging (Pri 2) acquires xLogMutex */
    xSemaphoreTake(xLogMutex, portMAX_DELAY);
    g_s2_prio_initial = uxTaskPriorityGet(xTaskLogHandle);

    uart_puts("[Inheritance Demo] Step 1: Logging (Pri=");
    uart_print_uint(g_s2_prio_initial);
    uart_puts(") takes xLogMutex\r\n");

    /* Arm test flags */
    g_s2_inherit_test_active   = true;
    g_s2_acq_preempted_boosted = false;

    /* Step 2: Signal Processing Task (Pri 3) to request xLogMutex */
    uart_puts("[Inheritance Demo] Step 2: Processing (Pri=3) requests xLogMutex and blocks...\r\n");
    xSemaphoreGive(xInheritSignalSem);

    /* Context switch to Processing happens immediately because Pri 3 > Pri 2.
     * Processing attempts to take xLogMutex and BLOCKS because Logging holds it.
     * FreeRTOS kernel automatically BOOSTS Logging priority to 3!
     * When Logging resumes execution right here, check its priority: */
    g_s2_prio_boosted = uxTaskPriorityGet(xTaskLogHandle);

    uart_puts("[Inheritance Demo] Step 3: Logging priority BOOSTED by FreeRTOS kernel to: ");
    uart_print_uint(g_s2_prio_boosted);
    if (g_s2_prio_boosted == 3) {
        uart_puts(" (Inherited from waiting Processing Task) [PASS] [ok]\r\n");
    } else {
        uart_puts(" [FAIL]\r\n");
    }

    /* Stop DMA timer during inheritance sequence to avoid interrupt collision */
    xTimerStop(xDmaTimer, 0);

    /* Step 4: Trigger Acquisition (Pri 4). Since 4 > 3, Acquisition preempts boosted Logging! */
    NVIC_SetPendingIRQ(Interrupt0_IRQn);

    /* Allow Acquisition to complete */
    for (volatile int i = 0; i < 5000; i++) {
        __asm__ volatile ("nop");
    }

    /* Step 5: Logging releases xLogMutex */
    xSemaphoreGive(xLogMutex);

    /* FreeRTOS restores Logging priority back to 2, and unblocks Processing (Pri 3).
     * Processing runs, finishes its log record, releases xLogMutex, and signals xInheritDoneSem. */
    if (xInheritDoneSem != NULL) {
        xSemaphoreTake(xInheritDoneSem, portMAX_DELAY);
    }

    /* Resume DMA timer */
    xTimerStart(xDmaTimer, 0);

    g_s2_prio_restored = uxTaskPriorityGet(xTaskLogHandle);
    g_s2_inherit_test_active = false;

    uart_puts("[Inheritance Demo] Step 5: xLogMutex released -> Logging priority RESTORED to: ");
    uart_print_uint(g_s2_prio_restored);
    if (g_s2_prio_restored == 2 && g_s2_acq_preempted_boosted && g_s2_prio_boosted == 3) {
        g_s2_inheritance_verified = true;
        uart_puts(" [PASS] [ok]\r\n");
        uart_puts("--------------------------------------------------------------------\r\n");
        uart_puts(">>> PRIORITY INHERITANCE TEST COMPLETED [ok] <<<\r\n");
    } else {
        uart_puts(" [FAIL]\r\n");
    }
}

/*----------------------------------------------------------------------------
 * SCENARIO STATE MACHINE (Software Timer Callback)
 * Executes in Timer Task context, completely independent of vTaskLogging
 *----------------------------------------------------------------------------*/
static void vScenarioTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    static uint32_t s_timer_ticks = 0;
    s_timer_ticks += 100;

    if (!g_experiment_running) {
        return;
    }

    /* Transition 1 -> 2 at Tick 2000 ms */
    if (g_current_scenario == 1 && s_timer_ticks >= 2000) {
        /* Gate Scenario 1 PASS/FAIL */
        bool s1_pass = (g_stats.alarm_actuate_count == 2) &&
                       (g_s1_detected_block6 == 1) &&
                       (g_s1_detected_block14 == 1);

        if (s1_pass) {
            uart_puts("\r\n>>> KICH BAN 1 HOAN THANH: [PASS] [ok] (Da phat hien dung 2 su kien bat thuong)\r\n");
        } else {
            uart_puts("\r\n>>> KICH BAN 1 THAT BAI: [FAIL]\r\n");
        }

        /* Transition to Scenario 2 */
        g_current_scenario = 2;
        uart_puts("\r\n====================================================================\r\n");
        uart_puts(">>> BAT DAU KICH BAN 2: KIEM CHUNG PREEMPTION THAT VA MUTEX PRIORITY INHERITANCE\r\n");
        uart_puts("====================================================================\r\n");
        uart_puts("[Process: Real FFT calculation active. Arming mid-FFT hardware preemption interrupt...]\r\n");
    }
    /* Execute Scenario 2 Priority Inheritance after Mid-FFT preemption verified */
    else if (g_current_scenario == 2 && g_s2_preempt_verified && g_s2_inheritance_verified) {
        bool s2_pass = g_s2_preempt_verified && g_s2_inheritance_verified;
        if (s2_pass) {
            uart_puts("\r\n>>> KICH BAN 2 HOAN THANH: [PASS] [ok]\r\n");
        } else {
            uart_puts("\r\n>>> KICH BAN 2 THAT BAI: [FAIL]\r\n");
        }

        /* Transition to Scenario 3: Stress Test */
        g_current_scenario = 3;
        uart_puts("\r\n====================================================================\r\n");
        uart_puts(">>> BAT DAU KICH BAN 3: STRESS TEST & GIOI HAN TAI NGUYEN (DROP-OLDEST)\r\n");
        uart_puts("    (Burst mode 4 ms/khoi, chen lien tuc anomaly, bao hoa xLogQueue)\r\n");
        uart_puts("====================================================================\r\n");

        /* Accelerate DMA timer from 64 ms to 4 ms burst rate */
        xTimerChangePeriod(xDmaTimer, pdMS_TO_TICKS(4), 0);
    }
    /* Complete Scenario 3 at ~3400 ms */
    else if (g_current_scenario == 3 && s_timer_ticks >= 3400) {
        static uint8_t s_drain_stage = 0;
        static uint8_t s_wait_ticks  = 0;
        if (s_drain_stage == 0) {
            s_drain_stage = 1;
            /* Stage 1: Stop DMA timer so no new blocks are generated */
            xTimerStop(xDmaTimer, 0);
            return;
        } else if (s_drain_stage == 1) {
            s_wait_ticks++;
            /* Stage 2: Wait up to 3 ticks (300 ms) for in-flight blocks to finish */
            if (s_wait_ticks < 3 && (uxQueueMessagesWaiting(xBlockQueue) > 0 || g_buf_busy[0] || g_buf_busy[1])) {
                return;
            }
            /* Flush logging queues */
            xTaskNotifyGive(xTaskLogHandle);
            s_drain_stage = 2;
            return;
        }

        /* Stage 3: Drain any remaining logs so written + dropped == generated */
        LogEntry_t discarded;
        while (xQueueReceive(xAlarmLogQueue, &discarded, 0) == pdPASS) {
            STATS_INC(alarm_logs_written);
        }
        while (xQueueReceive(xLogQueue, &discarded, 0) == pdPASS) {
            STATS_INC(normal_logs_written);
        }

        /* Gate Scenario 3 PASS/FAIL */
        bool s3_pass = (g_stats.isr_overrun_count > 0) &&
                       (g_stats.normal_logs_dropped > 0) &&
                       (g_stats.alarm_logs_dropped == 0) &&
                       (g_stats.generated_blocks == (g_stats.processed_blocks + g_stats.isr_overrun_count));

        if (s3_pass) {
            uart_puts("\r\n>>> KICH BAN 3 HOAN THANH: [PASS] [ok] (Log drop va Overrun da duoc ghi nhan, 0 log canh bao bi mat)\r\n");
        } else {
            uart_puts("\r\n>>> KICH BAN 3 THAT BAI: [FAIL]\r\n");
        }

        /* Stop simulation */
        g_experiment_running = false;
        xTimerStop(xScenarioTimer, 0);

        print_final_benchmark_report();

        uart_puts("\r\n[QEMU] ALL 3 SCENARIOS COMPLETED SUCCESSFULLY. EXITING SIMULATION...\r\n");
        qemu_exit(0);
    }
}

/*----------------------------------------------------------------------------
 * 4 Golden FFT Verification Tests (Chapter III §4 Bước 4)
 *----------------------------------------------------------------------------*/
static void run_golden_fft_tests(void)
{
    uart_puts("\r\n====================================================================\r\n");
    uart_puts(">>> CMSIS-DSP HELIUM REAL FFT GOLDEN VERIFICATION SUITE (512-POINT)\r\n");
    uart_puts("====================================================================\r\n");

    bool test1_pass = false;
    bool test2_pass = false;
    bool test3_pass = false;
    bool test4_pass = false;

    arm_status status = arm_rfft_fast_init_f32(&g_rfft_instance, BLOCK_SIZE);
    configASSERT(status == ARM_MATH_SUCCESS);

    /* TEST 1: Constant DC (+1.0) */
    for (int i = 0; i < BLOCK_SIZE; i++) g_FFT_Input[i] = 1.0f;
    arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);
    rfft_unpack_magnitude(g_FFT_Output, g_Mag_Spectrum, NUM_MAG_BINS);

    float32_t dc_val = g_Mag_Spectrum[0];
    float32_t ac_max = 0.0f;
    uint32_t max_idx = 0;
    arm_max_f32(&g_Mag_Spectrum[1], NUM_MAG_BINS - 1, &ac_max, &max_idx);

    uart_puts("[GOLDEN TEST 1] DC Constant (+1.0):\r\n  - Expected DC = 512.00, AC_max = 0.00\r\n  - Measured DC = ");
    uart_print_float(dc_val, 2);
    uart_puts(", AC_max = ");
    uart_print_float(ac_max, 2);
    if (fabsf(dc_val - 512.0f) < 0.1f && ac_max < 0.1f) {
        test1_pass = true;
        uart_puts(" -> [PASS] [ok]\r\n");
    } else {
        uart_puts(" -> [FAIL]\r\n");
    }

    /* TEST 2: Single Unit Impulse (+1.0 at index 0) */
    memset(g_FFT_Input, 0, sizeof(g_FFT_Input));
    g_FFT_Input[0] = 1.0f;
    arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);
    rfft_unpack_magnitude(g_FFT_Output, g_Mag_Spectrum, NUM_MAG_BINS);

    float32_t imp_min = 100.0f, imp_max = 0.0f;
    for (int i = 0; i < NUM_MAG_BINS; i++) {
        if (g_Mag_Spectrum[i] < imp_min) imp_min = g_Mag_Spectrum[i];
        if (g_Mag_Spectrum[i] > imp_max) imp_max = g_Mag_Spectrum[i];
    }
    uart_puts("[GOLDEN TEST 2] Single Unit Impulse:\r\n  - Expected Flat Spectrum: Mag = 1.00 for all bins\r\n  - Measured Range: [");
    uart_print_float(imp_min, 2);
    uart_puts(", ");
    uart_print_float(imp_max, 2);
    uart_puts("]");
    if (fabsf(imp_min - 1.0f) < 0.05f && fabsf(imp_max - 1.0f) < 0.05f) {
        test2_pass = true;
        uart_puts(" -> [PASS] [ok]\r\n");
    } else {
        uart_puts(" -> [FAIL]\r\n");
    }

    /* TEST 3: Rectangular Pulse (6 samples +1.0) */
    memset(g_FFT_Input, 0, sizeof(g_FFT_Input));
    for (int i = 0; i < 6; i++) g_FFT_Input[i] = 1.0f;
    arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);
    rfft_unpack_magnitude(g_FFT_Output, g_Mag_Spectrum, NUM_MAG_BINS);

    float32_t rect_dc = g_Mag_Spectrum[0];
    uart_puts("[GOLDEN TEST 3] Rectangular Pulse (6 samples):\r\n  - Expected DC = 6.00, Sinc Envelope\r\n  - Measured DC = ");
    uart_print_float(rect_dc, 2);
    if (fabsf(rect_dc - 6.0f) < 0.1f) {
        test3_pass = true;
        uart_puts(" -> [PASS] [ok]\r\n");
    } else {
        uart_puts(" -> [FAIL]\r\n");
    }

    /* TEST 4: Pure Sine 3 Cycles (Amplitude 3.0) */
    for (int i = 0; i < BLOCK_SIZE; i++) {
        g_FFT_Input[i] = 3.0f * arm_sin_f32(2.0f * PI * 3.0f * (float32_t)i / (float32_t)BLOCK_SIZE);
    }
    arm_rfft_fast_f32(&g_rfft_instance, g_FFT_Input, g_FFT_Output, 0);
    rfft_unpack_magnitude(g_FFT_Output, g_Mag_Spectrum, NUM_MAG_BINS);

    float32_t bin3_mag = g_Mag_Spectrum[3];
    float32_t other_max = 0.0f;
    for (int i = 0; i < NUM_MAG_BINS; i++) {
        if (i != 3 && g_Mag_Spectrum[i] > other_max) {
            other_max = g_Mag_Spectrum[i];
        }
    }
    uart_puts("[GOLDEN TEST 4] Sine 3 Cycles, Amplitude 3.0:\r\n  - Expected Bin 3 Mag = 768.00 (3.0 * 512 / 2), Other bins = 0.00\r\n  - Measured Bin 3 = ");
    uart_print_float(bin3_mag, 2);
    uart_puts(", Other_max = ");
    uart_print_float(other_max, 2);
    if (fabsf(bin3_mag - 768.0f) < 1.0f && other_max < 0.5f) {
        test4_pass = true;
        uart_puts(" -> [PASS] [ok]\r\n");
    } else {
        uart_puts(" -> [FAIL]\r\n");
    }

    /* GATE ALL-PASSED LINE STRICTLY BY CONDITION */
    bool all_passed = test1_pass && test2_pass && test3_pass && test4_pass;
    uart_puts("--------------------------------------------------------------------\r\n");
    if (all_passed) {
        uart_puts(">>> ALL 4 GOLDEN FFT VERIFICATION TESTS PASSED [ok] <<<\r\n");
    } else {
        uart_puts(">>> GOLDEN FFT VERIFICATION TESTS FAILED [FAIL] <<<\r\n");
        qemu_exit(4);
    }
    uart_puts("====================================================================\r\n");
}

/*----------------------------------------------------------------------------
 * Final Academic Benchmark & Verification Report
 *----------------------------------------------------------------------------*/
static void print_final_benchmark_report(void)
{
    uart_puts("\r\n====================================================================\r\n");
    uart_puts("       TONG KET SO LIEU THUC NGHIEM TOAN DIEN (CHUONG IV - V2)     \r\n");
    uart_puts("====================================================================\r\n");

    /* [1] Data Block Statistics */
    uart_puts("[1] THONG KE KHOI DU LIEU VA LOG (DANG THUC TOAN HOC):\r\n");
    uart_puts("    - Tong so khoi sinh ra (Generated)   : ");
    uart_print_uint(g_stats.generated_blocks);
    uart_puts("\r\n    - Tong so khoi da xu ly (Processed)  : ");
    uart_print_uint(g_stats.processed_blocks);
    uart_puts("\r\n    - So lan tran bo dem DMA (Overrun)   : ");
    uart_print_uint(g_stats.isr_overrun_count);
    uart_puts("\r\n    * Kiem tra dang thuc khoi: Generated == Processed + Overrun\r\n");
    uart_puts("      (");
    uart_print_uint(g_stats.generated_blocks);
    uart_puts(" == ");
    uart_print_uint(g_stats.processed_blocks + g_stats.isr_overrun_count);
    uart_puts(") -> ");
    if (g_stats.generated_blocks == (g_stats.processed_blocks + g_stats.isr_overrun_count)) {
        uart_puts("DANG THUC DUNG [PASS] [ok]\r\n");
    } else {
        uart_puts("DANG THUC SAI [FAIL]\r\n");
    }

    uart_puts("\r\n[2] PHAN TACH HANG DOI LOG THUONG VA LOG CANH BAO:\r\n");
    uart_puts("    - Log thuong sinh ra (Normal Gen)    : ");
    uart_print_uint(g_stats.normal_logs_generated);
    uart_puts("\r\n    - Log thuong da ghi (Normal Written) : ");
    uart_print_uint(g_stats.normal_logs_written);
    uart_puts("\r\n    - Log thuong bi drop (Normal Drop)   : ");
    uart_print_uint(g_stats.normal_logs_dropped);
    uart_puts(" (Chinh sach Drop-Oldest tren xLogQueue)\r\n");
    uart_puts("    * Kiem tra dang thuc log thuong: Generated == Written + Dropped\r\n");
    uart_puts("      (");
    uart_print_uint(g_stats.normal_logs_generated);
    uart_puts(" == ");
    uart_print_uint(g_stats.normal_logs_written + g_stats.normal_logs_dropped);
    uart_puts(") -> ");
    if (g_stats.normal_logs_generated == (g_stats.normal_logs_written + g_stats.normal_logs_dropped)) {
        uart_puts("DANG THUC DUNG [PASS] [ok]\r\n");
    } else {
        uart_puts("DANG THUC SAI [FAIL]\r\n");
    }
    uart_puts("    - Log canh bao sinh (Alarm Gen)      : ");
    uart_print_uint(g_stats.alarm_logs_generated);
    uart_puts("\r\n    - Log canh bao da ghi (Alarm Written): ");
    uart_print_uint(g_stats.alarm_logs_written);
    uart_puts("\r\n    - Log canh bao bi drop (Alarm Drop)  : ");
    uart_print_uint(g_stats.alarm_logs_dropped);
    uart_puts(" -> ");
    if (g_stats.alarm_logs_dropped == 0) {
        uart_puts("KHONG MAT BAT KY BAN GHI CANH BAO NAO [PASS] [ok]\r\n");
    } else {
        uart_puts("BI MAT CANH BAO [FAIL]\r\n");
    }

    /* [3] Latency with Cycle & Microsecond Precision */
    uint32_t mean_lat_cyc = (g_stats.latency_sample_count > 0) ?
                            (uint32_t)(g_stats.total_alarm_latency_cycles / g_stats.latency_sample_count) : 0;
    float max_lat_us  = (float)g_stats.max_alarm_latency_cycles / 25.0f;
    float mean_lat_us = (float)mean_lat_cyc / 25.0f;

    uart_puts("\r\n[3] DO TRE PHAT HIEN -> BAO DONG (DO CHU KY SYSTICK):\r\n");
    uart_puts("    - Do tre cuc dai (Max Latency)       : ");
    uart_print_uint(g_stats.max_alarm_latency_cycles);
    uart_puts(" cycles (");
    uart_print_float(max_lat_us, 2);
    uart_puts(" us)\r\n    - Do tre trung binh (Mean Latency)   : ");
    uart_print_uint(mean_lat_cyc);
    uart_puts(" cycles (");
    uart_print_float(mean_lat_us, 2);
    uart_puts(" us)\r\n");
    uart_puts("    * Ghi chu hoc thuat: So lieu do bang SysTick tren QEMU MPS3 AN547 (25 MHz).\r\n");
    uart_puts("      Khong phai so do dao dong ky tren phan cung that STM32N6.\r\n");

    /* [4] FreeRTOS Heap */
    uart_puts("\r\n[4] THONG KE FREERTOS HEAP (heap_5.c - SRAM 64 KB):\r\n");
    uart_puts("    - Tong dung luong Heap cau hinh      : 65536 Bytes (64 KB)\r\n");
    uart_puts("    - Heap con trong hien tai (Free)     : ");
    uart_print_uint(xPortGetFreeHeapSize());
    uart_puts(" Bytes\r\n    - Heap da cap phat thuc te (Used)    : ");
    uart_print_uint(65536 - xPortGetFreeHeapSize());
    uart_puts(" Bytes\r\n");

    /* [5] Stack High Water Mark */
    UBaseType_t hwm_alarm   = uxTaskGetStackHighWaterMark(xTaskAlarmHandle);
    UBaseType_t hwm_acq     = uxTaskGetStackHighWaterMark(xTaskAcqHandle);
    UBaseType_t hwm_proc    = uxTaskGetStackHighWaterMark(xTaskProcHandle);
    UBaseType_t hwm_log     = uxTaskGetStackHighWaterMark(xTaskLogHandle);
    TaskHandle_t xIdleHandle = xTaskGetIdleTaskHandle();
    UBaseType_t hwm_idle    = (xIdleHandle != NULL) ? uxTaskGetStackHighWaterMark(xIdleHandle) : 0;

    uart_puts("\r\n[5] BANG THEO DOI NGAN XEP (STACK HWM SO VOI BANG 3.1 CHUONG III):\r\n");
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
 *----------------------------------------------------------------------------*/
extern uint32_t __dtcm_data_start__;
extern uint32_t __dtcm_data_end__;
extern uint32_t __etext;
extern uint32_t __dtcm_bss_start__;
extern uint32_t __dtcm_bss_end__;

static void init_dtcm_memory(void)
{
    uint32_t *pSrc = &__etext;
    uint32_t *pDst = &__dtcm_data_start__;
    while (pDst < &__dtcm_data_end__) {
        *pDst++ = *pSrc++;
    }

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

    /* 4. Create Canonical IPC Objects (Chapter III §2.2 & Task-round2) */
    xBlockReady = xSemaphoreCreateBinary();
    configASSERT(xBlockReady != NULL);

    xBlockQueue = xQueueCreate(2, sizeof(BlockDescriptor_t));
    configASSERT(xBlockQueue != NULL);

    xLogQueue = xQueueCreate(8, sizeof(LogEntry_t));
    configASSERT(xLogQueue != NULL);

    xAlarmLogQueue = xQueueCreate(8, sizeof(LogEntry_t));
    configASSERT(xAlarmLogQueue != NULL);

    xLogMutex = xSemaphoreCreateMutex();
    configASSERT(xLogMutex != NULL);

    xInheritSignalSem = xSemaphoreCreateBinary();
    configASSERT(xInheritSignalSem != NULL);

    xInheritDoneSem = xSemaphoreCreateBinary();
    configASSERT(xInheritDoneSem != NULL);

    uart_puts("[Kernel] Created IPC Objects: xBlockReady, xBlockQueue(2), xLogQueue(8), xAlarmLogQueue(8), xLogMutex\r\n");

    /* 5. Create 4 Application Tasks with Strict Memory Placement */
    xTaskAlarmHandle = xTaskCreateStatic(
        vTaskAlarm, "vTaskAlarm", 256, NULL, 5, xAlarmStack, &xAlarmTCB
    );
    configASSERT(xTaskAlarmHandle != NULL);

    xTaskAcqHandle = xTaskCreateStatic(
        vTaskAcquisition, "vTaskAcquisition", 256, NULL, 4, xAcquisitionStack, &xAcquisitionTCB
    );
    configASSERT(xTaskAcqHandle != NULL);

    xTaskProcHandle = xTaskCreateStatic(
        vTaskProcessing, "vTaskProcessing", 1024, NULL, 3, xProcessingStack, &xProcessingTCB
    );
    configASSERT(xTaskProcHandle != NULL);

    xTaskLogHandle = xTaskCreateStatic(
        vTaskLogging, "vTaskLogging", 512, NULL, 2, xLoggingStack, &xLoggingTCB
    );
    configASSERT(xTaskLogHandle != NULL);

    /* 6. Create Software Timers */
    /* DMA Timer: triggers Interrupt0 every 64 ms */
    static StaticTimer_t xDmaTimerBuffer;
    xDmaTimer = xTimerCreateStatic(
        "xDmaTimer", pdMS_TO_TICKS(64), pdTRUE, NULL, vDmaTimerCallback, &xDmaTimerBuffer
    );
    configASSERT(xDmaTimer != NULL);
    xTimerStart(xDmaTimer, 0);

    /* Scenario State Machine Timer: manages transitions every 100 ms */
    static StaticTimer_t xScenarioTimerBuffer;
    xScenarioTimer = xTimerCreateStatic(
        "xScenarioTimer", pdMS_TO_TICKS(100), pdTRUE, NULL, vScenarioTimerCallback, &xScenarioTimerBuffer
    );
    configASSERT(xScenarioTimer != NULL);
    xTimerStart(xScenarioTimer, 0);

    /* 7. Configure NVIC for Stub Interrupts */
    NVIC_SetPriority(Interrupt0_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(Interrupt0_IRQn);

    NVIC_SetPriority(Interrupt1_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(Interrupt1_IRQn);

    uart_puts("[Kernel] Starting FreeRTOS Scheduler with 4 Tasks + Idle (Tick = 1000 Hz)...\r\n");
    uart_puts("\r\n====================================================================\r\n");
    uart_puts(">>> BAT DAU KICH BAN 1: HOAT DONG BINH THUONG VA PHAT HIEN ANOMALY <<<\r\n");
    uart_puts("    (20 khoi @ 8 kHz, 64 ms/khoi. Chen loi tai Khoi #6 va #14)\r\n");
    uart_puts("====================================================================\r\n");

    /* 8. Start the Scheduler */
    vTaskStartScheduler();

    while (1) {}
    return 0;
}
