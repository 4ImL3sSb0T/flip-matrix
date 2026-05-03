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

static volatile uint16_t rx_last_pos = 0;
static volatile uint32_t rx_errors = 0;
static volatile uint32_t rx_dropped = 0;

static volatile uint32_t tx_errors = 0;

void uart_async_tx_task(void *param) {
    while (1) {
        const size_t actual_len = xStreamBufferReceive(uart_tx_stream_buffer, uart_tx_dma_buffer, UART_ASYNC_TX_DMA_BUFFER_SIZE, portMAX_DELAY);
        uint8_t retries = 3;
        while (retries--) {
            if (HAL_UART_Transmit_DMA(&huart1, uart_tx_dma_buffer, actual_len) == HAL_OK) {
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                break;
            }
            tx_errors++;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
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
    const BaseType_t xTaskCreate_status = xTaskCreate(uart_async_tx_task, "uart_async_tx_task", 256, NULL, 24, &uart_async_tx_task_handle);

    if (xTaskCreate_status != pdPASS) return EXIT_FAIL;

    const HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart_rx_dma_buffer, UART_ASYNC_RX_DMA_BUFFER_SIZE);
    if (status != HAL_OK) return EXIT_FAIL;
    return EXIT_OK;
}

exit_code_t uart_async_write(const uint8_t* data, const uint32_t len, const TickType_t timeout) {
    const size_t sent = xStreamBufferSend(uart_tx_stream_buffer, data, len, timeout);
    if (sent != len)
        return EXIT_FAIL;
    return EXIT_OK;
}

size_t uart_async_read(uint8_t* data, const uint32_t len, const TickType_t timeout) {
    const size_t received = xStreamBufferReceive(uart_rx_stream_buffer, data, len, timeout);
    return received;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        xTaskNotifyFromISR(uart_async_tx_task_handle, 0, eNoAction, NULL);
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART1) {
        const uint16_t pos = Size;
        const uint16_t last = rx_last_pos;

        if (pos > last) {
            const uint16_t len = pos - last;
            const size_t sent = xStreamBufferSendFromISR(uart_rx_stream_buffer, &uart_rx_dma_buffer[last], len, NULL);
            if (sent < len) rx_dropped += len - sent;
        } else if (pos < last) {
            const uint16_t len1 = UART_ASYNC_RX_DMA_BUFFER_SIZE - last;
            const size_t sent1 = xStreamBufferSendFromISR(uart_rx_stream_buffer, &uart_rx_dma_buffer[last], len1, NULL);
            if (sent1 < len1) rx_dropped += len1 - sent1;
            const size_t sent2 = xStreamBufferSendFromISR(uart_rx_stream_buffer, &uart_rx_dma_buffer[0], pos, NULL);
            if (sent2 < pos) rx_dropped += pos - sent2;
        }

        rx_last_pos = pos;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        rx_errors++;
        HAL_UART_AbortReceive(huart);
        rx_last_pos = 0;
        HAL_UARTEx_ReceiveToIdle_DMA(huart, uart_rx_dma_buffer, UART_ASYNC_RX_DMA_BUFFER_SIZE);
    }
}
