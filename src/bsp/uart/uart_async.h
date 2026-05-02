#ifndef UART_ASYNC_H
#define UART_ASYNC_H

#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "service/tools/common_def.h"

exit_code_t uart_async_init(void);
exit_code_t uart_async_deinit(void);
exit_code_t uart_async_start(void);

exit_code_t uart_async_write(const uint8_t *data, uint32_t len, TickType_t timeout);
size_t uart_async_read(uint8_t *data, uint32_t len, TickType_t timeout);

#endif
