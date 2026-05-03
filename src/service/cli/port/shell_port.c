#include "shell_port.h"
#include "service/cli/shell.h"
#include "service/cli/log/log.h"
#include "bsp/uart/uart_async.h"
#include "FreeRTOS.h"
#include "task.h"

#define SHELL_TASK_STACK_SIZE 256
#define SHELL_TASK_PRIORITY   2
#define SHELL_READ_TIMEOUT_MS 100

static Shell shell;
static Log log;
static char shell_buffer[512];

static signed short shell_read(char *data, unsigned short len) {
    const size_t received = uart_async_read((uint8_t *)data, len, pdMS_TO_TICKS(SHELL_READ_TIMEOUT_MS));
    return (signed short)received;
}

static signed short shell_write(char *data, unsigned short len) {
    if (uart_async_write((const uint8_t *)data, len, portMAX_DELAY) == EXIT_OK)
        return (signed short)len;
    return 0;
}

static void log_write(char *data, short len) {
    uart_async_write((const uint8_t *)data, (uint32_t)len, portMAX_DELAY);
}

static void shell_task(void *param) {
    shellTask(&shell);
}

exit_code_t shell_port_init(void) {
    shell.read = shell_read;
    shell.write = shell_write;
    shellInit(&shell, shell_buffer, sizeof(shell_buffer));

    log.write = log_write;
    log.active = 1;
    log.level = LOG_ALL;
    logRegister(&log, &shell);

    return EXIT_OK;
}

exit_code_t shell_port_start(void) {
    const BaseType_t ret = xTaskCreate(shell_task, "shell", SHELL_TASK_STACK_SIZE,
                                        NULL, SHELL_TASK_PRIORITY, NULL);
    return (ret == pdPASS) ? EXIT_OK : EXIT_FAIL;
}
