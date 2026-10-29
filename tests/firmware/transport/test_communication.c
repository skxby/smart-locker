/* HAL/RTOS interface regression. This fixture does not model electrical UART. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../../../firmware/tasks/communication.c"

uint8_t fixture_uid[12] = {1u,2u,3u};
static jmp_buf stop;
static unsigned clocks, pins, dma_inits, uart_inits, starts, sends, abort_rx, abort_tx;
static unsigned notices, critical, step, scenario;
static uint16_t remaining = 2048u;
static uint64_t mono;
static uint8_t *dma_bytes, *sending;
static uint16_t sending_length;
static uint8_t sending_snapshot[524];
static int transmit_status;
static int abort_status, receive_status, expected_suspend;

void fixture_clock(void) { ++clocks; }
uint16_t fixture_counter(DMA_HandleTypeDef *d) { assert(d->Instance == DMA1_Stream5); return remaining; }
void fixture_critical(int enter) { if (enter) ++critical; else { assert(critical); --critical; } }
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *g)
{
    assert(port == GPIOD && g->Pin == (GPIO_PIN_5 | GPIO_PIN_6));
    assert(g->Mode == GPIO_MODE_AF_PP && g->Pull == GPIO_PULLUP);
    assert(g->Alternate == GPIO_AF7_USART2); ++pins;
}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *d)
{
    assert(d->Init.Channel == DMA_CHANNEL_4 && d->Init.MemInc == DMA_MINC_ENABLE);
    assert(d->Init.PeriphInc == DMA_PINC_DISABLE);
    assert(d->Init.PeriphDataAlignment == DMA_PDATAALIGN_BYTE);
    assert(d->Init.MemDataAlignment == DMA_MDATAALIGN_BYTE);
    if (d->Instance == DMA1_Stream5)
        assert(d->Init.Mode == DMA_CIRCULAR && d->Init.Direction == DMA_PERIPH_TO_MEMORY);
    else assert(d->Instance == DMA1_Stream6 && d->Init.Mode == DMA_NORMAL && d->Init.Direction == DMA_MEMORY_TO_PERIPH);
    ++dma_inits; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *h)
{
    assert(h->Instance == USART2 && h->Init.BaudRate == 115200u);
    assert(h->Init.WordLength == UART_WORDLENGTH_8B && h->Init.StopBits == UART_STOPBITS_1);
    assert(h->Init.Parity == UART_PARITY_NONE && h->Init.HwFlowCtl == UART_HWCONTROL_NONE);
    assert(h->hdmarx && h->hdmatx); ++uart_inits; return HAL_OK;
}
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, uint8_t *p, uint16_t n)
{
    (void)h; assert(n == 2048u); dma_bytes = p; remaining = 2048u; ++starts; return receive_status;
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, uint8_t *p, uint16_t n)
{
    (void)h; ++sends;
    if (transmit_status) return HAL_ERROR;
    assert(n <= sizeof(sending_snapshot)); sending = p; sending_length = n;
    memcpy(sending_snapshot, p, n); return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h) { (void)h; ++abort_rx; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h) { (void)h; ++abort_tx; return abort_status; }
void HAL_NVIC_SetPriority(int irq, unsigned p, unsigned sub) { assert((irq == 2 || irq == 5 || irq == 6) && p == 5 && sub == 0); }
void HAL_NVIC_EnableIRQ(int irq) { assert(irq == 2 || irq == 5 || irq == 6); }
void HAL_UART_IRQHandler(UART_HandleTypeDef *h) { (void)h; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h) { (void)h; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
void vTaskNotifyGiveFromISR(TaskHandle_t h, BaseType_t *wake) { assert(h == (void *)1); ++notices; *wake = pdTRUE; }
uint64_t mcu_board_monotonic_ms(void) { return mono; }
void vTaskSuspend(TaskHandle_t h)
{
    (void)h;
    assert(expected_suspend && s203_diag[1] == (uint32_t)expected_suspend);
    assert(!transport.session_valid || expected_suspend == 2);
    longjmp(stop, 1);
}
void vTaskDelay(uint32_t ticks) { assert(ticks == 100u); longjmp(stop, 1); }

#if BOARD_UART_QUALIFIED
static uint8_t hello[524];
static size_t hello_length;
static unsigned write_position;

static void push(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) { dma_bytes[write_position] = p ? p[i] : 0; write_position = (write_position + 1u) % 2048u; }
    remaining = (uint16_t)(2048u - write_position);
}
static void event(uint16_t size)
{
    uint64_t before = transport.stats.requests[0];
    uint64_t lost = transport.stats.rx_lost;
    unsigned sent = sends;
    HAL_UARTEx_RxEventCallback(&uart, size);
    /* An IRQ may publish counters/notify only; parser/TX run in the task. */
    assert(transport.stats.requests[0] == before && transport.stats.rx_lost == lost && sends == sent);
}
static void check_inflight(void)
{
    assert(sending && memcmp(sending, sending_snapshot, sending_length) == 0);
}

