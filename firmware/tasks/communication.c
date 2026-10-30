/* S2-03: Communication owns parser/TX; IRQs only publish DMA progress. */
#include "communication.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timebase.h"
#include "transport.h"
#ifdef LOCKER_S205
#include "assembly.h"
#endif
#include <string.h>

volatile uint32_t s203_diag[24];
static mcu_transport_t transport;

#if BOARD_UART_QUALIFIED
static UART_HandleTypeDef uart;
static DMA_HandleTypeDef rx_dma, tx_dma;
/* Main SRAM only (scatter excludes CCM). DMA owns rx_ring until stopped. */
static volatile uint8_t rx_ring[2048];
static uint8_t rx_copy[2048];
static volatile uint32_t producer;
static uint16_t observed_position;
static volatile uint32_t rx_fault, tx_done;
static TaskHandle_t owner;

static void publish_position(void)
{
    uint16_t position = (uint16_t)(2048U - __HAL_DMA_GET_COUNTER(&rx_dma));
    uint16_t delta;
    if (position == 2048U) position = 0U;
    delta = (uint16_t)((position + 2048U - observed_position) % 2048U);
    producer += delta;
    observed_position = position;
}

static void notify_owner(void)
{
    BaseType_t wake = pdFALSE;
    if (owner != NULL) vTaskNotifyGiveFromISR(owner, &wake);
    portYIELD_FROM_ISR(wake);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *handle, uint16_t size)
{
    if (handle != &uart) return;
    /* Size denotes a position, not an increment. NDTR samples the current
       position, avoiding stale HT Size after a more recent IDLE interrupt. */
    (void)size;
    publish_position();
    ++s203_diag[4];
    notify_owner();
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle != &uart) return;
    tx_done = 1U;
    notify_owner();
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    if (handle != &uart) return;
    rx_fault = 1U;
    ++s203_diag[5];
    notify_owner();
}
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&uart); }
void DMA1_Stream5_IRQHandler(void) { HAL_DMA_IRQHandler(&rx_dma); }
void DMA1_Stream6_IRQHandler(void) { HAL_DMA_IRQHandler(&tx_dma); }

static int uart_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOD, &gpio);
    rx_dma.Instance = DMA1_Stream5;
    rx_dma.Init.Channel = DMA_CHANNEL_4;
    rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    rx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    rx_dma.Init.Mode = DMA_CIRCULAR;
    rx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    rx_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&rx_dma) != HAL_OK) return 0;
    tx_dma.Instance = DMA1_Stream6;
    tx_dma.Init = rx_dma.Init;
    tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    tx_dma.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&tx_dma) != HAL_OK) return 0;
    uart.Instance = USART2;
    uart.Init.BaudRate = 115200;
    uart.Init.WordLength = UART_WORDLENGTH_8B;
    uart.Init.StopBits = UART_STOPBITS_1;
    uart.Init.Parity = UART_PARITY_NONE;
    uart.Init.Mode = UART_MODE_TX_RX;
    uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart.Init.OverSampling = UART_OVERSAMPLING_16;
    __HAL_LINKDMA(&uart, hdmarx, rx_dma);
    __HAL_LINKDMA(&uart, hdmatx, tx_dma);
    if (HAL_UART_Init(&uart) != HAL_OK) return 0;
    /* All UART/DMA callbacks serialized; logical 5 permits FromISR. */
    HAL_NVIC_SetPriority(USART2_IRQn, 5U, 0U);
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 5U, 0U);
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    return HAL_UARTEx_ReceiveToIdle_DMA(&uart, (uint8_t *)rx_ring, sizeof(rx_ring)) == HAL_OK;
}
#endif

