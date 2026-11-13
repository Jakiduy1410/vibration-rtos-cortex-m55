#include "uart.h"

/* CMSDK APB UART0 on MPS3 AN547: Non-Secure base 0x49303000 */
#define UART0_DATA_REG   (*(volatile uint32_t *)0x49303000UL)
#define UART0_STATE_REG  (*(volatile uint32_t *)0x49303004UL)
#define UART0_CTRL_REG   (*(volatile uint32_t *)0x49303008UL)
#define UART0_BAUDDIV    (*(volatile uint32_t *)0x49303010UL)

void uart_init(void)
{
    /* Set baudrate divider (25MHz / 115200) ~ 217 */
    UART0_BAUDDIV  = 217;
    /* Enable TX (bit 0) */
    UART0_CTRL_REG = 0x01;
}

void uart_putc(char c)
{
    if (c == '\n') {
        uart_putc('\r');
    }

    /* Wait while TX buffer full (bit 0 of STATE) */
    while (UART0_STATE_REG & 0x01) {
        /* Busy wait */
    }
    UART0_DATA_REG = (uint32_t)c;
}

void uart_puts(const char *str)
{
    if (!str) return;
    while (*str) {
        uart_putc(*str++);
    }
}

void uart_print_uint(uint32_t val)
{
    char buf[12];
    int i = 0;
    if (val == 0) {
        uart_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        uart_putc(buf[j]);
    }
}

void uart_print_int(int32_t val)
{
    if (val < 0) {
        uart_putc('-');
        val = -val;
    }
    uart_print_uint((uint32_t)val);
}

void uart_print_float(float val, int decimals)
{
    if (val < 0.0f) {
        uart_putc('-');
        val = -val;
    }
    float round_bias = 0.5f;
    for (int i = 0; i < decimals; i++) {
        round_bias *= 0.1f;
    }
    val += round_bias;

    uint32_t int_part = (uint32_t)val;
    uart_print_uint(int_part);
    if (decimals > 0) {
        uart_putc('.');
        float remainder = val - (float)int_part;
        for (int i = 0; i < decimals; i++) {
            remainder *= 10.0f;
            uint32_t d = (uint32_t)remainder;
            if (d > 9) d = 9;
            uart_putc((char)('0' + d));
            remainder -= (float)d;
        }
    }
}

void qemu_exit(int status)
{
    /* QEMU Semihosting SYS_EXIT */
    register uint32_t r0 __asm__("r0") = 0x18; /* SYS_EXIT */
    register uint32_t r1 __asm__("r1") = (status == 0) ? 0x20026 : 0x20027; /* ADP_Stopped_ApplicationExit */
    __asm__ volatile ("bkpt 0xAB" : : "r"(r0), "r"(r1) : "memory");
    while (1) {
        __asm__ volatile ("wfi");
    }
}
