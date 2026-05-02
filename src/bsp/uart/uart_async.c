#include "uart_async.h"
#include "service/tools/common_def.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "usart.h"

#define UART_ASYNC_TX_STREAM_BUFFER_SIZE 256
#define UART_ASYNC_RX_STREAM_BUFFER_SIZE 256

#define UART_ASYNC_TX_DMA_BUFFER_SIZE 256
#define UART_ASYNC_RX_DMA_BUFFER_SIZE 256

#define UART_ASYNC_TX_STREAM_BUFFER_ITEM_SIZE sizeof(uint8_t)
#define UART_ASYNC_RX_STREAM_BUFFER_ITEM_SIZE sizeof(uint8_t)

uint8_t uart_tx_dma_buffer[UART_ASYNC_TX_DMA_BUFFER_SIZE] __attribute__((section(".dma_buffer"), aligned(32), used));

uint8_t uart_rx_dma_buffer[UART_ASYNC_RX_DMA_BUFFER_SIZE] __attribute__((section(".dma_buffer"), aligned(32), used));

static StreamBufferHandle_t uart_tx_stream_buffer = NULL;
static StreamBufferHandle_t uart_rx_stream_buffer = NULL;

static TaskHandle_t uart_async_tx_task_handle = NULL;
static TaskHandle_t uart_async_rx_task_handle = NULL;

void uart_async_tx_task(void *param) {
    while (1) {
        const size_t actual_len = xStreamBufferReceive(uart_tx_stream_buffer, uart_tx_dma_buffer, UART_ASYNC_TX_DMA_BUFFER_SIZE, portMAX_DELAY);
        const HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart1, uart_tx_dma_buffer, actual_len);
        if (status != HAL_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

exit_code_t uart_async_init(void)
{
    uart_tx_stream_buffer = xStreamBufferCreate(UART_ASYNC_TX_STREAM_BUFFER_SIZE, UART_ASYNC_TX_STREAM_BUFFER_ITEM_SIZE);
    if (uart_tx_stream_buffer == NULL)
        return EXIT_NO_MEMORY;

    uart_rx_stream_buffer = xStreamBufferCreate(UART_ASYNC_RX_STREAM_BUFFER_SIZE, UART_ASYNC_RX_STREAM_BUFFER_ITEM_SIZE);
    if (uart_rx_stream_buffer == NULL)
        return EXIT_NO_MEMORY;


    return EXIT_OK;
}

exit_code_t uart_async_deinit(void)
{
    vStreamBufferDelete(uart_tx_stream_buffer);
    vStreamBufferDelete(uart_rx_stream_buffer);
    return EXIT_OK;
}

exit_code_t uart_async_start() {
    if (uart_tx_stream_buffer == NULL || uart_rx_stream_buffer == NULL) return EXIT_FAIL;
    const BaseType_t xTaskCreate_status = xTaskCreate(uart_async_tx_task, "uart_async_tx_task", configMINIMAL_STACK_SIZE, NULL, 1, &uart_async_tx_task_handle);

    if (xTaskCreate_status != pdPASS) return EXIT_FAIL;

    HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart_rx_dma_buffer, UART_ASYNC_RX_DMA_BUFFER_SIZE);
    if (status != HAL_OK) return EXIT_FAIL;
    return EXIT_OK;
}

exit_code_t uart_async_write(const uint8_t* data, const uint32_t len) {
    const size_t sent = xStreamBufferSend(uart_tx_stream_buffer, data, len, portMAX_DELAY);
    if (sent != len)
        return EXIT_FAIL;
    return EXIT_OK;
}

size_t uart_async_read(uint8_t* data, const uint32_t len) {
    const size_t received = xStreamBufferReceive(uart_rx_stream_buffer, data, len, portMAX_DELAY);
    return received;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        xTaskNotifyFromISR(uart_async_tx_task_handle, 0, eNoAction, NULL);
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART1) {

    }
}