uint32_t ulTaskNotifyTake(BaseType_t clear, uint32_t ticks)
{
    assert(clear == pdTRUE && ticks == 5u && critical == 0u);
    mono += 5u;
    ++step;
    if (scenario == 0u) {
        switch (step) {
        case 1:
            assert(clocks == 3u && pins == 1u && dma_inits == 2u && uart_inits == 1u && starts == 1u);
            push(hello, 5u); event(5u); event(5u); assert(producer == 5u); break;
        case 2:
            assert(transport.stream.length == 5u && sends == 0u);
            push(hello + 5u, hello_length - 5u); event((uint16_t)hello_length); break;
        case 3:
            assert(transport.stats.requests[0] == 1u && sends == 1u && transport.tx_count == 1u);
            check_inflight(); push(NULL, 1024u - write_position); event(1024u); event(1024u); break;
        case 4:
            check_inflight(); push(NULL, 1024u); event(2048u); event(2048u); assert(producer == 2048u); break;
        case 5:
            assert(s203_diag[7] == 2048u); check_inflight();
            push(hello, hello_length); event((uint16_t)hello_length);
            /* Stale HT Size must not invent a wrap: current NDTR is authoritative. */
            event(1024u); assert(producer == 2048u + hello_length); break;
        case 6:
            assert(transport.stats.requests[0] == 2u && transport.tx_count == 2u);
            check_inflight(); HAL_UART_TxCpltCallback(&uart); assert(transport.tx_count == 2u); break;
        case 7:
            assert(s203_diag[8] == 1u && transport.tx_count == 1u && sends == 2u);
            /* Exactly one full unconsumed circle, with both IRQ positions. */
            push(NULL, 1024u); event((uint16_t)write_position);
            push(NULL, 1024u); event((uint16_t)write_position); break;
        case 8:
            assert(transport.stats.rx_lost == 1u && !transport.session_valid && transport.fault_latched);
            check_inflight(); HAL_UART_ErrorCallback(&uart); assert(abort_rx == 0u); break;
        case 9:
            assert(abort_tx == 1u && abort_rx == 1u && starts == 2u && transport.stats.rx_lost == 2u);
            assert(transport.tx_count == 0u);
            assert(!transport.session_valid && remaining == 2048u);
            longjmp(stop, 1); break;
        default: assert(0);
        }
    } else if (scenario == 1u) {
        if (step == 1u) { push(hello, hello_length); event((uint16_t)hello_length); }
        if (step == 2u) { assert(sends == 1u); check_inflight(); mono += 170u; }
        if (step == 3u) { assert(transport.stats.rx_lost == 1u && !transport.session_valid); longjmp(stop, 1); }
    } else if (scenario == 2u || scenario == 3u) {
        if (step == 1u) { push(hello, hello_length); event((uint16_t)hello_length); }
        if (step == 2u) {
            check_inflight();
            if (scenario == 2u) { abort_status = HAL_ERROR; expected_suspend = 4; }
            else { receive_status = HAL_ERROR; expected_suspend = 3; }
            HAL_UART_ErrorCallback(&uart);
        }
        assert(step < 3u);
    } else if (scenario == 4u) {
        if (step == 1u) { transmit_status = HAL_ERROR; push(hello, hello_length); event((uint16_t)hello_length); }
        if (step == 2u) { assert(sends == 1u && transport.tx_count == 1u && s203_diag[9] == 1u); transmit_status = HAL_OK; }
        if (step == 3u) { assert(sends == 2u && transport.tx_count == 1u); check_inflight(); HAL_UART_TxCpltCallback(&uart); }
        if (step == 4u) { assert(transport.tx_count == 0u && s203_diag[8] == 1u); longjmp(stop, 1); }
    }
    return 1u;
}

static void reset(void)
{
    clocks = pins = dma_inits = uart_inits = starts = sends = abort_rx = abort_tx = notices = critical = step = 0u;
    mono = producer = observed_position = rx_fault = tx_done = write_position = 0u;
    abort_status = receive_status = transmit_status = expected_suspend = 0;
    remaining = 2048u; owner = NULL; sending = NULL; memset((void *)s203_diag, 0, sizeof(s203_diag));
    memset(&uart, 0, sizeof(uart)); memset(&rx_dma, 0, sizeof(rx_dma)); memset(&tx_dma, 0, sizeof(tx_dma));
}
#else
uint32_t ulTaskNotifyTake(BaseType_t clear, uint32_t ticks) { (void)clear; (void)ticks; assert(0); return 0; }
#endif

int main(void)
{
    (void)step; (void)scenario;
#if BOARD_UART_QUALIFIED
    locker_wire_request_union_t request;
    memset(&request, 0, sizeof(request));
    request.hello.new_session_id.bytes[0] = 1u;
    request.hello.host_instance_id.bytes[0] = 2u;
    request.hello.requested_version = 1u;
    assert(locker_wire_encode_request(1u, 5u, &request, hello, sizeof(hello), &hello_length) == LOCKER_STATUS_OK);
    for (scenario = 0; scenario < 6u; ++scenario) {
        reset();
        if (scenario == 5u) { receive_status = HAL_ERROR; expected_suspend = 2; }
        if (setjmp(stop) == 0) communication_task(NULL);
        assert(critical == 0u && s203_diag[0] == 0x53323033u);
    }
#else
    if (setjmp(stop) == 0) communication_task(NULL);
    assert(clocks == 0u && pins == 0u && dma_inits == 0u && uart_inits == 0u && starts == 0u && sends == 0u);
    assert(s203_diag[1] == 0u && s203_diag[2] == 1u);
#endif
    puts("communication HAL boundary checks passed");
    return 0;
}
