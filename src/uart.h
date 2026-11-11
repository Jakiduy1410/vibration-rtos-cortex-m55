#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *str);
void uart_print_uint(uint32_t val);
void uart_print_int(int32_t val);
void uart_print_float(float val, int decimals);
void qemu_exit(int status);

#endif /* UART_H */