void communication_task(void *argument)
{
#ifdef LOCKER_S205
    uint64_t progress = 0U;
#endif
    (void)argument;
    /* No backend until S2-04/05; no fake journal/Control success. */
    {
        uint8_t uid[12];
        memcpy(uid, (const void *)UID_BASE, sizeof(uid));
#ifdef LOCKER_S205
        while(!assembly_ready()) vTaskDelay(pdMS_TO_TICKS(1U));
        mcu_transport_init(&transport, uid, 0U, NULL, assembly_dispatch);
        transport.backend_hello = true;
#else
        mcu_transport_init(&transport, uid, 0U, NULL, NULL);
#endif
    }
    s203_diag[0] = 0x53323033U;
#if BOARD_UART_QUALIFIED
    {
        uint32_t consumed = 0U;
        uint64_t previous = mcu_board_monotonic_ms();
        int transmitting = 0;
        owner = xTaskGetCurrentTaskHandle();
        if (!uart_init()) { s203_diag[1] = 2U; vTaskSuspend(NULL); }
        s203_diag[1] = 1U;
        for (;;) {
            uint32_t begin, end, count, i;
            uint64_t now;
            const mcu_transport_packet_t *packet;
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5U));
            now = mcu_board_monotonic_ms();
            /* At 115200 8N1 a full ring takes 177.8ms. A long scheduler
               stall invalidates session even if modulo progress is ambiguous. */
            taskENTER_CRITICAL();
            publish_position();
            begin = producer;
            taskEXIT_CRITICAL();
            count = begin - consumed;
            if (rx_fault != 0U || now - previous >= 170U || count >= sizeof(rx_ring)) {
                mcu_transport_rx_lost(&transport);
#ifdef LOCKER_S205
                assembly_session_lost();
#endif
                ++s203_diag[6];
                consumed = begin;
                if (rx_fault != 0U) {
                    /* DMA error can also terminate TX without its completion
                       callback. Release its buffer only after synchronous abort. */
                    if (HAL_UART_AbortTransmit(&uart) != HAL_OK ||
                        HAL_UART_AbortReceive(&uart) != HAL_OK) {
                        s203_diag[1] = 4U;
                        vTaskSuspend(NULL);
                    }
                    if (transmitting) mcu_transport_tx_pop(&transport);
                    transmitting = 0; tx_done = 0U;
                    taskENTER_CRITICAL();
                    producer = 0U; observed_position = 0U; consumed = 0U;
                    rx_fault = 0U;
                    taskEXIT_CRITICAL();
                    if (HAL_UARTEx_ReceiveToIdle_DMA(&uart, (uint8_t *)rx_ring, sizeof(rx_ring)) != HAL_OK) {
                        s203_diag[1] = 3U;
                        vTaskSuspend(NULL);
                    }
                }
            } else if (count != 0U) {
                for (i = 0U; i < count; ++i) rx_copy[i] = rx_ring[(consumed + i) % sizeof(rx_ring)];
                __DMB();
                taskENTER_CRITICAL();
                publish_position(); end = producer;
                taskEXIT_CRITICAL();
                if (end - consumed >= sizeof(rx_ring) || mcu_board_monotonic_ms() - now >= 170U) {
                    mcu_transport_rx_lost(&transport);
#ifdef LOCKER_S205
                    assembly_session_lost();
#endif
                    ++s203_diag[6]; consumed = end;
                } else {
                    (void)mcu_transport_feed(&transport, rx_copy, count, now);
                    consumed = begin;
                    s203_diag[7] += count;
                }
            }
            previous = now;
#ifdef LOCKER_S205
            assembly_transmit(&transport);
#endif
            if (transmitting && tx_done != 0U) {
                mcu_transport_tx_pop(&transport);
                tx_done = 0U; transmitting = 0;
                ++s203_diag[8];
            }
            packet = mcu_transport_tx_peek(&transport);
            if (!transmitting && packet != NULL) {
                if (HAL_UART_Transmit_DMA(&uart, (uint8_t *)packet->bytes, (uint16_t)packet->length) == HAL_OK)
                    transmitting = 1;
                else { ++s203_diag[9]; }
            }
            s203_diag[10] = (uint32_t)transport.stream.stats.crc_errors;
            s203_diag[11] = (uint32_t)transport.stream.stats.length_errors;
            s203_diag[12] = (uint32_t)transport.stats.bad_session;
            s203_diag[13] = (uint32_t)transport.stats.tx_full;
            s203_diag[14] = (uint32_t)transport.stats.bad_payload;
            s203_diag[15] = (uint32_t)transport.stats.bad_version;
            s203_diag[16] = (uint32_t)transport.stats.unsupported;
            s203_diag[17] = (uint32_t)transport.stats.stale_replies;
            ++s203_diag[2];
#ifdef LOCKER_S205
            assembly_progress(LOCKER_MCU_TASK_ID_COMMUNICATION, ++progress);
#endif
        }
    }
#else
    /* Unverified isolation supplies/map: no alternate function or DMA. */
    s203_diag[1] = 0U;
    for (;;) { ++s203_diag[2]; vTaskDelay(pdMS_TO_TICKS(100U)); }
#endif
}
